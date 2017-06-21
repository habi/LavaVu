/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
** Copyright (c) 2010, Monash University
** All rights reserved.
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
**       * Redistributions of source code must retain the above copyright notice,
**          this list of conditions and the following disclaimer.
**       * Redistributions in binary form must reproduce the above copyright
**         notice, this list of conditions and the following disclaimer in the
**         documentation and/or other materials provided with the distribution.
**       * Neither the name of the Monash University nor the names of its contributors
**         may be used to endorse or promote products derived from this software
**         without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
** THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
** BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
** OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
**
** Contact:
*%  Owen Kaluza - Owen.Kaluza(at)monash.edu
*%
*% Development Team :
*%  http://www.underworldproject.org/aboutus.html
**
**~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

//Model class
#include "Model.h"

Database::Database() : readonly(true), silent(true), attached(NULL), db(NULL), memory(false)
{
  prefix[0] = '\0';
}

Database::Database(const FilePath& fn) : readonly(true), silent(true), attached(NULL), db(NULL), file(fn), memory(false)
{
  prefix[0] = '\0';
}

Database::~Database()
{
  if (db) sqlite3_close(db);
}

bool Database::open(bool write)
{
  //Single file database
  char path[FILE_PATH_MAX];
  int flags = SQLITE_OPEN_READONLY;
  if (write)
  {
    flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
    readonly = false;
  }

  strcpy(path, file.full.c_str());
  if (strstr(path, "file:")) 
  {
    flags = flags | SQLITE_OPEN_URI;
    memory = true;
  }

  if (strstr(path, "mode=memory"))
    memory = true;

  debug_print("Opening db %s with flags %d\n", path, flags);

  int ret = sqlite3_open_v2(path, &db, flags, NULL);
  if (ret != SQLITE_OK)
  {
    debug_print("Can't open database %s: %s\n", path, sqlite3_errmsg(db));
    return false;
  }

  // Success
  debug_print("Open database %s successful, SQLite version %s\n", path, sqlite3_libversion());
  //rc = sqlite3_busy_handler(db, busy_handler, void*);
  sqlite3_busy_timeout(db, 10000); //10 seconds

  return true;
}

void Database::reopen(bool write)
{
  if (!readonly || !db) return;
  if (db) sqlite3_close(db);
  open(write);

  //Re-attach any attached db file
  if (attached)
  {
    char SQL[SQL_QUERY_MAX];
    sprintf(SQL, "attach database '%s' as t%d", attached->path.c_str(), attached->step);
    if (issue(SQL))
      debug_print("Database %s found and re-attached\n", attached->path.c_str());
  }
}

void Database::attach(TimeStep* timestep)
{
  //Detach any attached db file
  if (memory) return;
  char SQL[SQL_QUERY_MAX];
  if (attached && attached->step != timestep->step)
  {
    sprintf(SQL, "detach database 't%d'", attached->step);
    if (issue(SQL))
    {
      debug_print("Database t%d detached\n", attached->step);
      attached = NULL;
      prefix[0] = '\0';
    }
    else
      debug_print("Database t%d detach failed!\n", attached->step);
  }

  //Attach n'th timestep database if available
  if (timestep->step > 0 && !attached)
  {
    const std::string& path = timestep->path;
    if (path.length() > 0)
    {
      sprintf(SQL, "attach database '%s' as t%d", path.c_str(), timestep->step);
      if (issue(SQL))
      {
        sprintf(prefix, "t%d.", timestep->step);
        debug_print("Database %s found and attached\n", path.c_str());
        attached = timestep;
      }
      else
      {
        debug_print("Database %s found but attach failed!\n", path.c_str());
      }
    }
    //else
    //   debug_print("Database %s not found, loading from current db\n", path.c_str());
  }
}

//SQLite3 utility functions
sqlite3_stmt* Database::select(const char* fmt, ...)
{
  GET_VAR_ARGS(fmt, SQL);

  //debug_print("Issuing select: %s\n", SQL);
  sqlite3_stmt* statement;
  //Prepare statement...
  int rc = sqlite3_prepare_v2(db, SQL, -1, &statement, NULL);
  if (rc != SQLITE_OK)
  {
    if (!silent)
      debug_print("Prepare error (%s) %s\n", SQL, sqlite3_errmsg(db));
    return NULL;
  }
  return statement;
}

bool Database::issue(const char* fmt, ...)
{
  GET_VAR_ARGS(fmt, SQL);
  // Executes a basic SQLite command (ie: without pointer objects and ignoring result sets) and checks for errors
  //debug_print("Issuing: %s\n", SQL);
  char* zErrMsg = NULL;
  if (sqlite3_exec(db, SQL, NULL, 0, &zErrMsg) != SQLITE_OK)
  {
    std::cerr << "SQLite error: " << (zErrMsg ? zErrMsg : "(no error msg)") << std::endl;
    std::cerr << " -- " << SQL << std::endl;
    sqlite3_free(zErrMsg);
    return false;
  }
  return true;
}

Model::Model(DrawState& drawstate) : now(-1), drawstate(drawstate), figure(-1)
{
  //Create new geometry containers
  init();
}

void Model::load(const FilePath& fn)
{
  //Open database file
  database = Database(fn);
  if (database.open())
  {
    loadTimeSteps();
    loadColourMaps();
    loadObjects();
    loadViewports();
    loadWindows();
  }
  else
  {
    std::cerr << "Model database open failed: " << fn.full << std::endl;
  }
}


View* Model::defaultView()
{
  //No views?
  if (views.size() == 0)
  {
    //Default view
    View* view = new View(drawstate);
    views.push_back(view);
  }

  //No objects?
  if (views[0]->objects.size() == 0)
  {
    //Add all objects to first viewport
    for (unsigned int o=0; o<objects.size(); o++)
    {
      views[0]->addObject(objects[o]);
      loadLinks(objects[o]);
    }
  }

  //Return first
  return views[0];
}

void Model::init()
{
  //Create new geometry containers
  geometry.clear();
  geometry.resize(lucMaxType);
  geometry[lucLabelType] = labels = new Geometry(drawstate);
  geometry[lucPointType] = points = new Points(drawstate);
  geometry[lucVectorType] = vectors = new Vectors(drawstate);
  geometry[lucTracerType] = tracers = new Tracers(drawstate);
  geometry[lucGridType] = quadSurfaces = new QuadSurfaces(drawstate);
  geometry[lucVolumeType] = volumes = new Volumes(drawstate);
  geometry[lucTriangleType] = triSurfaces = new TriSurfaces(drawstate);
  geometry[lucLineType] = lines = new Lines(drawstate);
  //TODO: all renderers should be switchable and user defined, for now leave out links (slow)
  //geometry[lucLineType] = lines = new Links(drawstate);
  geometry[lucShapeType] = shapes = new Shapes(drawstate);
  debug_print("Created %d new geometry containers\n", geometry.size());

  for (unsigned int i=0; i < geometry.size(); i++)
  {
    bool hideall = drawstate.global("hideall");
    if (hideall)
      geometry[i]->hideShowAll(true);
  }
}

Model::~Model()
{
  clearTimeSteps();

  //Clear drawing objects
  for(unsigned int i=0; i<objects.size(); i++)
    delete objects[i];

  //Clear views
  for(unsigned int i=0; i<views.size(); i++)
    delete views[i];

  //Clear colourmaps
  for(unsigned int i=0; i<colourMaps.size(); i++)
    delete colourMaps[i];
}

bool Model::loadFigure(int fig)
{
  if (fig < 0 || figures.size() == 0) return false;
  if (fig >= (int)figures.size()) fig = 0;
  if (fig < 0) fig = figures.size()-1;
  figure = fig;
  assert(figure >= 0);
  jsonRead(figures[figure]);

  //Set window caption
  if (fignames[figure].length() > 0)
    drawstate.globals["caption"] = fignames[figure];
  else if (!database.memory) 
    drawstate.globals["caption"] = database.file.base;
  return true;
}

void Model::storeFigure()
{
  //Add default figure
  if (figure < 0)
  {
    figure = 0;
    if (figures.size() == 0)
      figure = addFigure("default");
  }
  assert(figure >= 0);
  assert(figure < (int)figures.size());

  //Save current state in current selected figure
  figures[figure] = jsonWrite();
}

int Model::addFigure(std::string name, const std::string& state)
{
  if (name.length() == 0) name = "(unnamed)";
  //Check for duplicate names
  bool found = false;
  do
  {
    found = false;
    for (auto n : fignames)
    {
      //Duplicate name, add underscore until not a duplicate
      if (n == name)
      {
        found = true;
        name += "_";
      }
    }
  } while (found);

  fignames.push_back(name);
  if (state.length())
    figures.push_back(state);
  else
    figures.push_back(jsonWrite());

  return figures.size()-1;
}

void Model::addObject(DrawingObject* obj)
{
  //Check for duplicate names / objects
  bool found = false;
  do
  {
    found = false;
    for (auto o : objects)
    {
      //Duplicate object, skip
      if (o == obj) return;
      //Duplicate name, add underscore until not a duplicate
      if (o->name() == obj->name())
      {
        found = true;
        obj->properties.data["name"] = obj->name() + "_";
      }
    }
  } while (found);

  //Create master drawing object list entry
  objects.push_back(obj);
}

DrawingObject* Model::findObject(unsigned int id)
{
  for (unsigned int i=0; i<objects.size(); i++)
    if (objects[i]->dbid == id) return objects[i];
  return NULL;
}

void Model::close()
{
  for (unsigned int i=0; i < geometry.size(); i++)
    delete geometry[i];
  geometry.clear();

  labels = NULL;
  points = NULL;
  vectors = NULL;
  tracers = NULL;
  quadSurfaces = NULL;
  triSurfaces = NULL;
  lines = NULL;
  shapes = NULL;
  volumes = NULL;
}

void Model::clearObjects(bool all)
{
  if (membytes__ > 0 && geometry.size() > 0)
    debug_print("Clearing geometry, geom memory usage before clear %.3f mb\n", membytes__/1000000.0f);

  //Clear containers...
  for (unsigned int i=0; i < geometry.size(); i++)
    geometry[i]->clear(all);
}

void Model::setup()
{
  //Setup min/max on all object data
  for (unsigned int i=0; i < objects.size(); i++)
  {
    points->setupObject(objects[i]);
    quadSurfaces->setupObject(objects[i]);
    triSurfaces->setupObject(objects[i]);
    vectors->setupObject(objects[i]);
    tracers->setupObject(objects[i]);
    shapes->setupObject(objects[i]);
    lines->setupObject(objects[i]);
    volumes->setupObject(objects[i]);
  }
}

void Model::reload(DrawingObject* obj)
{
  //Full data reload on selected object only
  for (unsigned int i=0; i < geometry.size(); i++)
    geometry[i]->redrawObject(obj);

  for (unsigned int i = 0; i < colourMaps.size(); i++)
    colourMaps[i]->calibrated = false;
}

void Model::redraw(bool reload)
{
  //Flag redraw on all objects...
  for (unsigned int i=0; i < geometry.size(); i++)
  {
    if (reload) 
      //Full data reload...
      geometry[i]->reload = true;
    else
      //Just flag a redraw, will only be reloaded if vertex count changed
      geometry[i]->redraw = true;
  }

  for (unsigned int i = 0; i < colourMaps.size(); i++)
    colourMaps[i]->calibrated = false;
}

//Adds colourmap
unsigned int Model::addColourMap(ColourMap* cmap)
{
  //Create a default greyscale map
  if (!cmap) cmap = new ColourMap(drawstate, "default", "colours=#000000 #ffffff");
  //Save colour map in list
  colourMaps.push_back(cmap);
  //Return index
  return colourMaps.size()-1;
}

void Model::loadWindows()
{
  //Load state from database if available
  sqlite3_stmt* statement = database.select("SELECT name, data from state ORDER BY id");
  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    const char *name = (const char*)sqlite3_column_text(statement, 0);
    const char *data = (const char*)sqlite3_column_text(statement, 1);
    addFigure(name, data);
  }

  if (figures.size() > 0)
  {
    //Load the most recently added (last entry)
    //(so the previous state saved is restored on load)
    loadFigure(figures.size()-1);

    //Load object links (colourmaps)
    for (auto o: objects)
      loadLinks(o);
  }
  else //Old db uses window structure
  {
    //Load windows list from database and insert into models
    statement = database.select("SELECT id,name,width,height,colour,minX,minY,minZ,maxX,maxY,maxZ from window");
    //sqlite3_stmt* statement = model->select("SELECT id,name,width,height,colour,minX,minY,minZ,maxX,maxY,maxZ,properties from window");
    //window (id, name, width, height, colour, minX, minY, minZ, maxX, maxY, maxZ, properties)
    //Single window only supported now, use viewports for multiple
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
      std::string wtitle = std::string((char*)sqlite3_column_text(statement, 1));
      int width = sqlite3_column_int(statement, 2);
      int height = sqlite3_column_int(statement, 3);
      Colour colour;
      colour.value = sqlite3_column_int(statement, 4);
      float min[3], max[3];
      for (int i=0; i<3; i++)
      {
        if (sqlite3_column_type(statement, 5+i) != SQLITE_NULL)
          min[i] = (float)sqlite3_column_double(statement, 5+i);
        else
          min[i] = FLT_MAX;
        if (sqlite3_column_type(statement, 8+i) != SQLITE_NULL)
          max[i] = (float)sqlite3_column_double(statement, 8+i);
        else
          max[i] = -FLT_MAX;
      }

      drawstate.globals["caption"] = wtitle;
      drawstate.globals["resolution"] = {width, height};
      drawstate.globals["min"] = {min[0], min[1], min[2]};
      drawstate.globals["max"] = {max[0], max[1], max[2]};
      //Support legacy colour field
      if (colour.value != 0 && !drawstate.globals.count("colour"))
        drawstate.globals["background"] = colour.toJson();

      //Link the window viewports, objects & colourmaps
      loadLinks();
    }
  }
  sqlite3_finalize(statement);
}

//Load model viewports
void Model::loadViewports()
{
  //Clear existing views
  for(unsigned int i=0; i<views.size(); i++)
    delete views[i];
  views.clear();

  sqlite3_stmt* statement;
  statement = database.select("SELECT id,x,y,near,far FROM viewport ORDER BY y,x");

  //viewport:
  //(id, title, x, y, near, far, translateX,Y,Z, rotateX,Y,Z, scaleX,Y,Z, properties
  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    float x = (float)sqlite3_column_double(statement, 1);
    float y = (float)sqlite3_column_double(statement, 2);
    float nearc = (float)sqlite3_column_double(statement, 3);
    float farc = (float)sqlite3_column_double(statement, 4);

    //Create the view object and add to list
    views.push_back(new View(drawstate, x, y, nearc, farc));
  }
  sqlite3_finalize(statement);

  //Calculate viewport scaling ( x,y width,height )
  for (unsigned int v=0; v<views.size(); v++)
  {
    float nextx = 1.0, nexty = 1.0;
   
    //Find next largest x & y positions
    if (v+1 < views.size() && views[v+1]->x > views[v]->x)
       nextx = views[v+1]->x;

    for (unsigned int vv=v+1; vv<views.size(); vv++)
    {
       if (views[vv]->y > views[v]->y && views[vv]->y < nexty)
          nexty = views[vv]->y;
    }

    views[v]->w = nextx - views[v]->x;
    views[v]->h = nexty - views[v]->y;
    debug_print("-- Viewport %d at %f,%f %f x %f\n", v, views[v]->x, views[v]->y, views[v]->w, views[v]->h);
  }
}

//Load and apply viewport camera settings
void Model::loadViewCamera(int viewport_id)
{
  int adj=0;
  //Load specified viewport and apply camera settings
  sqlite3_stmt* statement = database.select("SELECT aperture,orientation,focalPointX,focalPointY,focalPointZ,translateX,translateY,translateZ,rotateX,rotateY,rotateZ,scaleX,scaleY,scaleZ,properties FROM viewport WHERE id=%d;", viewport_id);
  if (statement == NULL)
  {
    //Old db
    adj = -5;
    statement = database.select("SELECT translateX,translateY,translateZ,rotateX,rotateY,rotateZ,scaleX,scaleY,scaleZ FROM viewport WHERE id=%d;", viewport_id);
  }

  //viewport:
  //(aperture, orientation, focalPointX,Y,Z, translateX,Y,Z, rotateX,Y,Z, scaleX,Y,Z, properties
  if (sqlite3_step(statement) == SQLITE_ROW)
  {
    View* v = views[viewport_id-1];
    float aperture = 45.0;
    int orientation = RIGHT_HANDED;
    if (adj == 0)
    {
      aperture = (float)sqlite3_column_double(statement, 0);
      orientation = sqlite3_column_int(statement, 1);
    }
    float focus[3] = {0,0,0}, rotate[3], translate[3], scale[3] = {1,1,1};
    for (int i=0; i<3; i++)
    {
      //camera
      if (adj == 0)
      {
        if (sqlite3_column_type(statement, 2+i) != SQLITE_NULL)
          focus[i] = (float)sqlite3_column_double(statement, 2+i);
        else
          focus[i] = FLT_MIN;
      }
      translate[i] = (float)sqlite3_column_double(statement, 5+i+adj);
      rotate[i] = (float)sqlite3_column_double(statement, 8+i+adj);
      scale[i] = (float)sqlite3_column_double(statement, 11+i+adj);
    }

    const char *vprops = adj == 0 ? (char*)sqlite3_column_text(statement, 14) : "";

    if (adj == 0) v->focus(focus[0], focus[1], focus[2], aperture, true);
    v->translate(translate[0], translate[1], translate[2]);
    v->rotate(rotate[0], rotate[1], rotate[2]);
    v->setScale(scale[0], scale[1], scale[2]);
    v->properties.parseSet(std::string(vprops));
    v->properties["coordsystem"] = orientation;
  }
  sqlite3_finalize(statement);
}

//Load model objects
void Model::loadObjects()
{
  sqlite3_stmt* statement;
  statement = database.select("SELECT id, name, colour, opacity, properties FROM object", true);
  if (statement == NULL)
    statement = database.select("SELECT id, name, colour, opacity FROM object");

  //object (id, name, colourmap_id, colour, opacity, properties)
  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    int object_id = sqlite3_column_int(statement, 0);
    const char *otitle = (char*)sqlite3_column_text(statement, 1);

    //Create drawing object and add to master list
    std::string props = "";
    if (sqlite3_column_type(statement, 4) != SQLITE_NULL)
      props = std::string((char*)sqlite3_column_text(statement, 4));
    DrawingObject* obj = new DrawingObject(drawstate, otitle, props, object_id);

    //Convert old colour/opacity from hard coded fields if provided
    if (sqlite3_column_type(statement, 2) != SQLITE_NULL)
    {
      Colour cobj;
      cobj.value = sqlite3_column_int(statement, 2);
      if (cobj.value != 0 && !obj->properties.has("colour")) obj->properties.data["colour"] = cobj.toJson();
    }
    if (sqlite3_column_type(statement, 2) != SQLITE_NULL)
    {
      float opacity = (float)sqlite3_column_double(statement, 3);
      if (opacity > 0 && !obj->properties.has("opacity")) obj->properties.data["opacity"] = opacity;
    }

    addObject(obj);
  }
  sqlite3_finalize(statement);
}

//Load viewports in current window, objects in each viewport, colourmaps for each object
void Model::loadLinks()
{
  //Select statment to get all viewports in window and all objects in viewports
  //sprintf(SQL, "SELECT id,title,x,y,near,far,aperture,orientation,focalPointX,focalPointY,focalPointZ,translateX,translateY,translateZ,rotateX,rotateY,rotateZ,scaleX,scaleY,scaleZ,properties FROM viewport WHERE id=%d;", win->id);
  sqlite3_stmt* statement = database.select("SELECT viewport.id,object.id,object.colourmap_id,object_colourmap.colourmap_id,object_colourmap.data_type FROM window_viewport,viewport,viewport_object,object LEFT OUTER JOIN object_colourmap ON object_colourmap.object_id=object.id WHERE viewport_object.viewport_id=viewport.id AND object.id=viewport_object.object_id"); //Don't report errors as these tables are allowed to not exist

  int last_viewport = 0, last_object = 0;
  DrawingObject* draw = NULL;
  View* view = NULL;
  while ( sqlite3_step(statement) == SQLITE_ROW)
  {
    int viewport_id = sqlite3_column_int(statement, 0);
    int object_id = sqlite3_column_int(statement, 1);
    unsigned int colourmap_id = sqlite3_column_int(statement, 3); //Linked colourmap id
    int data_type = sqlite3_column_int(statement, 4); //Colour/Opacity/R/G/B etc (legacy)

    //Fields from object_colourmap
    if (!colourmap_id)
    {
      //Backwards compatibility with old db files
      colourmap_id = sqlite3_column_int(statement, 2);
    }

    //Get viewport
    view = views[viewport_id-1];
    if (last_viewport != viewport_id)
    {
      loadViewCamera(viewport_id);
      last_viewport = viewport_id;
      last_object = 0;  //Reset required, in case of single object which is shared between viewports
    }

    //Get drawing object
    draw = findObject(object_id);
    if (!draw) continue; //No geometry
    if (last_object != object_id)
    {
      view->addObject(draw);
      last_object = object_id;
    }

    //Add colour maps to drawing objects...
    if (colourmap_id)
    {
      if (colourMaps.size() < colourmap_id || !colourMaps[colourmap_id-1])
        abort_program("Invalid colourmap id %d\n", colourmap_id);
      //Find colourmap by id == index
      //Add colourmap to drawing object Colour & Opacity still suported, R/G/B are not
      if (data_type == lucColourValueData)
        draw->properties.data["colourmap"] = colourmap_id-1;
      if (data_type == lucOpacityValueData)
        draw->properties.data["opacitymap"] = colourmap_id-1;
    }
  }
  sqlite3_finalize(statement);
}

//Load colourmaps for each object only
void Model::loadLinks(DrawingObject* obj)
{
  //Only for objects from db files
  if (obj->dbid <= 0) return;

  //Select statment to get all viewports in window and all objects in viewports
  //sprintf(SQL, "SELECT id,title,x,y,near,far,aperture,orientation,focalPointX,focalPointY,focalPointZ,translateX,translateY,translateZ,rotateX,rotateY,rotateZ,scaleX,scaleY,scaleZ,properties FROM viewport WHERE id=%d;", win->id);
  sqlite3_stmt* statement = database.select("SELECT object.id,object.colourmap_id,object_colourmap.colourmap_id,object_colourmap.data_type FROM object LEFT OUTER JOIN object_colourmap ON object_colourmap.object_id=object.id WHERE object.id=%d", obj->dbid);

  while ( sqlite3_step(statement) == SQLITE_ROW)
  {
    unsigned int colourmap_id = sqlite3_column_int(statement, 2); //Linked colourmap id

    //Fields from object_colourmap
    if (!colourmap_id)
    {
      //Backwards compatibility with old db files
      colourmap_id = sqlite3_column_int(statement, 1);
    }

    //Add colour maps to drawing objects...
    if (colourmap_id > 0)
    {
      if (colourMaps.size() < colourmap_id || !colourMaps[colourmap_id-1])
        abort_program("Invalid colourmap id %d\n", colourmap_id);
      //Add colourmap to drawing object by index
      obj->properties.data["colourmap"] = colourmap_id-1;
    }
  }
  sqlite3_finalize(statement);
}

void Model::clearTimeSteps()
{
  for (unsigned int idx=0; idx < timesteps.size(); idx++)
  {
    //Clear the store first to avoid deleting active (double free)
    timesteps[idx]->cache.clear();
    delete timesteps[idx];
  }
  timesteps.clear();
}

int Model::loadTimeSteps(bool scan)
{
  //Strip any digits from end of filename to get base
  std::string base = database.file.base;
  std::string basename = base.substr(0, base.find_last_not_of("0123456789") + 1);

  //Don't reload timesteps when data has been cached
  if (useCache() && timesteps.size() > 0) return timesteps.size();
  clearTimeSteps();
  drawstate.gap = 0;
  int rows = 0;
  int last_step = 0;

  //Scan for additional timestep files with corrosponding entries in timestep table
  if (!scan && database)
  {
    sqlite3_stmt* statement = database.select("SELECT * FROM timestep");
    //(id, time, dim_factor, units)
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
      int step = sqlite3_column_int(statement, 0);
      double time = sqlite3_column_double(statement, 1);
      addTimeStep(step, time);
      //Save gap
      if (step - last_step > drawstate.gap) drawstate.gap = step - last_step;
      last_step = step;

      //Look for additional db file (minimum 3 digit padded step in names)
      std::string path = checkFileStep(step, basename, 3);
      if (path.length() > 0)
      {
        debug_print("Found step %d database %s\n", step, path.c_str());
        timesteps[rows]->path = path;
      }

      rows++;
    }
    sqlite3_finalize(statement);
  }

  //Assume we have at least one current timestep, even if none in table
  if (timesteps.size() == 0) addTimeStep();

  //Check for other timesteps in external files without table data
  //(only run this if explicitly requested with "scan command")
  if (scan) // || timesteps.size() == 1)
  {
    debug_print("Scanning for timesteps...\n");
    for (unsigned int ts=0; ts<10000; ts++)
    {
      //If no steps found after trying 100, give up scanning
      if (timesteps.size() < 2 && ts > 100) break;
      std::string path = checkFileStep(ts, basename);
      if (path.length() > 0)
      {
        debug_print("Found step %d database %s\n", ts, path.c_str());
        if (path == database.file.full && timesteps.size() == 1)
        {
          //Update step if this is the initial db file
          timesteps[0]->step = ts;
          timesteps[0]->path = path;
        }
        else
          addTimeStep(ts, 0.0, path);
      }
    }
    debug_print("Scanning complete, found %d steps.\n", timesteps.size());
  }

  //Copy to static for use in Tracers etc
  if (infostream) std::cerr << timesteps.size() << " timesteps loaded\n";
  drawstate.timesteps = timesteps;
  return timesteps.size();
}

void Model::loadFixed()
{
  //Insert fixed geometry records
  if (fixed.size() > 0) 
    for (unsigned int i=0; i<geometry.size(); i++)
      geometry[i]->insertFixed(fixed[i]);
}

std::string Model::checkFileStep(unsigned int ts, const std::string& basename, unsigned int limit)
{
  unsigned int len = (ts == 0 ? 1 : (int)log10((float)ts) + 1);
  //Lower limit of digits to look for, default 1-5
  if (len < limit) len = limit;
  for (int w = 5; w >= (int)len; w--)
  {
    std::ostringstream ss;
    ss << database.file.path << basename << std::setw(w) << std::setfill('0') << ts;
    ss << "." << database.file.ext;
    std::string path = ss.str();
    if (FileExists(path))
      return path;
  }
  return "";
}

void Model::loadColourMaps()
{
  sqlite3_stmt* statement = database.select("select count(*) from colourvalue");
  if (statement) return loadColourMapsLegacy();

  //New databases have only a colourmap table with colour data in properties
  statement = database.select("SELECT id,name,minimum,maximum,logscale,discrete,properties FROM colourmap");
  double minimum;
  double maximum;
  ColourMap* colourMap = NULL;
  while ( sqlite3_step(statement) == SQLITE_ROW)
  {
    char *cmname = (char*)sqlite3_column_text(statement, 1);
    minimum = sqlite3_column_double(statement, 2);
    maximum = sqlite3_column_double(statement, 3);
    int logscale = sqlite3_column_int(statement, 4);
    int discrete = sqlite3_column_int(statement, 5);
    std::string props;
    if (sqlite3_column_type(statement, 6) != SQLITE_NULL) props = (char*)sqlite3_column_text(statement, 6);
    colourMap = new ColourMap(drawstate, cmname, props);
    setColourMapProps(colourMap->properties, minimum, maximum, logscale, discrete);
    colourMaps.push_back(colourMap);
  }

  sqlite3_finalize(statement);

  //Initial calibration for all maps
  for (unsigned int i=0; i<colourMaps.size(); i++)
    colourMaps[i]->calibrate();
}

void Model::loadColourMapsLegacy()
{
  //Handles old databases with colourvalue table
  bool old = false;
  sqlite3_stmt* statement = database.select("SELECT colourmap.id,minimum,maximum,logscale,discrete,colour,value,name,properties FROM colourmap,colourvalue WHERE colourvalue.colourmap_id=colourmap.id");
  if (statement == NULL)
  {
    //Old DB format, had no name or props
    statement = database.select("SELECT colourmap.id,minimum,maximum,logscale,discrete,colour,value FROM colourmap,colourvalue WHERE colourvalue.colourmap_id=colourmap.id");
    old = true;
  }
  //colourmap (id, minimum, maximum, logscale, discrete, centreValue)
  //colourvalue (id, colourmap_id, colour, position)
  int map_id = 0;
  double minimum;
  double maximum;
  bool parsed = false;
  ColourMap* colourMap = NULL;
  while ( sqlite3_step(statement) == SQLITE_ROW)
  {
    int id = sqlite3_column_int(statement, 0);
    char idname[10];
    sprintf(idname, "%d", id);
    char *cmname = NULL;
    if (!old) cmname = (char*)sqlite3_column_text(statement, 7);

    //New map?
    if (id != map_id)
    {
      map_id = id;
      minimum = sqlite3_column_double(statement, 1);
      maximum = sqlite3_column_double(statement, 2);
      int logscale = sqlite3_column_int(statement, 3);
      int discrete = sqlite3_column_int(statement, 4);
      std::string props;
      if (!old && sqlite3_column_type(statement, 8) != SQLITE_NULL) props = (char*)sqlite3_column_text(statement, 8);
      colourMap = new ColourMap(drawstate, cmname ? cmname : idname, props);
      colourMaps.push_back(colourMap);
      setColourMapProps(colourMap->properties, minimum, maximum, logscale, discrete);
      //Colours already parsed from properties?
      if (colourMap->colours.size() > 0) parsed = true;
      else parsed = false;
    }

    if (!parsed)
    {
      //Add colour value
      int colour = sqlite3_column_int(statement, 5);
      //const char *name = sqlite3_column_name(statement, 7);
      if (sqlite3_column_type(statement, 6) != SQLITE_NULL)
      {
        double value = sqlite3_column_double(statement, 6);
        colourMap->add(colour, value);
      }
      else
        colourMap->add(colour);
    }
    //debug_print("ColourMap: %d min %f, max %f Value %d \n", id, minimum, maximum, colour);
  }

  sqlite3_finalize(statement);

  //Initial calibration for all maps
  for (unsigned int i=0; i<colourMaps.size(); i++)
    colourMaps[i]->calibrate();
}

void Model::setColourMapProps(Properties& properties, float minimum, float maximum, bool logscale, bool discrete)
{
  //These properties used to be database fields, convert
  if (logscale) properties.data["logscale"] = true;
  if (discrete) properties.data["discrete"] = true;

  //Set range if dynamic=0 or minimum/maximum values are not defaults
  if (properties.has("dynamic"))
  {
    //Legacy prop, supports int and bool
    json d = properties["dynamic"];
    bool dynamic = d.is_boolean() ? (bool)d : (int)d != 0;
    if (!dynamic)
      properties.data["range"] = {minimum, maximum};
  }

  float range = maximum - minimum;
  if (!properties.has("range") && range != 0.0 && range != 1.0)
  {
    properties.data["range"] = {minimum, maximum};
  }
}

void Model::freeze()
{
  //Freeze fixed geometry
  fixed = geometry;

  //Need new geometry containers after freeze
  //(Or new data will be appended to the frozen containers!)
  init();
  if (timesteps.size() == 0) addTimeStep();
  loadFixed();
}

bool Model::useCache()
{
  //Use cache if no database loaded, or turned on by global parameter
  if (!database) return true;
  return drawstate.global("cache");
}

void Model::cacheLoad()
{
  for (unsigned int i=0; i<timesteps.size(); i++)
  {
    setTimeStep(i);
    if (drawstate.now != (int)i) break; //All cached in loadGeometry (doesn't work for split db timesteps so still need this loop)
    debug_print("Cached time %d : %d/%d (%s)\n", step(), i+1, timesteps.size(), database.file.base.c_str());
  }
  //Cache final step
  cacheStep();
  //Clear current step to ensure selected is loaded from cache
  drawstate.now = now = -1;
}

void Model::cacheStep()
{
  //Don't cache if we already loaded from cache or out of range!
  if (!useCache() || drawstate.now < 0 || (int)timesteps.size() <= drawstate.now) return;
  if (timesteps[drawstate.now]->cache.size() > 0) return; //Already cached this step

  debug_print("~~~ Caching geometry @ %d (step %d : %s), geom memory usage: %.3f mb\n", step(), drawstate.now, database.file.base.c_str(), membytes__/1000000.0f);

  //Copy all elements
  if (membytes__ > 0)
  {
    clearStep();
    timesteps[drawstate.now]->write(geometry);
    debug_print("~~~ Cached step, at: %d\n", step());
    printf(".");
    fflush(stdout);
    //Objects have been moved into cache, clear from active list
    geometry.clear();
  }
  else
    debug_print("~~~ Nothing to cache\n");

  //TODO: fix support for partial caching?
  /*/Remove if over limit
  if (cache.size() > GeomCache::size)
  {
     GeomCache* cached = cache.front();
     cache.pop_front();
     //Clear containers...
     for (unsigned int i=0; i < cached->store.size(); i++)
     {
        cached->store[i]->clear(true);
        delete cached->store[i];
     }
     debug_print("~~~ Deleted oldest cached step (%d)\n", cached->step);
     delete cached;
  }*/
}

bool Model::restoreStep()
{
  if (drawstate.now < 0 || !useCache()) return false;
  if (timesteps[drawstate.now]->cache.size() == 0)
    return false; //Nothing cached this step

  //Load the cache and save loaded timestep
  clearStep();
  timesteps[drawstate.now]->read(geometry);
  debug_print("~~~ Cache hit at ts %d (idx %d), loading! %s\n", step(), drawstate.now, database.file.base.c_str());

  //Switch geometry containers
  labels = geometry[lucLabelType];
  points = (Points*)geometry[lucPointType];
  vectors = (Vectors*)geometry[lucVectorType];
  tracers = (Tracers*)geometry[lucTracerType];
  quadSurfaces = (QuadSurfaces*)geometry[lucGridType];
  volumes = (Volumes*)geometry[lucVolumeType];
  triSurfaces = (TriSurfaces*)geometry[lucTriangleType];
  lines = geometry[lucLineType];
  shapes = (Shapes*)geometry[lucShapeType];

  debug_print("~~~ Geom memory usage after load: %.3f mb\n", membytes__/1000000.0f);
  //Redraw display
  redraw();
  return true;
}

void Model::clearStep()
{
  //Clear and tell all geometry objects they need to reload data
  for (unsigned int i=0; i < geometry.size(); i++)
  {
    //Release any graphics memory and clear
    geometry[i]->close();
  }
}

void Model::printCache()
{
  debug_print("-----------CACHE %d steps\n", timesteps.size());
  for (unsigned int idx=0; idx < timesteps.size(); idx++)
    debug_print(" %d: has %d records\n", idx, timesteps[idx]->cache.size());
}

//Set time step if available, otherwise return false and leave unchanged
bool Model::hasTimeStep(int ts)
{
  if (timesteps.size() == 0 && loadTimeSteps() == 0) return false;
  for (unsigned int idx=0; idx < timesteps.size(); idx++)
    if (ts == timesteps[idx]->step)
      return true;
  return false;
}

int Model::nearestTimeStep(int requested)
{
  //Find closest matching timestep to requested, returns index
  int idx;
  if (timesteps.size() == 0 && loadTimeSteps() == 0) return -1;
  //if (loadTimeSteps() == 0 || timesteps.size() == 0) return -1;
  //if (timesteps.size() == 1 && drawstate.now >= 0 && ) return -1;  //Single timestep

  for (idx=0; idx < (int)timesteps.size(); idx++)
    if (timesteps[idx]->step >= requested) break;

  //Reached end of list?
  if (idx == (int)timesteps.size()) idx--;

  if (idx < 0) idx = 0;
  if (idx >= (int)timesteps.size()) idx = timesteps.size() - 1;

  return idx;
}

//Load data at specified timestep
int Model::setTimeStep(int stepidx)
{
  int rows = 0;
  clock_t t1 = clock();

  //Default timestep only? Skip load
  if (timesteps.size() == 0)
  {
    drawstate.now = now = -1;
    return -1;
  }

  if (stepidx < 0) stepidx = 0; //return -1;
  if (stepidx >= (int)timesteps.size())
    stepidx = timesteps.size()-1;

  //Unchanged...
  if (now >= 0 && stepidx == now && drawstate.now == now) return -1;

  //Setting initial step?
  bool first = (now < 0);

  //Cache currently loaded data
  if (useCache()) cacheStep();

  //Set the new timestep index
  debug_print("===== Model step %d Global step %d Requested step %d =====\n", now, drawstate.now, stepidx);
  drawstate.timesteps = timesteps; //Set to current model timestep vector
  drawstate.now = now = stepidx;
  debug_print("TimeStep set to: %d (%d)\n", step(), stepidx);

  if (!restoreStep())
  {
    //Create new geometry containers if required
    if (geometry.size() == 0) init();

    if (first)
      //Freeze any existing geometry as non time-varying when first step loaded
      freeze();
    else
      //Normally just clear any existing geometry
      clearObjects();

    //Import fixed data first
    if (drawstate.now >= 0) 
      loadFixed();

    //Attempt to load from cache first
    //if (restoreStep(state.now)) return 0; //Cache hit successful return value
    if (database)
    {
      //Detach any attached db file and attach n'th timestep database if available
      database.attach(timesteps[drawstate.now]);

      if (useCache())
        //Attempt caching all geometry from database at start
        rows += loadGeometry(0, 0, timesteps[timesteps.size()-1]->step, true);
      else
        rows += loadGeometry();

      debug_print("%.4lf seconds to load %d geometry records from database\n", (clock()-t1)/(double)CLOCKS_PER_SEC, rows);
    }
  }

  return rows;
}

int Model::loadGeometry(int obj_id, int time_start, int time_stop, bool recurseTracers)
{
  if (!database)
  {
    std::cerr << "No database loaded!!\n";
    return 0;
  }
  clock_t t1 = clock();

  //Default to current timestep
  if (time_start < 0) time_start = step();
  if (time_stop < 0) time_stop = step();

  //Load geometry
  char filter[256] = {'\0'};
  char objfilter[32] = {'\0'};

  //Setup filters, object...
  if (obj_id > 0)
  {
    sprintf(objfilter, "WHERE object_id=%d", obj_id);
    //Remove the skip flag now we have explicitly loaded object
    DrawingObject* obj = findObject(obj_id);

    if (obj) obj->skip = false;
  }

  //...timestep...(if ts db attached, assume all geometry is at current step)
  if (time_start >= 0 && time_stop >= 0 && !database.attached)
  {
    if (strlen(objfilter) > 0)
      sprintf(filter, "%s AND timestep BETWEEN %d AND %d", objfilter, time_start, time_stop);
    else
      sprintf(filter, " WHERE timestep BETWEEN %d AND %d", time_start, time_stop);
  }
  else
    strcpy(filter, objfilter);

  int datacol = 21;
  //object (id, name, colourmap_id, colour, opacity, wireframe, cullface, scaling, lineWidth, arrowHead, flat, steps, time)
  //geometry (id, object_id, timestep, rank, idx, type, data_type, size, count, width, minimum, maximum, dim_factor, units, labels,
  //minX, minY, minZ, maxX, maxY, maxZ, data)
  sqlite3_stmt* statement = database.select("SELECT id,object_id,timestep,rank,idx,type,data_type,size,count,width,minimum,maximum,dim_factor,units,labels,minX,minY,minZ,maxX,maxY,maxZ,data FROM %sgeometry %s ORDER BY timestep,object_id", database.prefix, filter);

  //Old database compatibility
  if (statement == NULL)
  {
    //object (id, name, colourmap_id, colour, opacity, wireframe, cullface, scaling, lineWidth, arrowHead, flat, steps, time)
    //geometry (id, object_id, timestep, rank, idx, type, data_type, size, count, width, minimum, maximum, dim_factor, units, data)
    statement = database.select("SELECT id,object_id,timestep,rank,idx,type,data_type,size,count,width,minimum,maximum,dim_factor,units,labels,data FROM %sgeometry %s ORDER BY timestep,object_id", database.prefix, filter);
    datacol = 15;

    //Fix
#ifdef ALTER_DB
    database.reopen(true);  //Open writable
    database.issue("ALTER TABLE %sgeometry ADD COLUMN minX REAL; ALTER TABLE %sgeometry ADD COLUMN minY REAL; ALTER TABLE %sgeometry ADD COLUMN minZ REAL; "
            "ALTER TABLE %sgeometry ADD COLUMN maxX REAL; ALTER TABLE %sgeometry ADD COLUMN maxY REAL; ALTER TABLE %sgeometry ADD COLUMN maxZ REAL; ",
            database.prefix, database.prefix, database.prefix, database.prefix, database.prefix, database.prefix, database.prefix);
#endif
  }

  //Very old database compatibility
  if (statement == NULL)
  {
    statement = database.select("SELECT id,object_id,timestep,rank,idx,type,data_type,size,count,width,minimum,maximum,dim_factor,units,data FROM %sgeometry %s ORDER BY timestep,object_id", database.prefix, filter);
    datacol = 14;
  }

  if (!statement) return 0;
  int rows = 0;
  int tbytes = 0;
  int ret;
  Geometry* active = NULL;
  do
  {
    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW)
    {
      rows++;
      int object_id = sqlite3_column_int(statement, 1);
      int timestep = sqlite3_column_int(statement, 2);
      int height = sqlite3_column_int(statement, 3);  //unused - was rank, now height
      int depth = sqlite3_column_int(statement, 4); //unused - was idx, now depth
      lucGeometryType type = (lucGeometryType)sqlite3_column_int(statement, 5);
      lucGeometryDataType data_type = (lucGeometryDataType)sqlite3_column_int(statement, 6);
      int size = sqlite3_column_int(statement, 7);
      int count = sqlite3_column_int(statement, 8);
      int items = count / size;
      int width = sqlite3_column_int(statement, 9);
      if (height == 0) height = width > 0 ? items / width : 0;
      //TODO: these should be used for value data if set!
      //float minimum = (float)sqlite3_column_double(statement, 10);
      //float maximum = (float)sqlite3_column_double(statement, 11);
      //Units field repurposed for data label
      const char *data_label = (const char*)sqlite3_column_text(statement, 13);
      const char *labels = datacol < 15 ? "" : (const char*)sqlite3_column_text(statement, 14);

      //printf("%d] OBJ %d STEP %d TYPE %d DTYPE %d DIMS (%d x %d x %d) COUNT %d ITEMS %d LABELS %s\n", 
      //       rows, object_id, timestep, type, data_type, width, height, depth, count, items, labels);

      const void *data = sqlite3_column_blob(statement, datacol);
      unsigned int bytes = sqlite3_column_bytes(statement, datacol);

      DrawingObject* obj = findObject(object_id);

      //Deleted or Skip object? (When noload enabled)
      if (!obj || obj->skip) continue;

      //Bulk load: switch timestep and cache if timestep changes!
      // - disabled when loading multiple tracer steps (recursive load)
      // - disabled when using attached databases (cached in loop via cacheLoad())
      if (recurseTracers && step() != timestep && !database.attached)
      {
        cacheStep();
        drawstate.now = now = nearestTimeStep(timestep);
        debug_print("TimeStep set to: %d, rows %d\n", step(), rows);
      }

      //Create new geometry containers if required
      if (geometry.size() == 0) init();

      if (type == lucTracerType)
      {
        height = 0;
        //Default particle count:
        if (width == 0) width = items;
      }

      //Create object and set parameters
      if (type == lucPointType && drawstate.global("pointspheres")) type = lucShapeType;
      /* Convert grid to tris
       * - need to skip index/normal data as it is setup for tri strips
      if (type == lucGridType) {
        type = lucTriangleType;
        if (data_type == lucIndexData || data_type == lucNormalData) continue;
      }*/
      active = geometry[type];

      if (recurseTracers && type == lucTracerType)
      {
        //if (datacol == 13) continue;  //Don't bother supporting tracers from old dbs
        //Only load tracer timesteps when on the vertex data object or will repeat for every type found
        if (data_type != lucVertexData) continue;

        //Tracers are loaded with a new select statement across multiple timesteps...
        //objects[object_id]->steps = timestep+1;
        //Tracers* tracers = (Tracers*)active;
        int stepstart = 0; //timestep - objects[object_id]->steps;
        //int stepstart = timestep - tracers->steps;

        loadGeometry(object_id, stepstart, timestep, false);
      }
      else
      {
        unsigned char* buffer = NULL;
        if (bytes != (unsigned int)(count * GeomData::byteSize(data_type)))
        {
          //Decompress!
          unsigned long dst_len = (unsigned long)(count * GeomData::byteSize(data_type));
          unsigned long uncomp_len = dst_len;
          unsigned long cmp_len = bytes;
          buffer = new unsigned char[dst_len];
          if (!buffer)
            abort_program("Out of memory!\n");

#ifdef USE_ZLIB
          int res = uncompress(buffer, &uncomp_len, (const unsigned char *)data, cmp_len);
          if (res != Z_OK || dst_len != uncomp_len)
#else
          int res = tinfl_decompress_mem_to_mem(buffer, uncomp_len, (const unsigned char *)data, cmp_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
          if (!res)
#endif
          {
            abort_program("uncompress() failed! error code %d\n", res);
            //abort_program("uncompress() failed! error code %d expected size %d actual size %d\n", res, dst_len, uncomp_len);
          }
          data = buffer; //Replace data pointer
          bytes = uncomp_len;
        }

        tbytes += bytes;   //Byte counter

        //Always add a new element for each new vertex geometry record, not suitable if writing db on multiple procs!
        if (data_type == lucVertexData && recurseTracers) active->add(obj);

        //Read data block
        GeomData* g;
        //Convert legacy value types to use data labels
        switch (data_type)
        {
          case lucColourValueData:
          case lucOpacityValueData:
          case lucRedValueData:
          case lucGreenValueData:
          case lucBlueValueData:
          case lucXWidthData:
          case lucYHeightData:
          case lucZLengthData:
          case lucSizeData:
          case lucMaxDataType:
            if (data_label && strlen(data_label) > 0)
              //Use provided label from units field
              g = active->read(obj, items, data, data_label);
            else //Use default/legacy label
              g = active->read(obj, items, data, GeomData::datalabels[data_type]);
            break;

          default:
            //Non-value data
            g = active->read(obj, items, data_type, data, width, height, depth);
        }

        //Set geom labels if any
        if (labels) active->label(obj, labels);

        //Where min/max vertex provided, load
        if (data_type == lucVertexData && type != lucLabelType)
        {
          float min[3] = {0,0,0}, max[3] = {0,0,0};
          if (datacol > 15 && sqlite3_column_type(statement, 15) != SQLITE_NULL)
          {
            for (int i=0; i<3; i++)
            {
              min[i] = (float)sqlite3_column_double(statement, 15+i);
              max[i] = (float)sqlite3_column_double(statement, 18+i);
            }
          }

          //Apply dims if provided
          if (min[0] != max[0] || min[1] != max[1] || min[2] != max[2])
          {
            g->checkPointMinMax(min);
            g->checkPointMinMax(max);
          }
        }

        if (buffer) delete[] buffer;
      }
    }
    else if (ret != SQLITE_DONE)
      fprintf(stderr, "Database file problem, sqlite_step returned: %d (%d)\n", ret, (ret>>8));
  }
  while (ret == SQLITE_ROW);

  sqlite3_finalize(statement);
  debug_print("... loaded %d rows, %d bytes, %.4lf seconds\n", rows, tbytes, (clock()-t1)/(double)CLOCKS_PER_SEC);

  return rows;
}

void Model::mergeDatabases()
{
  if (!database) return;
  database.reopen(true);  //Open writable
  for (unsigned int i=0; i<=timesteps.size(); i++)
  {
    debug_print("MERGE %d/%d...%d\n", i, timesteps.size(), step());
    setTimeStep(i);
    if (database.attached->step == step())
    {
      database.issue("insert into geometry select null, object_id, timestep, rank, idx, type, data_type, size, count, width, minimum, maximum, dim_factor, units, labels, properties, data, minX, minY, minZ, maxX, maxY, maxZ from %sgeometry", database.prefix);
    }
  }
}

void Model::updateObject(DrawingObject* target, lucGeometryType type, bool compress)
{
  database.reopen(true); //Ensure opened writable
  database.issue("BEGIN EXCLUSIVE TRANSACTION");
  if (type == lucMaxType)
    writeObjects(database, target, step(), compress);
  else
    writeGeometry(database, type, target, step(), compress);

  //Update object
  database.issue("update object set properties = '%s' where name = '%s'", target->properties.data.dump().c_str(), target->name().c_str());

  database.issue("COMMIT");
}

void Model::writeDatabase(const char* path, DrawingObject* obj, bool compress)
{
  //Write objects to a new database?
  Database outdb;
  if (path)
  {
    outdb = Database(FilePath(path));
    if (!outdb.open(true))
    {
      printf("Database write failed '%s': %s\n", path, sqlite3_errmsg(outdb.db));
      return;
    }
  }
  else
  {
    outdb = database;
    database.reopen(true);  //Open writable
  }

  // Remove existing static data
  outdb.issue("drop table IF EXISTS object");
  outdb.issue("drop table IF EXISTS state");

  // Create new tables when not present
  outdb.issue("create table IF NOT EXISTS geometry (id INTEGER PRIMARY KEY ASC, object_id INTEGER, timestep INTEGER, rank INTEGER, idx INTEGER, type INTEGER, data_type INTEGER, size INTEGER, count INTEGER, width INTEGER, minimum REAL, maximum REAL, dim_factor REAL, units VARCHAR(32), minX REAL, minY REAL, minZ REAL, maxX REAL, maxY REAL, maxZ REAL, labels VARCHAR(2048), properties VARCHAR(2048), data BLOB, FOREIGN KEY (object_id) REFERENCES object (id) ON DELETE CASCADE ON UPDATE CASCADE, FOREIGN KEY (timestep) REFERENCES timestep (id) ON DELETE CASCADE ON UPDATE CASCADE)");

  outdb.issue(
    "create table IF NOT EXISTS timestep (id INTEGER PRIMARY KEY ASC, time REAL, dim_factor REAL, units VARCHAR(32), properties VARCHAR(2048))");

  outdb.issue(
    "create table object (id INTEGER PRIMARY KEY ASC, name VARCHAR(256), colourmap_id INTEGER, colour INTEGER, opacity REAL, properties VARCHAR(2048))");

  //Write state
  writeState(outdb);

  outdb.issue("BEGIN EXCLUSIVE TRANSACTION");

  //Write objects
  for (unsigned int i=0; i < objects.size(); i++)
  {
    if (!obj || objects[i] == obj)
    {
      if (!outdb.issue("insert into object (name, properties) values ('%s', '%s')", objects[i]->name().c_str(), objects[i]->properties.data.dump().c_str()))
        return;
      //Store the id
      objects[i]->dbid = sqlite3_last_insert_rowid(outdb.db);
    }
  }

  //Write timesteps & objects...
  //(Only write timestep where doesn't exist already)
  if (timesteps.size() == 0)
  {
    //Create a default timestep
    outdb.issue("delete from timestep where id == '0'");
    outdb.issue("insert into timestep (id, time) values (0, 0);");
    writeObjects(outdb, obj, 0, compress);
  }

  for (unsigned int i = 0; i < timesteps.size(); i++)
  {
    outdb.issue("delete from timestep where id == '%d'", i);
    outdb.issue("insert into timestep (id, time, properties) values (%d, %g, '%s');", 
                timesteps[i]->step, timesteps[i]->time, "", timesteps[i]->step);

    //Get data at this timestep
    setTimeStep(i);

    //Write object data
    writeObjects(outdb, obj, step(), compress);
  }

  outdb.issue("COMMIT");
}

void Model::writeState()
{
  writeState(database);
}

void Model::writeState(Database& outdb)
{
  //Save changes to the active state first
  storeFigure();

  //Write state
  outdb.issue("create table if not exists state (id INTEGER PRIMARY KEY ASC, name VARCHAR(256), data TEXT)");

  char SQL[SQL_QUERY_MAX];
  for (unsigned int f=0; f<fignames.size(); f++)
  {
    // Delete any state entry with same name
    outdb.issue("delete from state where name == '%s'", fignames[f].c_str());

    snprintf(SQL, SQL_QUERY_MAX, "insert into state (name, data) values ('%s', ?)", fignames[f].c_str());
    sqlite3_stmt* statement;

    if (sqlite3_prepare_v2(outdb.db, SQL, -1, &statement, NULL) != SQLITE_OK)
      abort_program("SQL prepare error: (%s) %s\n", SQL, sqlite3_errmsg(outdb.db));

    if (sqlite3_bind_text(statement, 1, figures[f].c_str(), figures[f].length(), SQLITE_STATIC) != SQLITE_OK)
      abort_program("SQL bind error: %s\n", sqlite3_errmsg(outdb.db));

    if (sqlite3_step(statement) != SQLITE_DONE )
      abort_program("SQL step error: (%s) %s\n", SQL, sqlite3_errmsg(outdb.db));

    sqlite3_finalize(statement);
  }
}

void Model::writeObjects(Database& outdb, DrawingObject* obj, int step, bool compress)
{
  //Write object data
  for (unsigned int i=0; i < objects.size(); i++)
  {
    if (!obj || obj == objects[i])
    {
      //Loop through all geometry classes (points/vectors etc)
      for (int type=lucMinType; type<lucMaxType; type++)
      {
        writeGeometry(outdb, (lucGeometryType)type, objects[i], step, compress);
      }
    }
  }
}

void Model::deleteGeometry(Database& outdb, lucGeometryType type, DrawingObject* obj, int step)
{
  //Clear existing data of this type before writing, allows object update to db
  outdb.issue("DELETE FROM geometry WHERE object_id=%d and type=%d and timestep=%d;", obj->dbid, type, step);
}

void Model::writeGeometry(Database& outdb, lucGeometryType type, DrawingObject* obj, int step, bool compressdata)
{
  //Clear existing data of this type before writing, allows object data updates to db
  deleteGeometry(outdb, type, obj, step);

  std::vector<GeomData*> data = geometry[type]->getAllObjects(obj);
  //Loop through and write out all object data
  unsigned int data_type;
  for (unsigned int i=0; i<data.size(); i++)
  {
    for (data_type=0; data_type < data[i]->data.size(); data_type++)
    {
      //Write the data entry
      DataContainer* block = data[i]->data[data_type];
      if (!block || block->size() == 0) continue;
      if (infostream)
        std::cerr << "Writing geometry (type[" << data_type << "] * " << block->size()
                  << ") for object : " << obj->dbid << " => " << obj->name() << ", compress: " << compressdata << std::endl;
      writeGeometryRecord(outdb, type, (lucGeometryDataType)data_type, obj->dbid, data[i], block, step, compressdata);
    }
    for (unsigned int j=0; j<data[i]->values.size(); j++)
    {
      //Write the value data entry
      DataContainer* block = (DataContainer*)data[i]->values[j];
      if (!block || block->size() == 0) continue;
      if (infostream)
        std::cerr << "Writing geometry (values[" << j << "] * " << block->size()
                  << ") for object : " << obj->dbid << " => " << obj->name() << ", compress: " << compressdata << std::endl;
      //TODO: fix to write/read labels for data values from database, preferably in a separate table?
      //This hack will work for up to 7 value data sets for now
      //Filters and colourby properties will need modification though
      data_type = lucColourValueData+j;
      if (data_type == lucIndexData) data_type++;
      writeGeometryRecord(outdb, type, (lucGeometryDataType)data_type, obj->dbid, data[i], block, step, compressdata);
    }
  }
}

void Model::writeGeometryRecord(Database& outdb, lucGeometryType type, lucGeometryDataType dtype, unsigned int objid, GeomData* data, DataContainer* block, int step, bool compressdata)
{
  char SQL[SQL_QUERY_MAX];
  sqlite3_stmt* statement;
  unsigned char* buffer = (unsigned char*)block->ref(0);
  unsigned long src_len = block->bytes();
  // Compress the data if enabled and > 1kb
  unsigned long cmp_len = 0;
  if (compressdata &&  src_len > 1000)
  {
    cmp_len = compressBound(src_len);
    buffer = (unsigned char*)malloc((size_t)cmp_len);
    if (buffer == NULL)
      abort_program("Compress database: out of memory!\n");
    if (compress(buffer, &cmp_len, (const unsigned char *)block->ref(0), src_len) != Z_OK)
      abort_program("Compress database buffer failed!\n");
    if (cmp_len >= src_len)
    {
      free(buffer);
      buffer = (unsigned char*)block->ref(0);
      cmp_len = 0;
    }
    else
      src_len = cmp_len;
  }

  if (block->minimum == HUGE_VAL) block->minimum = 0;
  if (block->maximum == -HUGE_VAL) block->maximum = 0;

  float min[3], max[3];
  for (int c=0; c<3; c++)
  {
    min[c] = data->min[c];
    if (!ISFINITE(min[c])) min[c] = drawstate.min[c];
    if (!ISFINITE(min[c])) min[c] = 0.0;
    max[c] = data->max[c];
    if (!ISFINITE(max[c])) max[c] = drawstate.max[c];
    if (!ISFINITE(max[c])) max[c] = 0.0;
  }

  snprintf(SQL, SQL_QUERY_MAX, "insert into geometry (object_id, timestep, rank, idx, type, data_type, size, count, width, minimum, maximum, dim_factor, units, minX, minY, minZ, maxX, maxY, maxZ, labels, data) values (%d, %d, %d, %d, %d, %d, %d, %d, %d, %g, %g, %g, '%s', %g, %g, %g, %g, %g, %g, ?, ?)", objid, step, data->height, data->depth, type, dtype, block->unitsize(), block->size(), data->width, block->minimum, block->maximum, 0.0, block->label.c_str(), min[0], min[1], min[2], max[0], max[1], max[2]);

  /* Prepare statement... */
  if (sqlite3_prepare_v2(outdb.db, SQL, -1, &statement, NULL) != SQLITE_OK)
  {
    abort_program("SQL prepare error: (%s) %s\n", SQL, sqlite3_errmsg(outdb.db));
  }

  /* Setup text data for insert (on vertex block only) */
  std::string labels = data->getLabels().c_str();
  if (dtype == lucVertexData && labels.length() > 0)
  {
    if (sqlite3_bind_text(statement, 1, labels.c_str(), labels.length(), SQLITE_STATIC) != SQLITE_OK)
    //const char* clabels = labels.c_str();
    //if (sqlite3_bind_text(statement, 1, clabels, strlen(clabels), SQLITE_STATIC) != SQLITE_OK)
      abort_program("SQL bind error: %s\n", sqlite3_errmsg(outdb.db));
  }

  /* Setup blob data for insert */
  debug_print("Writing %lu bytes\n", src_len);
  if (sqlite3_bind_blob(statement, 2, buffer, src_len, SQLITE_STATIC) != SQLITE_OK)
    abort_program("SQL bind error: %s\n", sqlite3_errmsg(outdb.db));

  /* Execute statement */
  if (sqlite3_step(statement) != SQLITE_DONE )
    abort_program("SQL step error: (%s) %s\n", SQL, sqlite3_errmsg(outdb.db));

  sqlite3_finalize(statement);

  // Free compression buffer
  if (cmp_len > 0) free(buffer);

  //printf("WROTE ID %d STEP %d TYPE %d DTYPE %d DIMS (%d x %d x %d) COUNT %d LABELS %s\n", 
  //       objid, step, type, dtype, data->width, data->height, data->depth, block->size(), labels.c_str());
}

void Model::deleteObject(unsigned int id)
{
  if (!database) return;
  database.reopen(true);  //Open writable
  database.issue("DELETE FROM object WHERE id==%1$d; DELETE FROM geometry WHERE object_id=%1$d; DELETE FROM viewport_object WHERE object_id=%1$d;", id);
  database.issue("vacuum");
  //Update state
  writeState();
}

void Model::backup(Database& fromdb, Database& todb)
{
  if (!fromdb || !todb) return;
  sqlite3_backup *pBackup;  // Backup object used to copy data
  pBackup = sqlite3_backup_init(todb.db, "main", fromdb.db, "main");
  if (pBackup)
  {
    (void)sqlite3_backup_step(pBackup, -1);
    (void)sqlite3_backup_finish(pBackup);
  }
}

void Model::objectBounds(DrawingObject* draw, float* min, float* max)
{
  if (!min || !max) return;
  for (int i=0; i<3; i++)
    max[i] = -(min[i] = HUGE_VAL);
  //Expand bounds by all geometry objects
  for (unsigned int i=0; i < geometry.size(); i++)
    geometry[i]->objectBounds(draw, min, max);
}

std::string Model::jsonWrite(bool objdata)
{
  std::ostringstream json;
  jsonWrite(json, NULL, objdata);
  return json.str();
}

void Model::jsonWrite(std::ostream& os, DrawingObject* o, bool objdata)
{
  //Write new JSON format objects
  // - globals are all stored on / sourced from drawstate.globals
  // - views[] list holds view properies (previously single instance in "options")
  std::lock_guard<std::mutex> guard(drawstate.mutex);
  json exported;
  json properties = drawstate.globals;
  json cmaps = json::array();
  json outobjects = json::array();
  json outviews = json::array();

  //Converts named colours to js readable
  if (properties.count("background") > 0)
    properties["background"] = Colour(properties["background"]).toString();

  for (unsigned int v=0; v < views.size(); v++)
  {
    View* view = views[v];
    json& vprops = view->properties.data;

    float rotate[4], translate[3], focus[3];
    view->getCamera(rotate, translate, focus);
    json rot, trans, foc, scale, min, max;
    for (int i=0; i<4; i++)
    {
      rot.push_back(rotate[i]);
      if (i>2) break;
      trans.push_back(translate[i]);
      foc.push_back(focus[i]);
      scale.push_back(view->scale[i]);
      min.push_back(view->min[i]);
      max.push_back(view->max[i]);
    }

    vprops["rotate"] = rot;
    vprops["translate"] = trans;
    vprops["focus"] = foc;
    vprops["scale"] = scale;
    //Can't set min/max properties from auto calc or will override future bounding box calc,
    //useful to get the calculated bounding box, so export as "bounds"
    json bounds;
    bounds["min"] = min;
    bounds["max"] = max;
    vprops["bounds"] = bounds;

    //Converts named colours to js readable
    if (vprops.count("background") > 0)
      vprops["background"] = Colour(vprops["background"]).toString();

    //Add the view
    outviews.push_back(vprops);
  }

  for (unsigned int i = 0; i < colourMaps.size(); i++)
  {
    json cmap = colourMaps[i]->properties.data;
    json colours;

    if (!colourMaps[i]->calibrated)
      colourMaps[i]->calibrate();

    for (unsigned int c=0; c < colourMaps[i]->colours.size(); c++)
    {
      json colour;
      colour["position"] = colourMaps[i]->colours[c].position;
      colour["colour"] = colourMaps[i]->colours[c].colour.toString();
      colours.push_back(colour);
    }

    cmap["name"] = colourMaps[i]->name;
    cmap["colours"] = colours;

    cmaps.push_back(cmap);
  }

  //if (!viewer->visible) aview->filtered = false; //Disable viewport filtering
  for (unsigned int i=0; i < objects.size(); i++)
  {
    if (!o || objects[i] == o)
    {
      //Only able to dump point/triangle based objects currently:
      //TODO: fix to use sub-renderer output for others
      //"Labels", "Points", "Grid", "Triangles", "Vectors", "Tracers", "Lines", "Shapes", "Volumes"

      json obj = objects[i]->properties.data;

      //Include the object bounding box for WebGL
      float min[3], max[3];
      objectBounds(objects[i], min, max);
      if (min[0] < max[0] && min[1] < max[1])
      {
        obj["min"] = {min[0], min[1], min[2]};
        obj["max"] = {max[0], max[1], max[2]};
      }

      //TODO: export geom textures
      //Texture ? Export first only, as external file for now
      //TODO: dataurl using getImageUrlString(image, iw, ih, channels)
      //if (objects[i]->textures.size() > 0 && obj.count("texturefile") == 0)
      //  obj["texturefile"] = objects[i]->textures[0]->fn.full;

      if (!objdata)
      {
        if (points->getVertexCount(objects[i]) > 0) obj["points"] = true;
        if (quadSurfaces->getVertexCount(objects[i]) > 0 ||
            triSurfaces->getVertexCount(objects[i]) > 0 ||
            vectors->getVertexCount(objects[i]) > 0 ||
            tracers->getVertexCount(objects[i]) > 0 ||
            shapes->getVertexCount(objects[i]) > 0) obj["triangles"] = true;
        if (lines->getVertexCount(objects[i]) > 0) obj["lines"] = true;
        if (volumes->getVertexCount(objects[i]) > 0) obj["volume"] = true;

        //Data labels
        json dict;
        for (unsigned int j=0; j < geometry.size(); j++)
        {
          json list = geometry[j]->getDataLabels(objects[i]);
          std::string key;
          for (auto dataobj : list)
          {
            key = dataobj["label"];
            dict[key] = dataobj;
          }
        }

        if (dict.size() > 0)
          obj["data"] = dict;

        //std::cout << "HAS OBJ TYPES: (point,tri,vol)" << obj.count("points") << "," 
        //          << obj.count("triangles") << "," << obj.count("volume") << std::endl;
        outobjects.push_back(obj);
        continue;
      }

      for (int type=lucMinType; type<lucMaxType; type++)
      {
        //Collect vertex/normal/index/value data
        //When extracting data, skip objects with no data returned...
        //if (!geometry[type]) continue;
        geometry[type]->jsonWrite(objects[i], obj);
      }

      //Save object if contains data
      if (obj["points"].size() > 0 ||
          obj["triangles"].size() > 0 ||
          obj["lines"].size() > 0 ||
          obj["volume"].size() > 0)
      {

        //Converts named colours to js readable
        if (obj.count("colour") > 0)
          obj["colour"] = Colour(obj["colour"]).toString();

        outobjects.push_back(obj);
      }
    }
  }

  exported["properties"] = properties;
  exported["views"] = outviews;
  exported["colourmaps"] = cmaps;
  exported["objects"] = outobjects;
  //Should not set this unless data changed for webgl?
  //exported["reload"] = true;
  exported["reload"] = objdata;
  if (figure >= 0 && figure < (int)fignames.size())
    exported["figure"] = fignames[figure];

  //Export with indentation
  os << std::setw(2) << exported;
}

void Model::jsonRead(std::string data)
{
  std::lock_guard<std::mutex> guard(drawstate.mutex);
  
  json imported = json::parse(data);

  //Load globals, merge with existing values
  Properties::mergeJSON(drawstate.globals, imported["properties"]);

  json inviews;
  //If "options" exists (old format) read it as first view properties
  if (imported.count("options") > 0)
    inviews.push_back(imported["options"]);
  else
    inviews = imported["views"];

  // Import views
  for (unsigned int v=0; v < inviews.size(); v++)
  {
    if (v >= views.size())
    {
      //Insert a view
      View* view = new View(drawstate);
      views.push_back(view);
      //Insert all objects for now
      view->objects = objects;
    }

    View* view = views[v];

    //Apply base properties with merge
    view->properties.merge(inviews[v]);

    //TODO: Fix view to use all these properties directly
    json rot, trans, foc, scale, min, max;
    //Skip import cam if not provided
    if (inviews[v].count("rotate") > 0)
    {
      rot = view->properties["rotate"];
      if (rot.size() == 4)
        view->setRotation(rot[0], rot[1], rot[2], rot[3]);
      else if (rot.size() == 3)
        view->rotate(rot[0], rot[1], rot[2]);
    }
    else
      view->setRotation(0, 0, 0, 1);

    if (inviews[v].count("translate") > 0)
    {
      trans = view->properties["translate"];
      view->setTranslation(trans[0], trans[1], trans[2]);
    }
    if (inviews[v].count("focus") > 0)
    {
      foc = view->properties["focus"];
      view->focus(foc[0], foc[1], foc[2]);
    }
    if (inviews[v].count("scale") > 0)
    {
      scale = view->properties["scale"];
      view->scale[0] = scale[0];
      view->scale[1] = scale[1];
      view->scale[2] = scale[2];
    }
    //min = aview->properties["min"];
    //max = aview->properties["max"];
    //view->init(false, newmin, newmax);
    view->setBackground(); //Update background colour
  }

  // Import colourmaps
  json cmaps = imported["colourmaps"];
  for (unsigned int i=0; i < cmaps.size(); i++)
  {
    if (i >= colourMaps.size())
      addColourMap();

    //Replace properties with imported
    colourMaps[i]->properties.data = cmaps[i];
    colourMaps[i]->properties.checkall();
    json cmap = colourMaps[i]->properties.data;

    json colours = cmap["colours"];
    if (cmap["name"].is_string()) colourMaps[i]->name = cmap["name"];
    //Replace colours
    colourMaps[i]->colours.clear();
    for (unsigned int c=0; c < colours.size(); c++)
    {
      json colour = colours[c];
      Colour newcolour(colour["colour"]);
      colourMaps[i]->addAt(newcolour, colour["position"]);
    }
  }

  //Import objects
  json inobjects = imported["objects"];
  //Before loading state, set all object visibility to hidden
  //Only objects present in state data will be shown
  //for (unsigned int i=0; i < geometry.size(); i++)
  //  geometry[i]->showObj(NULL, false);

  unsigned int len = objects.size();
  if (len < inobjects.size()) len = inobjects.size();
  for (unsigned int i=0; i < objects.size(); i++)
  {
    /*if (i >= objects.size())
    {
      std::string name = inobjects[i]["name"];
      addObject(new DrawingObject(drawstate, name));
    }*/

    if (i >= inobjects.size())
    {
      //Not in imported list, assume hidden
      objects[i]->properties.data["visible"] = false;
    }
    
    //Merge properties
    objects[i]->properties.merge(inobjects[i]);
  }

  bool reload = (imported["reload"].is_boolean() && imported["reload"]);
  redraw(reload);
}



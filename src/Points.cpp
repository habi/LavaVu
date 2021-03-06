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

#include "GraphicsUtil.h"
#include "Geometry.h"

Points::Points(Session& session) : Geometry(session)
{
  type = lucPointType;
  pidx = swap = NULL;
  indexlist = NULL;
  idxcount = 0;
  indexvbo = 0;
  vbo = 0;
}

Points::~Points()
{
  close();
}

void Points::close()
{
  if (!session.global("gpucache"))
  {
    if (vbo)
      glDeleteBuffers(1, &vbo);
    if (indexvbo)
      glDeleteBuffers(1, &indexvbo);
    vbo = 0;
    indexvbo = 0;

    reload = true;
  }

  if (pidx)
    delete[] pidx;
  if (swap)
    delete[] swap;
  if (indexlist)
    delete[] indexlist;

  pidx = swap = NULL;
  indexlist = NULL;
}

void Points::update()
{
  //Get point count
  total = 0;
  for (unsigned int index = 0; index < geom.size(); index++)
  {
    //Un-structured vertices
    total += geom[index]->count();
    debug_print(" %s, points %d hidden? %s\n", geom[index]->draw->name().c_str(), geom[index]->count(), (!drawable(index) ? "yes" : "no"));
  }
  if (total == 0) return;

  //Ensure vbo recreated if total changed
  //To force update, set geometry->reload = true
  if (reload || vbo == 0)
    loadVertices();

  //Always reload indices if redraw flagged
  if (redraw) idxcount = 0;

  //Reload the sort array
  loadList(); 
}

void Points::loadVertices()
{
  debug_print("Reloading %d particles...\n", total);
  // Update points...
  clock_t t1,t2;

  // VBO - copy normals/colours/positions to buffer object for quick display
  int datasize;
  if (session.global("pointattribs"))
    datasize = sizeof(float) * 5 + sizeof(Colour);   //Vertex(3), two flags and 32-bit colour
  else
    datasize = sizeof(float) * 3 + sizeof(Colour);   //Vertex(3) and 32-bit colour
  if (!vbo) glGenBuffers(1, &vbo);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  //Initialise point buffer
  if (glIsBuffer(vbo))
  {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, total * datasize, NULL, GL_DYNAMIC_DRAW);
    debug_print("  %d byte VBO created, for %d vertices\n", (int)(total * datasize), total);
  }
  else
    debug_print("  VBO creation failed!\n");

  GL_Error_Check;

  unsigned char *p = NULL, *ptr = NULL;
  if (glIsBuffer(vbo))
  {
    ptr = p = (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    GL_Error_Check;
    if (!p) abort_program("glMapBuffer failed");
  }
  else ptr = NULL;
  //else abort_program("vbo fail");

//////////////////////////////////////////////////
  t1 = clock();
  //debug_print("Reloading %d particles...(size %f)\n", total);

  //Get eye distances and copy all particles into sorting array
  for (unsigned int s = 0; s < geom.size(); s++)
  {
    debug_print("Swarm %d, points %d hidden? %s\n", s, geom[s]->count(), (hidden[s] ? "yes" : "no"));

    //Calibrate colourMap
    ColourLookup& getColour = geom[s]->colourCalibrate();

    unsigned int hasColours = geom[s]->colourCount();
    if (hasColours > geom[s]->count()) hasColours = geom[s]->count(); //Limit to vertices
    unsigned int colrange = hasColours ? geom[s]->count() / hasColours : 1;
    if (colrange < 1) colrange = 1;
    debug_print("Using 1 colour per %d vertices (%d : %d)\n", colrange, geom[s]->count(), hasColours);

    Properties& props = geom[s]->draw->properties;
    float psize0 = props["pointsize"];
    float scaling = props["scaling"];
    //printf("psize %f scaling %f\n", psize0, scaling);
    psize0 *= scaling;
    float ptype = getPointType(s); //Default (-1) is to use the global (uniform) value
    bool attribs = session.global("pointattribs");
    unsigned int sizeidx = geom[s]->valuesLookup(geom[s]->draw->properties["sizeby"]);
    bool usesize = geom[s]->valueData(sizeidx) != NULL;
    //std::cout << geom[s]->draw->properties["sizeby"] << " : " << sizeidx << " : " << usesize << std::endl;
    Colour colour;

    for (unsigned int i = 0; i < geom[s]->count(); i ++)
    {
      //Copy data to VBO entry
      if (ptr)
      {
        assert((unsigned int)(ptr-p) < total * datasize);
        //Copies vertex bytes
        memcpy(ptr, geom[s]->render->vertices[i], sizeof(float) * 3);
        ptr += sizeof(float) * 3;
        //getColour(c, i);
        //Have colour values but not enough for per-vertex, spread over range (eg: per triangle)
        unsigned int cidx = i / colrange;
        if (cidx * colrange == i)
          getColour(colour, cidx);
        memcpy(ptr, &colour, sizeof(Colour));
        ptr += sizeof(Colour);
        //Optional per-object size/type
        if (attribs)
        {
          //Copies settings (size + smooth)
          float psize = psize0;
          if (usesize) psize *= geom[s]->valueData(sizeidx, i);
          memcpy(ptr, &psize, sizeof(float));
          ptr += sizeof(float);
          memcpy(ptr, &ptype, sizeof(float));
          ptr += sizeof(float);
        }
      }
    }

  }
  t2 = clock();
  debug_print("  %.4lf seconds to update %d particles into vbo\n", total, (t2-t1)/(double)CLOCKS_PER_SEC);
  t1 = clock();

  if (ptr) glUnmapBuffer(GL_ARRAY_BUFFER);
  GL_Error_Check;
}

void Points::loadList()
{
  // Update points sorting list...
  clock_t t1,t2;
  t1 = clock();

  //Create sorting array
  if (pidx) delete[] pidx;
  pidx = new PIndex[total];
  if (swap) delete[] swap;
  swap = new PIndex[total];
  if (indexlist) delete[] indexlist;
  indexlist = new unsigned int[total];
  if (pidx == NULL || swap == NULL || indexlist == NULL) abort_program("Memory allocation error (failed to allocate %d bytes)", sizeof(PIndex) * total * 2 + sizeof(unsigned int) * total);
  if (geom.size() == 0) return;
  int voffset = 0;
  unsigned int maxCount = session.global("pointmaxcount");
  unsigned int subSample = session.global("pointsubsample");
  //Auto-sub-sample if maxcount set
  if (maxCount > 0 && elements > maxCount)
    subSample = elements / maxCount + 0.5; //Rounded up
  elements = 0;
  uint32_t SEED;
  for (unsigned int s = 0; s < geom.size(); voffset += geom[s]->count(), s++)
  {
    if (!drawable(s)) continue;

    //Calibrate colourMap - required to re-cache filter settings (TODO: split filter reload into another function?)
    geom[s]->colourCalibrate();

    bool filter = geom[s]->draw->filterCache.size();
    for (unsigned int i = 0; i < geom[s]->count(); i ++)
    {
      if (filter && geom[s]->filter(i)) continue;
      // If subSampling, use a pseudo random distribution to select which particles to draw
      // If we just draw every n'th particle, we end up with a whole bunch in one region / proc
      SEED = i; //Reset the seed for determinism based on index
      if (subSample > 1 && SHR3(SEED) % subSample > 0) continue;

      pidx[elements].index = indexlist[elements] = voffset + i;
      pidx[elements].vertex = geom[s]->render->vertices[i];
      pidx[elements].distance = 0;
      elements++;
    }
  }
  t2 = clock();
  debug_print("  %.4lf seconds to update %d/%d particles into sort array\n", (t2-t1)/(double)CLOCKS_PER_SEC, elements, total);
  t1 = clock();

  if (session.global("sort"))
    sort();
}

//Depth sort the particles before drawing, called whenever the viewing angle has changed
void Points::sort()
{
  //List not yet loaded, wait
  //if (!pidx || !total == 0 || elements == 0 || !view->is3d) return;
  if (!pidx) return;

  clock_t t1,t2;
  t1 = clock();

  //Calculate min/max distances from view plane
  calcDistanceRange(true);

  //Update eye distances, clamping distance to integer between 0 and USHRT_MAX
  float multiplier = (float)USHRT_MAX / (view->maxdist - view->mindist);
  float fdistance;
  for (unsigned int i = 0; i < elements; i++)
  {
    //Distance from viewing plane is -eyeZ
    fdistance = eyePlaneDistance(view->modelView, pidx[i].vertex);
    //fdistance = view->eyeDistance(pidx[i].vertex);
    pidx[i].distance = (unsigned short)(multiplier * (fdistance - view->mindist));
  }
  t2 = clock();
  debug_print("  %.4lf seconds to calculate distances\n", (t2-t1)/(double)CLOCKS_PER_SEC);
  t1 = clock();

  //Depth sort using 2-byte key radix sort, 10 times faster than equivalent quicksort
  if (view->is3d)
  {
    radix_sort<PIndex>(pidx, swap, elements, 2);
    t2 = clock();
    debug_print("  %.4lf seconds to sort %d points\n", (t2-t1)/(double)CLOCKS_PER_SEC, elements);
  }

  //Re-map vertex indices in sorted order
  t1 = clock();
  //Lock the update mutex, to allow updating the indexlist and prevent access while drawing
  std::lock_guard<std::mutex> guard(loadmutex);
  //Reverse order farthest to nearest
  int distSample = session.global("pointdistsample");
  uint32_t SEED;
  idxcount = 0;
  for(int i=elements-1; i>=0; i--)
  {
    //Distance based sub-sampling
    if (distSample > 0)
    {
      SEED = pidx[i].index; //Reset the seed for determinism based on index
      int subSample = 1 + distSample * pidx[i].distance / USHRT_MAX; //[1,distSample]
      if (subSample > 1 && SHR3(SEED) % subSample > 0) continue;
    }
    indexlist[idxcount] = pidx[i].index;
    idxcount ++;
  }

  t2 = clock();
  if (distSample)
    debug_print("  %.4lf seconds to load %d indices (dist-sub-sampled %d)\n", (t2-t1)/(double)CLOCKS_PER_SEC, idxcount, distSample);
  else
    debug_print("  %.4lf seconds to load %d indices)\n", (t2-t1)/(double)CLOCKS_PER_SEC, idxcount);
  t1 = clock();

  //Force update after sort
  idxcount = 0;
}

//Reloads points into display list or VBO, required after data update and depth sort
void Points::render()
{
  clock_t t1,t2,tt;
  if (total == 0 || elements == 0) return;
  assert(pidx);

  tt = t1 = clock();

  // Index buffer object for quick display
  if (!indexvbo)
    glGenBuffers(1, &indexvbo);

  //Always set data size again in case changed
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexvbo);
  GL_Error_Check;
  //Initialise particle buffer
  if (glIsBuffer(indexvbo))
  {
    //Lock the update mutex, to wait for any updates to the indexlist to finish
    std::lock_guard<std::mutex> guard(loadmutex);
    idxcount = elements;
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxcount * sizeof(GLuint), indexlist, GL_DYNAMIC_DRAW);
    debug_print("  %d byte IBO uploaded %d indices\n", idxcount * sizeof(GLuint), idxcount);
  }
  else
    abort_program("IBO creation failed!\n");
  GL_Error_Check;

  t2 = clock();
  debug_print("  Total %.4lf seconds.\n", (t2-tt)/(double)CLOCKS_PER_SEC);
}

int Points::getPointType(int index)
{
  if (index != -1)
  {
    if (geom[index]->draw->properties.has("pointtype"))
      return geom[index]->draw->properties["pointtype"];
    else
      return -1; //Use global 
  }
  return session.global("pointtype");
}

void Points::draw()
{
  if (elements == 0) return;
  clock_t t0 = clock();
  double time;
  GL_Error_Check;

  Shader* prog = session.prog[lucPointType];
  setState(0, prog); //Set global draw state (using first object)

  //Re-render the particles if view has rotated
  if (idxcount != elements) render();
  //After render(), elements holds unfiltered count, idxcount is filtered

  glDepthFunc(GL_LEQUAL); //Ensure points at same depth both get drawn
  glEnable(GL_POINT_SPRITE);
  GL_Error_Check;

  //Gen TexCoords for point sprites (for GLSL < 1.3 where gl_PointCoord not available)
  glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
  GL_Error_Check;

  //Point size distance attenuation (disabled for 2d models)
  float scale0 = (float)geom[0]->draw->properties["scalepoints"] * session.scale2d; //Include 2d scale factor
  if (view->is3d && session.global("pointattenuate")) //Adjust scaling by model size when using distance size attenuation
  {
    prog->setUniform("uPointScale", scale0 * view->model_size);
    prog->setUniform("uPointDist", 1);
  }
  else
  {
    prog->setUniform("uPointScale", scale0);
    prog->setUniform("uPointDist", 0);
  }
  prog->setUniformi("uPointType", getPointType());

  glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
  GL_Error_Check;

  // Draw using vertex buffer object
  int stride = 3 * sizeof(float) + sizeof(Colour);
  bool attribs = session.global("pointattribs");
  if (attribs)
    stride += 2 * sizeof(float);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexvbo);
  if (idxcount > 0 && glIsBuffer(vbo) && glIsBuffer(indexvbo))
  {
    //Built in attributes gl_Vertex & gl_Color (Note: for OpenGL 3.0 onwards, should define our own generic attributes)
    glVertexPointer(3, GL_FLOAT, stride, (GLvoid*)0); // Load vertex x,y,z only
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, (GLvoid*)(3*sizeof(float)));   // Load rgba, offset 3 float
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    GLint aSize = 0, aPointType = 0;
    aSize = prog->attribs["aSize"];
    aPointType = prog->attribs["aPointType"];
    //Generic vertex attributes, "aSize", "aPointType"
    if (attribs)
    {
      if (aSize >= 0)
      {
        glEnableVertexAttribArray(aSize);
        glVertexAttribPointer(aSize, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3*sizeof(float)+sizeof(Colour)));
      }
      if (aPointType >= 0)
      {
        glEnableVertexAttribArray(aPointType);
        glVertexAttribPointer(aPointType, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(4*sizeof(float)+sizeof(Colour)));
      }

      //Draw the points
      glDrawElements(GL_POINTS, idxcount, GL_UNSIGNED_INT, (GLvoid*)0);

      if (aSize >= 0) glDisableVertexAttribArray(aSize);
      if (aPointType >= 0) glDisableVertexAttribArray(aPointType);
    }
    else
    {
      //Use generic default values
      glVertexAttrib1f(aPointType, -1.0);
      glVertexAttrib1f(aSize, 1.0);
      GL_Error_Check;

      //Draw the points
      glDrawElements(GL_POINTS, idxcount, GL_UNSIGNED_INT, (GLvoid*)0);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  //Restore state
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_POINT_SPRITE);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_TEXTURE_2D);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);

  time = ((clock()-t0)/(double)CLOCKS_PER_SEC);
  if (time > 0.05)
    debug_print("%.4lf seconds to draw points\n", time);
  GL_Error_Check;

  labels();
}

void Points::jsonWrite(DrawingObject* draw, json& obj)
{
  jsonExportAll(draw, obj);
}


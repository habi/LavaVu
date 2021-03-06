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

#include "Session.h"
#include "Util.h"
#include "GraphicsUtil.h"
#include "DrawingObject.h"
#include "ColourMap.h"
#include "View.h"
#include "ViewerTypes.h"
#include "Shaders.h"
#include "TimeStep.h"
#include "base64.h"

#ifndef Geometry__
#define Geometry__

//Types based on triangle renderer
#define TriangleBased(type) (type == lucTriangleType || type == lucGridType || type == lucShapeType || type == lucVectorType || type == lucTracerType)

// Point indices + distance for sorting
typedef struct
{
  unsigned short distance;
  GLuint index; //global index
  float* vertex; //Pointer to vertex
} PIndex;

typedef struct
{
  unsigned short distance;
  GLuint index[3]; //global indices
  float* vertex; //Pointer to vertex to calc distance from (usually centroid)
} TIndex;

//All the fixed data types for rendering
typedef struct
{
  Coord3DValues vertices;
  Coord3DValues vectors;
  Coord3DValues normals;
  UIntValues indices;
  UIntValues colours;
  Coord2DValues texCoords;
  UCharValues luminance;
  UCharValues rgb;
} RenderData;

//Shared pointer so we can pass these around without issues
typedef std::shared_ptr<RenderData> Render_Ptr;

//Colour lookup functors
class ColourLookup
{
public:
  DrawingObject* draw;
  Render_Ptr render;
  FloatValues* vals;
  FloatValues* ovals;
  float div255;

  ColourLookup() {}

  void init(DrawingObject* draw, Render_Ptr render, FloatValues* vals, FloatValues* ovals)
  {
    div255 = 1.0/255;
    this->draw = draw;
    this->render = render;
    this->vals = vals;
    this->ovals = ovals;
  }

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupMapped : public ColourLookup
{
 public:
  ColourLookupMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupRGBA : public ColourLookup
{
 public:
  ColourLookupRGBA() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupRGB : public ColourLookup
{
 public:
  ColourLookupRGB() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupLuminance : public ColourLookup
{
 public:
  ColourLookupLuminance() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

//Same as above with mapped opacity lookup
class ColourLookupOpacityMapped : public ColourLookup
{
 public:
  ColourLookupOpacityMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupMappedOpacityMapped : public ColourLookup
{
 public:
  ColourLookupMappedOpacityMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupRGBAOpacityMapped : public ColourLookup
{
 public:
  ColourLookupRGBAOpacityMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupRGBOpacityMapped : public ColourLookup
{
 public:
  ColourLookupRGBOpacityMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

class ColourLookupLuminanceOpacityMapped : public ColourLookup
{
 public:
  ColourLookupLuminanceOpacityMapped() {}

  virtual void operator()(Colour& colour, unsigned int idx) const;
};

//Geometry object data store
#define MAX_DATA_ARRAYS 64
class GeomData
{
public:
  static std::string names[lucMaxType];
  static std::string datalabels[lucMaxDataType+1];
  DrawingObject* draw; //Parent drawing object
  unsigned int width;
  unsigned int height;
  unsigned int depth;
  bool opaque;   //Flag for opaque geometry, render first, don't depth sort
  ImageLoader* texture; //Texture
  lucGeometryType type;   //Holds the object type

  //Colour/Opacity lookup functors
  ColourLookup _getColour;
  ColourLookupMapped _getColourMapped;
  ColourLookupRGBA _getColourRGBA;
  ColourLookupRGB _getColourRGB;
  ColourLookupLuminance _getColourLuminance;
  ColourLookupOpacityMapped _getColourOpacityMapped;
  ColourLookupMappedOpacityMapped _getColourMappedOpacityMapped;
  ColourLookupRGBAOpacityMapped _getColourRGBAOpacityMapped;
  ColourLookupRGBOpacityMapped _getColourRGBOpacityMapped;
  ColourLookupLuminanceOpacityMapped _getColourLuminanceOpacityMapped;

  float distance;

  //Bounding box of content
  float min[3];
  float max[3];

  std::vector<std::string> labels;      //Optional vertex labels

  std::vector<Values_Ptr> values;
  Render_Ptr render;

  static unsigned int byteSize(lucGeometryDataType type)
  {
    if (type == lucIndexData || type == lucRGBAData)
      return sizeof(unsigned int);
    if (type == lucLuminanceData || type == lucRGBData)
      return sizeof(unsigned char);
    return sizeof(float);
  }

  GeomData(DrawingObject* draw, lucGeometryType type)
    : draw(draw), width(0), height(0), depth(0), opaque(false), type(type)
  {
    render = std::make_shared<RenderData>();
    texture = NULL;

    for (int i=0; i<3; i++)
    {
      min[i] = HUGE_VAL;
      max[i] = -HUGE_VAL;
    }
  }

  ~GeomData()
  {
    if (texture)
      delete texture;
  }

  unsigned int count() {return render->vertices.size() / 3;}  //Number of vertices

  DataContainer* dataContainer(lucGeometryDataType type)
  {
    switch (type)
    {
      case lucVertexData:
        return (DataContainer*)&render->vertices;
      case lucVectorData:
        return (DataContainer*)&render->vectors;
      case lucNormalData:
        return (DataContainer*)&render->normals;
      case lucIndexData:
        return (DataContainer*)&render->indices;
      case lucRGBAData:
        return (DataContainer*)&render->colours;
      case lucTexCoordData:
        return (DataContainer*)&render->texCoords;
      case lucLuminanceData:
        return (DataContainer*)&render->luminance;
      case lucRGBData:
        return (DataContainer*)&render->rgb;
      default:
        return NULL;
    }
  }

  void checkPointMinMax(float *coord);
  void calcBounds();

  void label(std::string& labeltext);
  std::string getLabels();
  ColourLookup& colourCalibrate();
  int colourCount();
  unsigned int valuesLookup(const json& by);
  bool filter(unsigned int idx);
  FloatValues* colourData();
  float colourData(unsigned int idx);
  FloatValues* valueData(unsigned int vidx);
  float valueData(unsigned int vidx, unsigned int idx);

  unsigned int gridElements2d()
  {
    //Return number of quad elements in a 2d grid
    //with underflow prevention check
    if (width == 0 || height == 0) return 0;
    return (width-1) * (height-1);
  }
};

class Distance
{
public:
  int id;
  float distance;
  Distance(int i, float d) : id(i), distance(d) {}
  bool operator<(const Distance& rhs) const
  {
    return distance < rhs.distance;
  }
};

//Class for surface vertices, sorting, collating & averaging normals
class Vertex
{
public:
  Vertex() : vert(NULL), ref(-1), vcount(1) {}

  float* vert;
  int ref;
  int id;
  int vcount;

  bool operator<(const Vertex &rhs) const
  {
    //Comparison for vertex sort
    if (vert[0] != rhs.vert[0]) return vert[0] < rhs.vert[0];
    if (vert[1] != rhs.vert[1]) return vert[1] < rhs.vert[1];
    return vert[2] < rhs.vert[2];
  }

  bool operator==(const Vertex &rhs) const
  {
    //Comparison for equality
    return fabs(vert[0] - rhs.vert[0]) < 0.001 && fabs(vert[1] - rhs.vert[1]) < 0.001 && fabs(vert[2] - rhs.vert[2]) < 0.001;
  }

  friend std::ostream& operator<<(std::ostream& stream, const Vertex& v);
};

//Comparison for sort by id
struct vertexIdSort
{
  bool operator()(const Vertex &a, const Vertex &b)
  {
    return (a.id < b.id);
  }
};

//Shared pointer for GeomData
typedef std::shared_ptr<GeomData> Geom_Ptr;

//Container class for a list of geometry objects
class Geometry
{
  friend class Model;
protected:
  View* view;
  std::vector<Geom_Ptr> geom;
  std::vector<bool> hidden;
  unsigned int elements;
  unsigned int drawcount;
  DrawingObject* cached;

public:
  Session& session;
  //Store the actual maximum bounding box
  bool allhidden, internal, unscale;
  Vec3d iscale; //Factors for un-scaling
  lucGeometryType type;   //Holds the object type
  unsigned int total;     //Total entries of all objects in container
  bool redraw;    //Redraw flag
  bool reload;    //Reload and redraw flag
  std::mutex sortmutex;
  std::mutex loadmutex;

  Geometry(Session& session);
  virtual ~Geometry();

  void clear(); //Called before new data loaded
  void remove(DrawingObject* draw);
  void clearValues(DrawingObject* draw, std::string label="");
  void clearData(DrawingObject* draw, lucGeometryDataType dtype);
  virtual void close(); //Called on quit & before gl context recreated

  void compareMinMax(float* min, float* max);
  void dump(std::ostream& csv, DrawingObject* draw=NULL);
  virtual void jsonWrite(DrawingObject* draw, json& obj);
  void jsonExportAll(DrawingObject* draw, json& obj, bool encode=true);
  bool hide(unsigned int idx);
  void hideShowAll(bool hide);
  bool show(unsigned int idx);
  void showObj(DrawingObject* draw, bool state);
  void redrawObject(DrawingObject* draw, bool reload=false);
  void setValueRange(DrawingObject* draw);
  bool drawable(unsigned int idx);
  virtual void init(); //Called on GL init
  void setState(unsigned int i, Shader* prog=NULL);
  virtual void display(); //Display saved geometry
  virtual void update();  //Implementation should create geometry here...
  virtual void draw();    //Implementation should draw geometry here...
  virtual void sort();    //Threaded sort function
  void labels();  //Draw labels
  std::vector<Geom_Ptr> getAllObjects(DrawingObject* draw);
  Geom_Ptr getObjectStore(DrawingObject* draw);
  Geom_Ptr add(DrawingObject* draw);
  Geom_Ptr read(DrawingObject* draw, unsigned int n, lucGeometryDataType dtype, const void* data, int width=0, int height=0, int depth=0);
  void read(Geom_Ptr geomdata, unsigned int n, lucGeometryDataType dtype, const void* data, int width=0, int height=0, int depth=0);
  Geom_Ptr read(DrawingObject* draw, unsigned int n, const void* data, std::string label);
  Geom_Ptr read(Geom_Ptr geom, unsigned int n, const void* data, std::string label);
  void addTriangle(DrawingObject* obj, float* a, float* b, float* c, int level, bool swapY=false);
  void setupObject(DrawingObject* draw);
  void insertFixed(Geometry* fixed);
  bool inFixed(DataContainer* block0);
  void label(DrawingObject* draw, const char* labels);
  void label(DrawingObject* draw, std::vector<std::string> labels);
  void print();
  json getDataLabels(DrawingObject* draw);
  int size() {return geom.size();}
  virtual void setup(View* vp, float* min=NULL, float* max=NULL);
  void calcDistanceRange(bool eyePlane=false);
  void objectBounds(DrawingObject* draw, float* min, float* max);
  void move(Geometry* other);
  void toImage(unsigned int idx);
  void setTexture(DrawingObject* draw, ImageLoader* tex);
  void loadTexture(DrawingObject* draw, GLubyte* data, GLuint width, GLuint height, GLuint channels, bool flip=true);
  Quaternion vectorRotation(Vec3d rvector);
  void drawVector(DrawingObject *draw, const Vec3d& translate, const Vec3d& vector, float scale, float radius0, float radius1, float head_scale, int segment_count=24);
  void drawTrajectory(DrawingObject *draw, float coord0[3], float coord1[3], float radius0, float radius1, float arrowHeadSize, float scale[3], float maxLength=0.f, int segment_count=24);
  void drawCuboid(DrawingObject *draw, Vec3d& min, Vec3d& max, Quaternion& rot, bool quads=false);
  void drawCuboidAt(DrawingObject *draw, Vec3d& pos, Vec3d& dims, Quaternion& rot, bool quads=false);
  void drawSphere(DrawingObject *draw, Vec3d& centre, float radius, int segment_count=24);
  void drawEllipsoid(DrawingObject *draw, Vec3d& centre, Vec3d& radii, Quaternion& rot, int segment_count=24);

  //Return total vertex count
  unsigned int getVertexCount(DrawingObject* draw)
  {
    unsigned int count = 0;
    for (unsigned int i=0; i<geom.size(); i++)
      if (!draw || draw == geom[i]->draw) count += geom[i]->count();
    return count;
  }
  //Return vertex count of most recently used object
  unsigned int getVertexIdx(DrawingObject* draw)
  {
    Geom_Ptr geom = getObjectStore(draw);
    if (geom) return geom->count();
    return 0;
  }
};

class Triangles : public Geometry
{
protected:
  unsigned int idxcount;
  std::vector<unsigned int> counts;
  GLuint indexvbo, vbo;
public:
  Triangles(Session& session);
  virtual ~Triangles() {close();}
  virtual void close();
  unsigned int triCount();
  virtual void update();
  virtual void loadMesh() {}
  void loadBuffers();
  virtual void render();
  virtual void draw();
  virtual void jsonWrite(DrawingObject* draw, json& obj);
};

class TriSurfaces : public Triangles
{
  friend class QuadSurfaces; //Allow private access from QuadSurfaces
  TIndex *tidx;
  TIndex *swap;
  unsigned int* indexlist;
  unsigned int tricount;
  std::vector<Vec3d> centroids;
public:
  TriSurfaces(Session& session);
  virtual ~TriSurfaces();
  virtual void close();
  virtual void update();
  virtual void loadMesh();
  virtual void sort();    //Threaded sort function
  void loadList();
  void calcTriangleNormals(int index, std::vector<Vertex> &verts, std::vector<Vec3d> &normals);
  void calcTriangleNormalsWithIndices(int index);
  void calcGridNormals(int i, std::vector<Vec3d> &normals);
  void calcGridIndices(int i, std::vector<GLuint> &indices);
  virtual void render();
  virtual void draw();
};

class Lines : public Geometry
{
  unsigned int idxcount;
  std::vector<unsigned int> counts;
  GLuint indexvbo, vbo;
  unsigned int linetotal;
public:
  Lines(Session& session);
  virtual ~Lines();
  virtual void close();
  virtual void update();
  virtual void render();
  virtual void draw();
  virtual void jsonWrite(DrawingObject* draw, json& obj);
};

class Points : public Geometry
{
  PIndex *pidx;
  PIndex *swap;
  unsigned int* indexlist;
  unsigned int idxcount;
  GLuint indexvbo, vbo;
public:
  Points(Session& session);
  virtual ~Points();
  virtual void close();
  virtual void update();
  void loadVertices();
  void loadList();
  virtual void sort();    //Threaded sort function
  void render();
  int getPointType(int index=-1);
  virtual void draw();
  virtual void jsonWrite(DrawingObject* draw, json& obj);

  void dumpJSON();
};

class Glyphs : public Geometry
{
protected:
  Lines* lines;
  TriSurfaces* tris;
  Points* points;
public:
  Glyphs(Session& session);
  virtual ~Glyphs();
  virtual void close();
  virtual void setup(View* vp, float* min=NULL, float* max=NULL);
  virtual void display();
  virtual void sort();    //Threaded sort function
  virtual void update();
  virtual void draw();
  virtual void jsonWrite(DrawingObject* draw, json& obj);
};

class Links : public Glyphs
{
  bool all2d, any3d;
public:
  Links(Session& session, bool all2Dflag=false);
  ~Links() {close();}
  virtual void update();
  virtual void jsonWrite(DrawingObject* draw, json& obj);
};

class Vectors : public Glyphs
{
public:
  Vectors(Session& session);
  virtual void update();
};

class Tracers : public Glyphs
{
public:
  Tracers(Session& session);
  virtual void update();
};

class Shapes : public Glyphs
{
public:
  Shapes(Session& session);
  virtual void update();
};

class QuadSurfaces : public TriSurfaces
{
  std::vector<Distance> surf_sort;
public:
  QuadSurfaces(Session& session);
  virtual ~QuadSurfaces();
  virtual void update();
  virtual void sort();    //Threaded sort function
  virtual void render();
  void calcGridIndices(int i, std::vector<GLuint> &indices, unsigned int vertoffset);
  virtual void draw();
};

class Imposter : public Geometry
{
  GLuint vbo;
public:
  Imposter(Session& session);
  virtual ~Imposter() {close();}
  virtual void close();
  virtual void update();
  virtual void draw();
};

class Volumes : public Imposter
{
  std::vector<Distance> vol_sort;
public:
  GLuint colourTexture;
  std::map<DrawingObject*, unsigned int> slices;

  Volumes(Session& session);
  ~Volumes();
  virtual void close();
  virtual void update();
  virtual void sort();    //Threaded sort function
  virtual void draw();
  void render(int i);
  ImageData* getTiledImage(DrawingObject* draw, unsigned int index, unsigned int& iw, unsigned int& ih, unsigned int& channels, int xtiles=16);
  void saveTiledImage(DrawingObject* draw, int xtiles=16);
  void getSliceImage(ImageData* image, GeomData* slice, int offset=0);
  void saveSliceImages(DrawingObject* draw, unsigned int index);
  virtual void jsonWrite(DrawingObject* draw, json& obj);
  void isosurface(Triangles* surfaces, DrawingObject* target, bool clearvol=false);
};

//Sorting util functions
int compareXYZ(const void *a, const void *b);
int comparePoint(const void *a, const void *b);

template <typename T>
void radix(char byte, long N, T *source, T *dest)
//void radix(char byte, char size, long N, unsigned char *src, unsigned char *dst)
{
  // Radix counting sort of 1 byte, 8 bits = 256 bins
  int size = sizeof(T);
  long count[256], index[256];
  int i;
  memset(count, 0, sizeof(count)); //Clear counts
  unsigned char* src = (unsigned char*)source;
  //unsigned char* dst = (unsigned char*)dest;

  //Create histogram, count occurences of each possible byte value 0-255
  for (i=0; i<N; i++)
    count[src[i*size+byte]]++;

  //Calculate number of elements less than each value (running total through array)
  //This becomes the offset index into the sorted array
  //(eg: there are 5 smaller values so place me in 6th position = 5)
  index[0]=0;
  for (i=1; i<256; i++) index[i] = index[i-1] + count[i-1];

  //Finally, re-arrange data by index positions
  for (i=0; i<N; i++ )
  {
    int val = src[i*size + byte];  //Get value
    memcpy(&dest[index[val]], &source[i], size);
    //memcpy(&dst[index[val]*size], &src[i*size], size);
    index[val]++; //Increment index to push next element with same value forward one
  }
}


template <typename T>
void radix_sort(T *source, T* swap, long N, char bytes)
{
  assert(bytes % 2 == 0);
  //debug_print("Radix X sort: %d items %d bytes. Byte: ", N, size);
  // Sort bytes from least to most significant
  for (char x = 0; x < bytes; x += 2)
  {
    radix<T>(x, N, source, swap);
    radix<T>(x+1, N, swap, source);
    //radix_sort_byte(x, N, (unsigned char*)source, (unsigned char*)temp, size);
    //radix_sort_byte(x+1, N, (unsigned char*)temp, (unsigned char*)source, size);
  }
}


template <typename T>
void radix_sort(T *source, long N, char bytes)
{
  T* temp = (T*)malloc(N*sizeof(T));
  radix_sort(source, temp, N, bytes);
  free(temp);
}

#endif //Geometry__

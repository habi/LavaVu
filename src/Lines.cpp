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

#include "Geometry.h"

Lines::Lines(Session& session) : Geometry(session)
{
  type = lucLineType;
  idxcount = 0;
  vbo = 0;
  indexvbo = 0;
  linetotal = 0;
}

Lines::~Lines()
{
  close();
}

void Lines::close()
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
}

void Lines::update()
{
  //Skip update if count hasn't changed
  //To force update, set geometry->reload = true
  if (reload) elements = 0;
  if (elements > 0 && (linetotal == (unsigned int)elements || total == 0)) return;

  //Count lines
  linetotal = 0;
  for (unsigned int i=0; i<geom.size(); i++)
    linetotal += geom[i]->count();
  if (linetotal == 0) return;

  if (reload) idxcount = 0;

  //Copy data to Vertex Buffer Object
  // VBO - copy normals/colours/positions to buffer object
  unsigned char *p, *ptr;
  ptr = p = NULL;
  int datasize = sizeof(float) * 3 + sizeof(Colour);   //Vertex(3), and 32-bit colour
  int bsize = linetotal * datasize;
  //Initialise vertex buffer
  if (!vbo) glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  if (glIsBuffer(vbo))
  {
    glBufferData(GL_ARRAY_BUFFER, bsize, NULL, GL_STATIC_DRAW);
    debug_print("  %d byte VBO created for LINES, holds %d vertices\n", bsize, bsize/datasize);
    ptr = p = (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    GL_Error_Check;
  }
  if (!p) abort_program("VBO setup failed");

  clock_t t1,t2,tt;
  tt=clock();
  counts.clear();
  counts.resize(geom.size());
  for (unsigned int i=0; i<geom.size(); i++)
  {
    t1=tt=clock();

    //Calibrate colour maps on range for this object
    ColourLookup& getColour = geom[i]->colourCalibrate();

    unsigned int hasColours = geom[i]->colourCount();
    unsigned int colrange = hasColours ? geom[i]->count() / hasColours : 1;
    if (colrange < 1) colrange = 1;
    debug_print("Using 1 colour per %d vertices (%d : %d)\n", colrange, geom[i]->count(), hasColours);

    Colour colour;
    bool filter = geom[i]->draw->filterCache.size();
    for (unsigned int v=0; v < geom[i]->count(); v++)
    {
      if (filter && !internal && geom[i]->filter(v)) continue;

      //Have colour values but not enough for per-vertex, spread over range (eg: per segment)
      unsigned int cidx = v / colrange;
      if (cidx >= hasColours) cidx = hasColours - 1;
      getColour(colour, cidx);
      //if (cidx%100 ==0) printf("COLOUR %d => %d,%d,%d\n", cidx, colour.r, colour.g, colour.b);

      //Write vertex data to vbo
      assert((int)(ptr-p) < bsize);
      //Copies vertex bytes
      memcpy(ptr, &geom[i]->render->vertices[v][0], sizeof(float) * 3);
      ptr += sizeof(float) * 3;
      //Copies colour bytes
      memcpy(ptr, &colour, sizeof(Colour));
      ptr += sizeof(Colour);

      //Count of vertices actually plotted
      counts[i]++;
    }
    t2 = clock();
    debug_print("  %.4lf seconds to reload %d vertices\n", (t2-t1)/(double)CLOCKS_PER_SEC, counts[i]);
    t1 = clock();
    //Indexed
    if (geom[i]->render->indices.size() > 0)
      elements += geom[i]->render->indices.size();
    else
      elements += geom[i]->count();
  }

  glUnmapBuffer(GL_ARRAY_BUFFER);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  GL_Error_Check;

  t1 = clock();
  debug_print("Plotted %d lines in %.4lf seconds\n", linetotal, (t1-tt)/(double)CLOCKS_PER_SEC);
}

//Reloads line indices
void Lines::render()
{
  clock_t t1,t2;
  t1 = clock();
  if (elements == 0) return;

  //Prepare the Index buffer
  if (!indexvbo)
    glGenBuffers(1, &indexvbo);

  //Always set data size again in case changed
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexvbo);
  GL_Error_Check;
  if (glIsBuffer(indexvbo))
  {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements * sizeof(GLuint), NULL, GL_DYNAMIC_DRAW);
    debug_print("  %d byte IBO prepared for %d indices\n", elements * sizeof(GLuint), elements);
  }
  else
    abort_program("IBO creation failed\n");
  GL_Error_Check;

  //Element counts to actually plot (exclude filtered/hidden) per geom entry
  counts.clear();
  counts.resize(geom.size());

  //Upload vertex indices
  unsigned int offset = 0;
  unsigned int voffset = 0;
  idxcount = 0;
  for (unsigned int index = 0; index < geom.size(); index++)
  {
    unsigned int indices = geom[index]->render->indices.size();
    if (drawable(index))
    {
      if (indices > 0)
      {
        //Create the index list, adding offset from previous element vertices
        unsigned int indexlist[indices];
        for(unsigned int i=0; i<indices; i++)
          indexlist[i] = voffset + geom[index]->render->indices[i];

        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(GLuint), indices * sizeof(GLuint), indexlist);
        //printf("%d upload %d indices, voffset %d\n", index, indices, voffset);
        counts[index] = indices;
        offset += indices;
        GL_Error_Check;
      }
      else
      {
        //No indices, just raw vertices
        counts[index] = geom[index]->count();
        //printf("%d upload NO indices, %d vertices, voffset %d\n", index, counts[index], voffset);
      }
      idxcount += counts[index];
    }

    //Vertex index offset
    voffset += geom[index]->count();
  }

  GL_Error_Check;
  t2 = clock();
  debug_print("  %.4lf seconds to upload %d indices\n", (t2-t1)/(double)CLOCKS_PER_SEC, idxcount);
  t1 = clock();
  //After render(), elements holds unfiltered count, idxcount is filtered
  elements = idxcount;
}

void Lines::draw()
{
  //Re-render if count changes
  if (idxcount != elements) render();

  // Draw using vertex buffer object
  glPushAttrib(GL_ENABLE_BIT);
  clock_t t0 = clock();
  double time;
  int stride = 3 * sizeof(float) + sizeof(Colour);   //3+3+2 vertices, normals, texCoord + 32-bit colour
  int offset = 0;
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexvbo);
  if (geom.size() > 0 && elements > 0 && glIsBuffer(vbo))
  {
    glVertexPointer(3, GL_FLOAT, stride, (GLvoid*)0); // Load vertex x,y,z only
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, (GLvoid*)(3*sizeof(float)));   // Load rgba, offset 3 float
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    //Disable depth test on 2d models
    if (view->is3d)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);

    for (unsigned int i=0; i<geom.size(); i++)
    {
      Properties& props = geom[i]->draw->properties;
      if (drawable(i))
      {
        //Set draw state
        setState(i, session.prog[lucLineType]);

        //Lines specific state
        float scaling = props["scalelines"];
        //Don't apply object scaling to internal lines objects
        if (!internal) scaling *= (float)props["scaling"];
        float lineWidth = (float)props["linewidth"] * scaling * session.scale2d; //Include 2d scale factor
        if (lineWidth <= 0) lineWidth = scaling;
        glLineWidth(lineWidth);

        if (geom[i]->render->indices.size() > 0)
        {
          //Draw with index buffer
          if (props["link"])
            glDrawElements(GL_LINE_STRIP, counts[i], GL_UNSIGNED_INT, (GLvoid*)(offset*sizeof(GLuint)));
          else
            glDrawElements(GL_LINES, counts[i], GL_UNSIGNED_INT, (GLvoid*)(offset*sizeof(GLuint)));
        }
        else
        {
          //Draw directly from vertex buffer
          if (props["link"])
            glDrawArrays(GL_LINE_STRIP, offset, counts[i]);
          else
            glDrawArrays(GL_LINES, offset, counts[i]);
        }
      }

      offset += counts[i];
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  GL_Error_Check;

  //Restore state
  glPopAttrib();
  glBindTexture(GL_TEXTURE_2D, 0);

  time = ((clock()-t0)/(double)CLOCKS_PER_SEC);
  if (time > 0.05)
    debug_print("  %.4lf seconds to draw %d lines\n", time, offset);
  GL_Error_Check;
}

void Lines::jsonWrite(DrawingObject* draw, json& obj)
{
  jsonExportAll(draw, obj);
}

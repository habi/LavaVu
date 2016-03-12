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

#include "DrawingObject.h"
#include "Model.h"

unsigned int DrawingObject::lastid = 0;

DrawingObject::DrawingObject(std::string name, std::string props, ColourMap* map, unsigned int id) : id(id), name(name), skip(Model::noload)
{
  if (id == 0) this->id = DrawingObject::lastid+1;
  DrawingObject::lastid = this->id;
  colourMaps.resize(lucMaxDataType);
  //Sets the default colour map if provided, newer databases provide separately
  if (map) colourMaps[lucColourValueData] = map;

  properties.parseSet(props);
  //Adjust types of some old props
  properties.convertBools({"static", "lit", "cullface", "wireframe", "flat", "depthtest", "clip", "colourbar", "link", 
                          "tubes", "opaque", "colourmap", "isowalls", "tricubicfilter", "taper", "fade", "printticks", 
                          "printunits", "scientific"});

  //All props now lowercase, fix a couple of legacy camelcase values
  if (properties.has("pointSize")) {properties.data["pointsize"] = properties["pointSize"]; properties.data.erase("pointSize");}
  properties.data["visible"] = true;
  filterout = false;
  colourIdx = 0; //Default colouring data is first value block
}

DrawingObject::~DrawingObject()
{
  for (int i=0; i<textures.size(); i++)
    delete textures[i];
}

void DrawingObject::setup()
{
  //Cache values for faster lookups during draw calls
  colour = properties.getColour("colour", 255, 255, 255, 255);
  opacity = 1.0;
  if (properties.has("opacity"))
    opacity = properties["opacity"];
  //Convert values (1,255] -> [0,1]
  if (opacity > 1.0) opacity /= 255.0;

  colourIdx = properties["colourby"];
}

void DrawingObject::addColourMap(ColourMap* map, lucGeometryDataType data_type)
{
  //Sets the colour map for the specified geometry data type
  colourMaps[data_type] = map;
  /*
     if (data_type == lucRedValueData)
        map->setComponent(0);
     if (data_type == lucGreenValueData)
        map->setComponent(1);
     if (data_type == lucBlueValueData)
        map->setComponent(2);
  */
}

int DrawingObject::addTexture(std::string texfn)
{
  //If passed a valid file path:
  // - will add the texture loader object but not load the file yet
  //If not:
  // - will add an empty texture loader object that must be manually filled
  textures.push_back(new TextureLoader(texfn));
  return textures.size() - 1;
}

TextureData* DrawingObject::useTexture(int index)
{
  GL_Error_Check;
  //Load default texture if available
  if (textures.size() == 0)
  {
    if (properties.has("texturefile"))
    {
      std::string texfn = properties["texturefile"];
      if (texfn.length() > 0 && FileExists(texfn))
      {
        index = addTexture(texfn);
      }
      else
      {
        if (texfn.length() > 0) debug_print("Texture File: %s not found!\n", texfn.c_str());
        //If load failed, skip from now on
        properties.data["texturefile"] = "";
        return NULL;
      }
    }
    else
      return NULL;
  }

  //Use first available texture if out of range
  //if (index+1 > textures.size())
  if (textures.size() > 0 && (index < 0 || index+1 > textures.size()))
    index = textures.size() - 1;

  if (index >= 0 && textures[index])
  {
    //On first call only loads data from external file if provided
    //Then, and on subsequent calls, simply returns the preloaded texture
    return textures[index]->use();
  }
  GL_Error_Check;

  //No texture:
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_TEXTURE_3D);
  return NULL;
}


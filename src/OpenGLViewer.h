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

//OpenGLViewer - handles all GL rendering & display
#ifndef OpenGLViewer__
#define OpenGLViewer__

#include "GraphicsUtil.h"
#include "ApplicationInterface.h"
#include "OutputInterface.h"
#include "InputInterface.h"

class FrameBuffer
{
public:
  int width, height;
  GLuint target = 0;

  FrameBuffer() : width(0), height(0) {}
  virtual ~FrameBuffer() {}
  virtual ImageData* pixels(ImageData* image, int channels=3, bool flip=false);
};

class FBO : public FrameBuffer
{
public:
  bool enabled;
  GLuint frame;
  GLuint texture;
  GLuint depth;
  int downsample;

  FBO() : FrameBuffer()
  {
    enabled = false;
    texture = depth = frame = 0;
    downsample = 1;
  }

  virtual ~FBO()
  {
    destroy();
  }

  bool create(int w, int h);
  void destroy();
  void disable();
  ImageData* pixels(ImageData* image, int channels=3, bool flip=false);
};

class OpenGLViewer : public ApplicationInterface, public FrameBuffer
{
private:

protected:
  int timer = 0;
  int elapsed = 0;
  int animate = 0; //Redisplay when idle for # milliseconds

  std::vector<OutputInterface*> outputs; //Additional output attachments
  std::vector<InputInterface*> inputs; //Additional input attachments
  FBO fbo;
  int savewidth, saveheight;

public:
  int idle = 0;
  //Timer increments in ms
  int timer_msec = 10;
  int timer_animate = 50;

  ApplicationInterface* app;
  std::deque<std::string> commands;
  pthread_mutex_t cmd_mutex;

  GLboolean stereoBuffer, doubleBuffer;
  bool visible;
  bool stereo;
  bool fullscreen;
  bool postdisplay; //Flag to request a frame when animating
  bool quitProgram;
  bool isopen;   //Set when window is first opened

  int mouseState;
  ShiftState keyState;
  MouseButton button;
  int last_x, last_y;

  int blend_mode;
  int outwidth, outheight;
  std::string title;
  std::string output_path;
  bool imagemode;

  OpenGLViewer();
  virtual ~OpenGLViewer();

  //Window app management - called by derived classes, in turn call application interface virtuals
  virtual void open(int width=0, int height=0);
  virtual void init();
  virtual void setsize(int width, int height);
  virtual void resize(int new_width, int new_height);
  virtual void display(bool redraw=true);
  virtual void close();
  virtual void setTimer(int msec);

  // Default virtual functions for interactivity (call application interface)
  virtual bool mouseMove(int x, int y)
  {
    return app->mouseMove(x,y);
  }
  virtual bool mousePress(MouseButton btn, bool down, int x, int y)
  {
    return app->mousePress(btn, down, x, y);
  }
  virtual bool mouseScroll(float scroll)
  {
    return app->mouseScroll(scroll);
  }
  virtual bool keyPress(unsigned char key, int x, int y)
  {
    return app->keyPress(key, x, y);
  }

  virtual void show();
  virtual void setTitle() {}
  virtual void execute();
  void loop(bool interactive=true);

  virtual void fullScreen() {}
  void outputON(int w, int h, int channels=3);
  void outputOFF();
  ImageData* pixels(ImageData* image, int channels=3, bool flip=false);
  ImageData* pixels(ImageData* image, int w, int h, int channels=3, bool flip=false);
  std::string image(const std::string& path="", int jpegquality=0, bool transparent=false);

  void downSample(int q);

  void animateTimer(int msec=-1);

  void addOutput(OutputInterface* output)
  {
    outputs.push_back(output);
  }

  void addInput(InputInterface* input)
  {
    inputs.push_back(input);
  }

  bool pollInput(void);
};

#endif //OpenGLViewer__

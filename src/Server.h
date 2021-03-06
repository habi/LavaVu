//Server - web server
#ifndef Server__
#define Server__

#ifndef DISABLE_SERVER

#include "OutputInterface.h"
#include "OpenGLViewer.h"
#include "mongoose/mongoose.h"

class Server : public OutputInterface
{
  //Singleton class, construct with Server::Instance()
protected:
  //Server() {}   //Protected constructors
  Server(OpenGLViewer* viewer);
  //Server(Server const&) {} // copy constructor is protected
  //Server& operator=(Server const&) {} // assignment operator is protected

  static Server* _self;

  OpenGLViewer* viewer;

  // Thread sync
  pthread_mutex_t cs_mutex;
  pthread_cond_t condition_var;

  int client_id;
  bool updated;
  std::map<int,bool> synched; //Client status
  ImageData *imageCache;

public:
  static int port, threads, quality;
  static std::string htmlpath;
  static bool render;

  unsigned char* jpeg;
  int jpeg_bytes;

  //Public instance constructor/getter
  static Server* Instance(OpenGLViewer* viewer);
  static void Delete()
  {
    if (_self) delete _self;
    _self = NULL;
  }
  static bool running() { return _self != NULL;}
  virtual ~Server();
  struct mg_context* ctx;

  static int request(struct mg_connection *conn);

  // virtual functions for window management
  virtual void open(int width, int height);
  virtual void resize(int new_width, int new_height);
  virtual void display();
  virtual void close();
  virtual void idle() {}

  bool compare(ImageData* image);
};

#endif //DISABLE_SERVER
#endif //Server__

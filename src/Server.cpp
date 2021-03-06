#ifndef DISABLE_SERVER
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Server.h"
#include "Util.h"
#if defined _WIN32
#include <SDL/SDL_syswm.h>
#endif

#include "base64.h"
#include "jpeg/jpge.h"

#define MAX_POST_LEN 32767

Server* Server::_self = NULL; //Static

//Defaults
int Server::port = 8080;
int Server::quality = 90;
int Server::threads = 2;
bool Server::render = false;
std::string Server::htmlpath = "html";

Server* Server::Instance(OpenGLViewer* viewer)
{
  if (!_self)   // Only allow one instance of class to be generated.
    _self = new Server(viewer);

  return _self;
}

Server::Server(OpenGLViewer* viewer) : viewer(viewer)
{
  imageCache = NULL;
  jpeg = NULL;
  updated = false;
  client_id = 0;
  synched[0] = true;
  ctx = NULL;
  // Initialize mutex and condition variable objects
  pthread_mutex_init(&cs_mutex, NULL);
  pthread_cond_init (&condition_var, NULL);
}

Server::~Server()
{
  pthread_cond_broadcast(&condition_var);  //Display complete signal
  if (ctx)
    mg_stop(ctx);
}

// virtual functions for window management
void Server::open(int width, int height)
{
  if (!port) return;
  //Enable the timer
  //viewer->setTimer(250);   //1/4 sec timer
  struct mg_callbacks callbacks;

  char ports[16], threadstr[16];
  sprintf(ports, "%d", port);
  sprintf(threadstr, "%d", threads);
  debug_print("html path: %s ports: %s\n", htmlpath.c_str(), ports);
  const char *options[] =
  {
    "document_root", htmlpath.c_str(),
    "listening_ports", ports,
    "num_threads", threadstr,
    NULL
  };

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.begin_request = &Server::request;
  if ((ctx = mg_start(&callbacks, NULL, options)) == NULL)
    std::cerr << "HTTP server open failed" << std::endl;
}

void Server::resize(int new_width, int new_height)
{
}

bool Server::compare(ImageData* image)
{
  bool match = false;
  if (imageCache)
  {
    match = true;
    if (image->size() != imageCache->size()) return false;
    for (unsigned int i=0; i<image->size(); i++)
    {
      if (image->pixels[i] != imageCache->pixels[i])
      {
        match = false;
        break;
      }
    }
    delete imageCache;
  }
  imageCache = image;
  return match;
}

void Server::display()
{
  //Image serving can be disabled by global prop
  if (!ctx || !render) return;
  if (quality < 50) quality = 90;  //Ensure valid

  //If not currently sending an image, update the image data
  if (pthread_mutex_trylock(&cs_mutex) == 0)
  {
    //CRITICAL SECTION
    // Read the pixels (flipped)
    ImageData *image = viewer->pixels(NULL, 3, true);

    if (!compare(image))
    {
      // Writes JPEG image to memory buffer.
      // On entry, jpeg_bytes is the size of the output buffer pointed at by jpeg, which should be at least ~1024 bytes.
      // If return value is true, jpeg_bytes will be set to the size of the compressed data.
      jpeg_bytes = viewer->width * viewer->height * 3;
      if (jpeg) delete[] jpeg;
      jpeg = new unsigned char[jpeg_bytes];

      // Fill in the compression parameter structure.
      jpge::params params;
      params.m_quality = quality;
      params.m_subsampling = jpge::H1V1;   //H2V2/H2V1/H1V1-none/0-grayscale

      if (compress_image_to_jpeg_file_in_memory(jpeg, jpeg_bytes, viewer->width, viewer->height, 3,
          (const unsigned char *)image->pixels, params))
      {
        debug_print("JPEG compressed, size %d\n", jpeg_bytes);
      }
      else
        debug_print("JPEG compress error\n");

      //Deleted in compare
      //delete image;

      updated = true; //Set new frame rendered flag
      std::map<int,bool>::iterator iter;
      for (iter = synched.begin(); iter != synched.end(); ++iter)
      {
        iter->second = false; //Flag update waiting
      }
      pthread_cond_broadcast(&condition_var);  //Display complete signal to all waiting clients
      //pthread_cond_signal(&condition_var);  //Display complete signal
    }
    pthread_mutex_unlock(&cs_mutex); //END CRITICAL SECTION
  }
  else
  {
    viewer->postdisplay = true;  //Need to update
    debug_print("DELAYING IMAGE UPDATE\n");
  }
}

void Server::close()
{
}

void send_file(const char *fname, struct mg_connection *conn)
{
  std::ifstream file(fname, std::ios::binary);
  std::streambuf* raw_buffer = file.rdbuf();

  //size_t len = raw_buffer->pubseekoff(0, std::ios::end, std::ios::in);;
  file.seekg(0,std::ios::end);
  size_t len = file.tellg();
  file.seekg(0,std::ios::beg);

  char* src = new char[len];
  raw_buffer->sgetn(src, len);

  //Base64 encode!
  std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(src), len);
  mg_write(conn, encoded.c_str(), encoded.length());

  delete[] src;
}

void send_string(std::string str, struct mg_connection *conn)
{
  //Base64 encode!
  std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(str.c_str()), str.length());
  mg_write(conn, encoded.c_str(), encoded.length());
}

int Server::request(struct mg_connection *conn)
{
  const struct mg_request_info *request_info = mg_get_request_info(conn);
  int id = -1;
  debug_print("SERVER REQUEST: %s\n", request_info->uri);

  //Default location is control interface only
  if (strcmp("/", request_info->uri) == 0)
  {
    mg_printf(conn, "HTTP/1.1 302 Found\r\n"
              "Set-Cookie: original_url=%s\r\n"
              "Location: %s\r\n\r\n",
              request_info->uri, "/index.html?server");
  }
  //Default viewer with server enabled
  if (strcmp("/viewer", request_info->uri) == 0)
  {
    mg_printf(conn, "HTTP/1.1 302 Found\r\n"
              "Set-Cookie: original_url=%s\r\n"
              "Location: %s\r\n\r\n",
              request_info->uri, "/viewer.html?server");
  }
  else if (strstr(request_info->uri, "/timestep=") != NULL)
  {
    //Update timesteps from database,
    //Load timestep
    //Write image for each window
    int ts = atoi(request_info->uri+10);
    debug_print("TIMESTEP REQUEST %d\n", ts);
  }
  else if (strstr(request_info->uri, "/connect") != NULL)
  {
    //Return an id assigned to this client
    _self->client_id++;
    debug_print("NEW CONNECTION: %d (%d THREADS)\n", _self->client_id, _self->threads);
    mg_printf(conn, "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "%d", _self->client_id);
    _self->updated = true; //Force update
    _self->synched[_self->client_id] = false;
  }
  else if (strstr(request_info->uri, "/disconnect=") != NULL)
  {
    int id = atoi(request_info->uri+12);
    _self->synched.erase(id);
    debug_print("%d DISCONNECTED, CLIENT %d\n", id, _self->client_id);
    mg_printf(conn, "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n");
    _self->updated = true; //Force update
    pthread_cond_broadcast(&_self->condition_var);  //Display complete signal
  }
  else if (strstr(request_info->uri, "/objects") != NULL || 
           strstr(request_info->uri, "/getstate") != NULL)
  {
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    std::string objects = _self->viewer->app->requestData("objects");
    mg_write(conn, objects.c_str(), objects.length());
  }
  else if (strstr(request_info->uri, "/history") != NULL)
  {
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    std::string history = _self->viewer->app->requestData("history");
    mg_write(conn, history.c_str(), history.length());
  }
  else if (strstr(request_info->uri, "/image") != NULL)
  {
    id = 0; //Default client
    //Get id from url if passed eg: /image=id
    if (strstr(request_info->uri, "/image=") != NULL)
      id = atoi(request_info->uri+7);
  }
  else if (strstr(request_info->uri, "command=") != NULL)
  {
    std::string data = request_info->uri+1;
    const size_t equals = data.find('=');
    const size_t amp = data.find('&');
    //Push command onto queue to be processed in the viewer thread
    pthread_mutex_lock(&_self->viewer->cmd_mutex);
    if (amp != std::string::npos)
      _self->viewer->commands.push_back(data.substr(equals+1, amp-equals-1));
    else
      _self->viewer->commands.push_back(data.substr(equals+1));
    debug_print("CMD: %s\n", data.substr(equals+1).c_str());
    _self->viewer->postdisplay = true;
    pthread_mutex_unlock(&_self->viewer->cmd_mutex);
    if (strstr(request_info->uri, "icommand=") != NULL)
      id = 0;  //Image requested with command, use default client id
    else
      mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
  }
  else if (strstr(request_info->uri, "/post") != NULL)
  {
    //mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    char post_data[MAX_POST_LEN+1] = {0};
    int post_data_len = mg_read(conn, post_data, sizeof(post_data));
    if (post_data_len > MAX_POST_LEN)
    {
      std::cerr << "ERROR! Post data truncated, skipping\n";
      post_data_len = 0;
    }
    //printf("%d\n%s\n", post_data_len, post_data);
    if (post_data_len)
    {
      //Push command onto queue to be processed in the viewer thread
      pthread_mutex_lock(&_self->viewer->cmd_mutex);
      //Seems post data string can exceed data length or be missing terminator
      post_data[post_data_len] = 0;
      _self->viewer->commands.push_back(std::string(post_data));
      _self->viewer->postdisplay = true;
      pthread_mutex_unlock(&_self->viewer->cmd_mutex);
    }
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"); //Empty OK response required
  }
  else if (strstr(request_info->uri, "/key=") != NULL)
  {
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    std::string data = request_info->uri+1;
    pthread_mutex_lock(&_self->viewer->cmd_mutex);
    _self->viewer->commands.push_back("key " + data);
    _self->viewer->postdisplay = true;
    pthread_mutex_unlock(&_self->viewer->cmd_mutex);
  }
  else if (strstr(request_info->uri, "/mouse=") != NULL)
  {
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"); //Empty response, prevent XML errors
    std::string data = request_info->uri+1;
    pthread_mutex_lock(&_self->viewer->cmd_mutex);
    _self->viewer->commands.push_back("mouse " + data);
    _self->viewer->postdisplay = true;
    pthread_mutex_unlock(&_self->viewer->cmd_mutex);
  }
  else if (strstr(request_info->uri, "/render") != NULL)
  {
    //Enable/disable image serving
    Server::render = !Server::render;
    std::cerr << "Image serving  " << (Server::render ? "ENABLED" : "DISABLED") << std::endl;
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"); //Empty OK response required
  }
  else
  {
    // No suitable handler found, mark as not processed. Mongoose will
    // try to serve the request.
    return 0;
  }

  //Respond with an image frame
  if (id >= 0)
  {
    if (!Server::render) {
      Server::render = true;
      _self->updated = true; //Force update
    _self->viewer->postdisplay = true;
    _self->synched[id] = true;
    }
    //Image update requested, wait until data available then send
    pthread_t tid;
    tid = pthread_self();
    pthread_mutex_lock(&_self->cs_mutex);
    debug_print("CLIENT %d THREAD ID %u ENTERING WAIT STATE (synched %d)\n", id, tid, _self->synched[id]);

    //while (!_self->updated && !_self->viewer->quitProgram)
    while (_self->synched[id] && !_self->viewer->quitProgram)
    {
      debug_print("CLIENT %d THREAD ID %u WAITING\n", id, tid);
      //This doesn't seem to be needed, causes constant display updates even when no changes
      //_self->viewer->notIdle(1000); //Starts the idle timer (1 second before display fired)
      pthread_cond_wait(&_self->condition_var, &_self->cs_mutex);
    }
    debug_print("CLIENT %d THREAD ID %u RESUMED, quit? %d\n", id, tid, _self->viewer->quitProgram);

    if (!_self->viewer->quitProgram)
    {
      //debug_print("Sending JPEG %d bytes...\n", _self->jpeg_bytes);
      mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n");
      mg_printf(conn, "Content-Length: %d\r\n", _self->jpeg_bytes);
      //Allow cross-origin requests
      mg_printf(conn, "Access-Control-Allow-Origin: *\r\n\r\n");
      //Write raw
      mg_write(conn, _self->jpeg, _self->jpeg_bytes);

      _self->updated = false;
      _self->synched[id] = true;
    }
    pthread_mutex_unlock(&_self->cs_mutex);
  }

  // Returning non-zero tells mongoose that our function has replied to
  // the client, and mongoose should not send client any more data.
  return 1;
}

#endif  //DISABLE_SERVER

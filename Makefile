#Install path
PREFIX ?= lavavu
PROGNAME = LavaVu
PROGRAM = $(PREFIX)/$(PROGNAME)
LIBRARY = $(PREFIX)/lib$(PROGNAME).$(LIBEXT)
SWIGLIB = $(PREFIX)/_$(PROGNAME)Python.so

PYTHON ?= python
PYLIB ?= `python-config --libs`
PYINC ?= `python-config --includes`

#Object files path
OPATH ?= tmp
#HTML files path
HTMLPATH = $(PREFIX)/html

#Compilers
CXX ?= g++
CC ?= gcc

#Default flags
CFLAGS = $(FLAGS) -fPIC -Isrc
CPPFLAGS = $(CFLAGS) -std=c++0x
#For external dependencies
EXTCFLAGS = -O3 -DNDEBUG -fPIC -Isrc
EXTCPPFLAGS = $(EXTCFLAGS) -std=c++0x
SWIGFLAGS=

#Default to X11 enabled unless explicitly disabled
X11 ?= 1

# Separate compile options per configuration
ifeq ($(CONFIG),debug)
  CFLAGS += -g -O0 -DDEBUG
else
  CFLAGS += -O3 -DNDEBUG
endif

#Linux/Mac specific libraries/flags for offscreen & interactive
OS := $(shell uname)
ifeq ($(OS), Darwin)
  SWIGFLAGS= -undefined suppress -flat_namespace
  CFLAGS += -Wno-unknown-warning-option -Wno-c++14-extensions -Wno-shift-negative-value
  #Mac OS X with Cocoa + CGL
  CFLAGS += -FCocoa -FOpenGL -I/usr/include/malloc -stdlib=libc++
  LIBS=-lc++ -ldl -lpthread -framework Cocoa -framework Quartz -framework OpenGL -lobjc -lm -lz
  DEFINES += -DUSE_FONTS -DHAVE_CGL
  APPLEOBJ=$(OPATH)/CocoaViewer.o
  LIBEXT=dylib
  LIBBUILD=-dynamiclib
  LIBINSTALL=-install_name @loader_path/lib$(PROGNAME).$(LIBEXT)
else
  #Linux 
  LIBS=-ldl -lpthread -lm -lGL -lz
  DEFINES += -DUSE_FONTS
  LIBEXT=so
  LIBBUILD=-shared
  LIBLINK=-Wl,-rpath='$$ORIGIN'
ifeq ($(GLUT), 1)
  #GLUT optional
  LIBS+= -lglut
  DEFINES += -DHAVE_GLUT
else ifeq ($(SDL), 1)
  #SDL optional
  LIBS+= -lSDL
  DEFINES += -DHAVE_SDL
else ifeq ($(X11), 1)
  #X11 default
  LIBS+= -lX11
  DEFINES += -DHAVE_X11
else
  #Assume providing own context
  DEFINES += -DHAVE_GLCONTEXT
endif
endif

#Extra defines passed
DEFINES += $(DEFS)
ifdef SHADER_PATH
DEFINES += -DSHADER_PATH=\"$(SHADER_PATH)\"
endif

#Add a libpath (useful for linking specific libGL)
ifdef LIBDIR
  LIBS+= -L$(LIBDIR) -Wl,-rpath=$(LIBDIR)
endif

#Other optional components
ifeq ($(VIDEO), 1)
  CFLAGS += -DHAVE_LIBAVCODEC -DHAVE_SWSCALE
  LIBS += -lavcodec -lavutil -lavformat -lswscale
endif
#Default libpng disabled, use built in png support
LIBPNG ?= 0
ifeq ($(LIBPNG), 1)
  CFLAGS += -DHAVE_LIBPNG
  LIBS += -lpng
else
  CFLAGS += -DUSE_ZLIB
endif
ifeq ($(TIFF), 1)
  CFLAGS += -DHAVE_LIBTIFF
  LIBS += -ltiff
endif

#Source search paths
vpath %.cpp src:src/Main:src:src/jpeg:src/png
vpath %.h src/Main:src:src/jpeg:src/png:src/sqlite3
vpath %.c src/mongoose:src/sqlite3
vpath %.cc src

#Always run this script to update version.cpp if git version changes
TMP := $(shell $(PYTHON) version.py)

SRC := $(wildcard src/*.cpp) $(wildcard src/Main/*Viewer.cpp) $(wildcard src/jpeg/*.cpp)
INC := $(wildcard src/*.h) $(wildcard src/Main/*.h)

ifneq ($(LIBPNG), 1)
	SRC += $(wildcard src/png/*.cpp)
	INC += $(wildcard src/png/*.h)
endif

OBJ := $(SRC:%.cpp=%.o)
#Strip paths (src) from sources
OBJS = $(notdir $(OBJ))
#Add object path
OBJS := $(OBJS:%.o=$(OPATH)/%.o)
ALLOBJS := $(OBJS)
#Additional library objects (no cpp extension so not included above)
ALLOBJS += $(OPATH)/mongoose.o $(OPATH)/sqlite3.o
#Mac only
ALLOBJS += $(APPLEOBJ)

default: install

.PHONY: install
install: $(PROGRAM) $(SWIGLIB) $(HTMLPATH)/viewer.html
	@if [ $(PREFIX) != "lavavu" ]; then \
	cp -R lavavu/*.py $(PREFIX); \
	cp -R lavavu/shaders/*.* $(PREFIX)/shaders; \
	cp -R lavavu/html/*.* $(HTMLPATH); \
	fi

$(HTMLPATH)/viewer.html: lavavu/html/viewer_template.html lavavu/shaders/*.frag lavavu/shaders/*.vert
	sed -e "/Point vertex shader/    r lavavu/shaders/pointShaderWEBGL.vert"  \
      -e "/Point fragment shader/  r lavavu/shaders/pointShaderWEBGL.frag"  \
      -e "/Tri vertex shader/      r lavavu/shaders/triShaderWEBGL.vert"    \
      -e "/Tri fragment shader/    r lavavu/shaders/triShaderWEBGL.frag"    \
      -e "/Volume vertex shader/   r lavavu/shaders/volumeShaderWEBGL.vert" \
      -e "/Volume fragment shader/ r lavavu/shaders/volumeShaderWEBGL.frag" \
      -e "/Line vertex shader/     r lavavu/shaders/lineShaderWEBGL.vert"   \
      -e "/Line fragment shader/   r lavavu/shaders/lineShaderWEBGL.frag"   \
			< lavavu/html/viewer_template.html > $(HTMLPATH)/viewer.html

.PHONY: force
$(OPATH)/compiler_flags: force | paths
	@echo '$(CPPFLAGS)' | cmp -s - $@ || echo '$(CPPFLAGS)' > $@

.PHONY: paths
paths:
	@mkdir -p $(OPATH)
	@mkdir -p $(PREFIX)
	@mkdir -p $(HTMLPATH)
	@mkdir -p $(PREFIX)/shaders

#Rebuild *.cpp
$(OBJS): $(OPATH)/%.o : %.cpp $(OPATH)/compiler_flags $(INC)
	$(CXX) $(CPPFLAGS) $(DEFINES) -c $< -o $@

$(PROGRAM): $(LIBRARY) main.cpp | paths
	$(CXX) $(CPPFLAGS) $(DEFINES) -c src/Main/main.cpp -o $(OPATH)/main.o
	$(CXX) -o $(PROGRAM) $(OPATH)/main.o $(LIBS) -lLavaVu -L$(PREFIX) $(LIBLINK)

$(LIBRARY): $(ALLOBJS) | paths
	$(CXX) -o $(LIBRARY) $(LIBBUILD) $(LIBINSTALL) $(ALLOBJS) $(LIBS)

$(OPATH)/mongoose.o : mongoose.c
	$(CC) $(EXTCFLAGS) -o $@ -c $^ 

src/sqlite3/sqlite3.c:
	#Ensure the submodule is checked out
	git submodule update --init

$(OPATH)/sqlite3.o : sqlite3.c
	$(CC) $(EXTCFLAGS) -o $@ -c $^ 

$(OPATH)/CocoaViewer.o : src/Main/CocoaViewer.mm
	$(CXX) $(CPPFLAGS) $(DEFINES) -o $@ -c $^ 

#Python interface
SWIGSRC = src/LavaVuPython_wrap.cxx
SWIGOBJ = $(OPATH)/LavaVuPython_wrap.os
NUMPYINC = $(shell $(PYTHON) -c 'import numpy; print(numpy.get_include())')
ifneq ($(NUMPYINC),)
#Skip build if numpy not found
SWIGOBJ = $(OPATH)/LavaVuPython_wrap.os
endif

.PHONY: swig
swig : $(INC) src/LavaVuPython.i | paths
	swig -v -Wextra -python -ignoremissing -O -c++ -DSWIG_DO_NOT_WRAP -outdir $(PREFIX) src/LavaVuPython.i

$(SWIGLIB) : $(LIBRARY) $(SWIGOBJ)
	$(CXX) -o $(SWIGLIB) $(LIBBUILD) $(SWIGOBJ) $(SWIGFLAGS) ${PYLIB} -lLavaVu -L$(PREFIX) $(LIBLINK)

$(SWIGOBJ) : $(SWIGSRC)
	$(CXX) $(CPPFLAGS) ${PYINC} -c $(SWIGSRC) -o $(SWIGOBJ) -I$(NUMPYINC)

docs: src/LavaVu.cpp src/Session.h
	$(PROGRAM) -S -h -p0 : docs:properties quit > docs/Property-Reference.md
	$(PROGRAM) -S -h -p0 : docs:interaction quit > docs/Interaction.md
	$(PROGRAM) -S -h -p0 : docs:scripting quit > docs/Scripting-Commands-Reference.md
	$(PROGRAM) -? > docs/Commandline-Arguments.md

clean:
	-rm -f *~ $(OPATH)/*.o
	-rm -f $(PREFIX)/*.so
	-rm -f $(PREFIX)/LavaVu
	@if [ $(PREFIX) != "lavavu" ]; then \
	-rm -rf $(PREFIX)/html; \
	-rm -rf $(PREFIX)/shaders; \
	-rm -f $(PREFIX)/*.py; \
	fi


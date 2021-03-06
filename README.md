![# logo](http://owen.kaluza.id.au/Slides/2017-08-15/LavaVu.png)

[![Build Status](https://travis-ci.org/OKaluza/LavaVu.svg?branch=master)](https://travis-ci.org/OKaluza/LavaVu)

A scientific visualisation tool with a python interface for fast and flexible visual analysis.

![examplevis](http://owen.kaluza.id.au/Slides/2017-08-15/combined.png)


LavaVu development is supported by the [Monash Immersive Visualisation Plaform](http://monash.edu.au/mivp) and the Simulation, Analysis & Modelling component of the [NCRIS AuScope](http://www.auscope.org.au/ncris/) capability.

The acronym stands for: lightweight, automatable  visualisation and analysis viewing utility, but "lava" is also a reference to its primary application as a viewer for geophysical simulations. It was also chosen to be unique enough to find the repository with google.

The project started in the gLucifer<sup>1</sup> framework for visualising geodynamics simulations. The OpenGL visualisation module was separated from the simulation and sampling libraries and became a more general purpose visualisation tool. gLucifer continues as a set of sampling tools for Underworld simulations as part of the [Underworld2](https://github.com/underworldcode/underworld2/) code. LavaVu provides the rendering library for creating 2d and 3d visualisations to view this sampled data, inline within interactive IPython notebooks and offline through saved visualisation databases and images/movies.

As a standalone tool it is a scriptable 3D visualisation tool capable of producing publication quality high res images and video output from time varying data sets along with HTML5 3D visualisations in WebGL.
Rendering features include correctly and efficiently rendering large numbers of opaque and transparent points and surfaces and volume rendering by GPU ray-marching. There are also features for drawing vector fields and tracers (streamlines).

Control is via python and a set of simple verbose scripting commands along with mouse/keyboard interaction.
GUI components can be generated for use from a web browser via the python "control" module and a built in web server.

A native data format called GLDB is used to store and visualisations in a compact single file, using SQLite for storage and fast loading. A small number of other data formats are supported for import (OBJ surfaces, TIFF stacks etc). 
Further data import formats are supported with python scripts, with the numpy interface allowing rapid loading and manipulation of data.

A CAVE2 virtual reality mode is provided by utilising Omegalib (http://github.com/uic-evl/omegalib) to allow use in Virtual Reality and Immersive Visualisation facilities, such as the CAVE2 at Monash, see (https://github.com/mivp/LavaVR).
Side-by-side and quad buffer stereoscopic 3D support is also provided for other 3D displays.

### This repository ###

This is the public source code repository for all development on the project.
Currently I am the sole developer but contributions are welcome.
Development happens in the "master" branch with stable releases tagged.

### How do I get set up? ###

It's now in the python package index, so you can install with *pip*:

```
pip install --user lavavu
```

Try it out:

```
python
> import lavavu
> lv = lavavu.Viewer() #Create a viewer
> lv.test()            #Create some sample data
> lv.interactive()     #Open an interactive viewer window
```

Alternatively, clone this repository with *git* and build from source:

```
  git clone https://github.com/OKaluza/LavaVu
  cd LavaVu
  make -j4
```

If all goes well the viewer will be built, try running with:
  ./lavavu/LavaVu

### Dependencies ###

* To use with python requires python 2.7+ and NumPy
* For video output, requires: libavcodec, libavformat, libavutil, libswscale (from FFmpeg / libav)
* To build the python interface from source requires swig (http://www.swig.org/)

### Who do I talk to? ###

* Report bugs/issues here on github: https://github.com/OKaluza/LavaVu/issues
* Contact developer: Owen Kaluza (at) monash.edu

For further documentation / examples, see the Wiki
* https://github.com/OKaluza/LavaVu/wiki

---
<sup>1</sup> Stegman, D.R., Moresi, L., Turnbull, R., Giordani, J., Sunter, P., Lo, A. and S. Quenette, *gLucifer: Next Generation Visualization Framework for High performance computational geodynamics*, 2008, Visual Geosciences

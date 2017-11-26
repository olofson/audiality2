Audiality 2
===========

This file explains how to build and install Audiality 2 from source.

Installing
----------

* Install the dependencies. You'll need SDL and/or JACK for the integrated audio I/O drivers, but it is possible to build Audiality 2 without any audio drivers at all, if you want to use Audiality 2 for offline rendering, or handle audio I/O in the host applications.
  * SDL 1.2 or 2.0 (optional; required only for a2test)
    * http://libsdl.org
  * JACK (optional; preferred for low latency audio and studio integration)
    * http://jackaudio.org
  * MXE (optional; needed for cross-compiling Windows binaries)
    * http://mxe.cc/

* Download the source code.
  * Archive (check the Download panel)
    * http://audiality.org/
  * GitHub/SSH
    * git clone git@github.com:olofson/audiality2.git
  * *Alternatively:* GitHub/HTTPS
    * git clone https://github.com/olofson/audiality2.git

* Configure the source tree.
  * Option 1 (Un*x with bash or similar shell)
    * ./configure [*target*]
      * Currently available targets:
        * release
        * release32
        * maintainer
        * debug
        * mingw-release
        * mingw-debug
        * mingw-static
        * all *(all of the above)*
        * (Default if not specified: release)
  * Option 2 (Using CMake directly)
    * mkdir build
    * cd build
    * cmake ..

* Build and install.
  * ./make-all
    * (The resulting executables are found in "build/<target>/src/")
  * *Alternatively:* Enter the desired build target directory.
    * make
    * *Optional:* sudo make install

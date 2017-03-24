Audiality 2
===========
[![Build Status](https://travis-ci.org/olofson/audiality2.svg?branch=master)](https://travis-ci.org/olofson/audiality2)

Overview
--------

Audiality 2 is a realtime audio and music engine, primarily intended for video games. While it supports traditional sample playback as well as additive, subtractive and granular synthesis, the distinctive feature is subsample accurate realtime scripting.

Design
------

Audiality 2 generates sound and music using a tree graph of voices, driven by user defined programs running on a virtual machine. Voices are modular, allowing custom combinations of oscillators, filters and other units.

Each voice is controlled by a program (user defined script code) that can be given initial arguments, and receive messages for realtime control. A program can (recursively) spawn other programs on subvoices, and control these by sending messages.

Timing is subsample accurate, and durations can be specified in milliseconds, or in terms of user defined musical ticks.

The name Audiality...
---------------------

...has been around for a long time, and the last few years the sound engine of Kobo Deluxe has been known by this name.

Now, as the former Audiality is no longer maintained, and the new sound engine in development for Kobo II has much more potential, it has been decided to yet again recycle the name.

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
  * GitHub
    * git clone git@github.com:olofson/audiality2.git
  
* Configure the source tree.
  * Option 1 (Un*x with bash or similar shell)
    * ./cfg-all
  * Option 2 (Using CMake directly)
    * mkdir build
    * cd build
    * cmake ..

* Build and install.
  * Enter the desired build directory. (cfg-all creates a few different ones under "build".)
  * make
  * sudo make install

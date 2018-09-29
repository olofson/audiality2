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

Documentation
-------------

HTML ([Markdeep](https://casual-effects.com/markdeep/)) documentation is found in 'docs', and can be viewed online [here](https://olofson.github.io/audiality2/).

The name Audiality...
---------------------

...has been around for a long time, and the last few years the sound engine of Kobo Deluxe has been known by this name.

Now, as the former Audiality is no longer maintained, and the new sound engine in development for Kobo II has much more potential, it has been decided to yet again recycle the name.

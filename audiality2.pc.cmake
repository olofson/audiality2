#
# pkgconfig for Audiality 2
#

prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${exec_prefix}
includedir=${prefix}/include/Audiality2

Name: Audiality2
Description: Audiality 2 - a realtime script driven sound synthesis and audio engine.
Version: @VERSION_MAJOR@.@VERSION_MINOR@.@VERSION_PATCH@
Requires: @A2_PC_REQUIRES@
Libs: -L${libdir} -laudiality2
Libs.private: -L${libdir} @A2_PC_LIBS@
Cflags: -I${includedir}

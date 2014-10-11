#! /bin/sh

echo "=========================================================="
echo "Building a2test as JS for browsers, using Emscripten..."
echo "=========================================================="

BUILDDIR=$(pwd)/build
if [ ! -e $BUILDDIR ]; then
	mkdir $BUILDDIR
fi
if [ ! -e $BUILDDIR/emscripten ]; then
	mkdir $BUILDDIR/emscripten
fi
BUILDDIR=$BUILDDIR/emscripten

# Build the Audiality 2 library
LIBSOURCES="src/audiality2.c src/platform.c src/core.c src/stream.c"
LIBSOURCES="$LIBSOURCES src/audiality2.c src/platform.c src/core.c"
LIBSOURCES="$LIBSOURCES src/stream.c src/waves.c src/bank.c src/api.c"
LIBSOURCES="$LIBSOURCES src/xinsertapi.c src/properties.c src/compiler.c"
LIBSOURCES="$LIBSOURCES src/drivers.c src/utilities.c src/render.c"
LIBSOURCES="$LIBSOURCES src/rchm.c src/sfifo.c"

LIBSOURCES="$LIBSOURCES src/units/wtosc.c src/units/panmix.c"
LIBSOURCES="$LIBSOURCES src/units/inline.c src/units/xsink.c"
LIBSOURCES="$LIBSOURCES src/units/xsource.c src/units/xinsert.c"
LIBSOURCES="$LIBSOURCES src/units/dbgunit.c src/units/limiter.c"
LIBSOURCES="$LIBSOURCES src/units/fbdelay.c src/units/filter12.c"
LIBSOURCES="$LIBSOURCES src/units/dcblock.c src/units/waveshaper.c"

LIBSOURCES="$LIBSOURCES src/drivers/sdldrv.c src/drivers/jackdrv.c"
LIBSOURCES="$LIBSOURCES src/drivers/bufferdrv.c src/drivers/dummydrv.c"
LIBSOURCES="$LIBSOURCES src/drivers/mallocdrv.c"

echo "Building Audiality 2 library..."
emcc -O2 -DA2_HAVE_SDL -Iinclude -Isrc -Isrc/units -Isrc/drivers $LIBSOURCES -o $BUILDDIR/libaudiality2.bc

echo "Building a2test..."
emcc -O2 -Iinclude --preload-file "test/data@/data" test/a2test.c test/gui.c $BUILDDIR/libaudiality2.bc -o $BUILDDIR/a2test.html

echo
echo "=========================================================="
echo "Done!"
echo "=========================================================="

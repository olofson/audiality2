include(CheckIncludeFiles)

include_directories(${AUDIALITY2_BINARY_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/src)
include_directories(${AUDIALITY2_SOURCE_DIR}/src/units)
include_directories(${AUDIALITY2_SOURCE_DIR}/src/drivers)


# Core files
set(sources
	audiality2.c
	platform.c
	core.c
	units.c
	stream.c
	waves.c
	bank.c
	api.c
	xinsertapi.c
	properties.c
	compiler.c
	drivers.c
	utilities.c
	render.c
	rchm.c
	sfifo.c
	error.c
)

# Voice units
set(sources ${sources}
	units/wtosc.c
	units/panmix.c
	units/inline.c
	units/xsink.c
	units/xsource.c
	units/xinsert.c
	units/dbgunit.c
	units/limiter.c
	units/fbdelay.c
	units/filter12.c
	units/dcblock.c
	units/waveshaper.c
	units/fm.c
)

# Drivers
set(sources ${sources}
	drivers/sdldrv.c
	drivers/jackdrv.c
	drivers/bufferdrv.c
	drivers/dummydrv.c
	drivers/mallocdrv.c
	drivers/coreaudiodrv.c
)

if(SDL_FOUND)
	add_definitions(-DA2_HAVE_SDL)
	include_directories(${SDL_INCLUDE_DIR})
endif(SDL_FOUND)

CHECK_INCLUDE_FILES(jack/jack.h HAVE_JACK_H)
find_package(Threads)

#find_package(ALSA)
#if(ALSA_FOUND)
#	add_definitions(-DA2_HAVE_ALSA)
#endif(ALSA_FOUND)


add_library(audiality2 ${sources})

if(APPLE)
	find_library(CORE_AUDIO CoreAudio)
	find_library(AUDIO_TOOLBOX AudioToolbox)

	target_link_libraries(audiality2 ${CORE_AUDIO} ${AUDIO_TOOLBOX})
endif(APPLE)

if(UNIX)
	target_link_libraries(audiality2 m)
endif(UNIX)

if(SDL_FOUND)
	target_link_libraries(audiality2 ${SDL_LIBRARY})
endif(SDL_FOUND)

if(HAVE_JACK_H AND CMAKE_DL_LIBS AND Threads_FOUND)
	add_definitions(-DA2_HAVE_JACK)
	target_link_libraries(audiality2 ${CMAKE_DL_LIBS})
	target_link_libraries(audiality2 ${CMAKE_THREAD_LIBS_INIT})
endif()

if(WIN32)
	target_link_libraries(audiality2 -lwinmm -ldxguid)
endif(WIN32)

# Until 2.0, we assume there's no ABI compatibility across releases!
set_target_properties(audiality2 PROPERTIES VERSION ${VERSION_STRING}
	SOVERSION ${VERSION_STRING})

# Release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

set(f "-Wall -Wwrite-strings -Wcast-align")
set(f "${f} -Wbad-function-cast -Waggregate-return")
set(f "${f} -Wstrict-prototypes -fno-builtin -Wshadow")
set(f "${f} -Wdeclaration-after-statement -Wmissing-prototypes")
set(f "${f} -Wmissing-declarations -Wdisabled-optimization")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
	set(f "${f} -Wincompatible-pointer-types")
endif()

# Debug build: No optimizations, extra debug features, don't fail on warnings
set(CMAKE_C_FLAGS_DEBUG "${f} -O0 -g -DDEBUG")

# Maintainer build: Few optimizations, lots of warnings, fail on warnings
set(CMAKE_C_FLAGS_MAINTAINER "${f} -O1 -g -Werror")

install(TARGETS audiality2 DESTINATION lib)

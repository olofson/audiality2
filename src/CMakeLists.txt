INCLUDE (CheckIncludeFiles)

include_directories(${AUDIALITY2_SOURCE_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/src)
include_directories(${AUDIALITY2_SOURCE_DIR}/src/units)
include_directories(${AUDIALITY2_SOURCE_DIR}/src/drivers)


# Core files
set(sources
	audiality2.c
	core.c
	waves.c
	bank.c
	api.c
	compiler.c
	drivers.c
	utilities.c
	rchm.c
	sfifo.c
)

# Voice units
set(sources ${sources}
	units/wtosc.c
	units/panmix.c
	units/inline.c
	units/xinsert.c
	units/dbgunit.c
	units/limiter.c
	units/fbdelay.c
	units/filter12.c
)

# Drivers
set(sources ${sources}
	drivers/sdldrv.c
	drivers/jackdrv.c
	drivers/streamdrv.c
	drivers/dummydrv.c
	drivers/mallocdrv.c
)


find_package(SDL)
if(SDL_FOUND)
	add_definitions(-DA2_HAVE_SDL)
	include_directories(${SDL_INCLUDE_DIR})
	link_libraries(${SDL_LIBRARY})
endif(SDL_FOUND)

CHECK_INCLUDE_FILES(jack/jack.h HAVE_JACK_H)
if(HAVE_JACK_H)
	add_definitions(-DA2_HAVE_JACK)
endif(HAVE_JACK_H)

#find_package(ALSA)
#if(ALSA_FOUND)
#	add_definitions(-DA2_HAVE_ALSA)
#endif(ALSA_FOUND)


add_library(audiality2 SHARED ${sources})
target_link_libraries(audiality2 m ${SDL_LIBRARY})

#add_library(audiality2 STATIC ${sources})
#target_link_libraries(audiality2 m)
#if(WIN32)
#	add_library(audiality2 STATIC ${sources})
#	target_link_libraries(audiality2 m ${SDL_LIBRARY} -lwinmm -ldxguid)
#else(WIN32)
#	add_library(audiality2 SHARED ${sources})
#	target_link_libraries(audiality2 m ${SDL_LIBRARY})
#endif(WIN32)


# Release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Maintainer build: No optimizations, lots of warnings, fail on warnings
set(f "-O0 -g -Wall -Wwrite-strings -Wcast-align")
set(f "${f} -Wbad-function-cast -Waggregate-return")
set(f "${f} -Wstrict-prototypes -fno-builtin -Wshadow")
set(f "${f} -Wdeclaration-after-statement -Wmissing-prototypes")
set(f "${f} -Wmissing-declarations -Wdisabled-optimization")
set(CMAKE_C_FLAGS_MAINTAINER "${f} -Werror")

# Debug build: Maintainer + extra debug features, don't fail on warnings
set(f "${f} -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG ${f})

install(TARGETS audiality2 DESTINATION lib)

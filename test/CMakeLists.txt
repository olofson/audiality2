# For stand-alone build, linking to the system A2 library
#find_package(Audiality2 REQUIRED)
#include_directories(${AUDIALITY2_INCLUDE_DIR})

# For local build, not relying on an installed library
include_directories(${AUDIALITY2_SOURCE_DIR}/include)
link_directories(${AUDIALITY2_BINARY_DIR})

set(AUDIALITY2_LIBRARY audiality2)

add_executable(rtsubtest WIN32 MACOSX_BUNDLE rtsubtest.c)
target_link_libraries(rtsubtest ${AUDIALITY2_LIBRARY})

find_package(SDL)
if(SDL_FOUND)
	include_directories(${SDL_INCLUDE_DIR})
	link_libraries(${SDL_LIBRARY} SDLmain)
	add_executable(a2test WIN32 MACOSX_BUNDLE a2test.c gui.c)
	target_link_libraries(a2test ${AUDIALITY2_LIBRARY})
endif(SDL_FOUND)

# Release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Maintainer build: No optimizations, lots of warnings, fail on warnings
set(f "-O1 -g -DDEBUG -Wall -Werror -Wwrite-strings -Wcast-align")
set(f "${f} -Wbad-function-cast -Waggregate-return")
set(f "${f} -Wstrict-prototypes -fno-builtin -Wshadow")
set(f "${f} -Wdeclaration-after-statement -Wmissing-prototypes")
set(f "${f} -Wmissing-declarations -Wdisabled-optimization")
set(CMAKE_C_FLAGS_MAINTAINER ${f})

# Debug build: Maintainer + extra debug features, don't fail on warnings
set(f "${f} -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG ${f})

# For stand-alone build, linking to the system A2 library
#find_package(Audiality2 REQUIRED)
#include_directories(${AUDIALITY2_INCLUDE_DIR})

# For local build, not relying on an installed library
include_directories(${AUDIALITY2_BINARY_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/include)
link_directories(${AUDIALITY2_BINARY_DIR})

set(AUDIALITY2_LIBRARY audiality2)

add_executable(waveupload WIN32 MACOSX_BUNDLE waveupload.c)
target_link_libraries(waveupload ${AUDIALITY2_LIBRARY})

add_executable(rtsubstate WIN32 MACOSX_BUNDLE rtsubstate.c)
target_link_libraries(rtsubstate ${AUDIALITY2_LIBRARY})

add_executable(renderwave WIN32 MACOSX_BUNDLE renderwave.c)
target_link_libraries(renderwave ${AUDIALITY2_LIBRARY})

add_executable(renderwave2 WIN32 MACOSX_BUNDLE renderwave2.c)
target_link_libraries(renderwave2 ${AUDIALITY2_LIBRARY})

add_executable(streamupload WIN32 MACOSX_BUNDLE streamupload.c)
target_link_libraries(streamupload ${AUDIALITY2_LIBRARY})

add_executable(wavestress WIN32 MACOSX_BUNDLE wavestress.c)
target_link_libraries(wavestress ${AUDIALITY2_LIBRARY})

add_executable(wavestress2 WIN32 MACOSX_BUNDLE wavestress2.c)
target_link_libraries(wavestress2 ${AUDIALITY2_LIBRARY})

add_executable(voicestress WIN32 MACOSX_BUNDLE voicestress.c)
target_link_libraries(voicestress ${AUDIALITY2_LIBRARY})

add_executable(streamtest WIN32 MACOSX_BUNDLE streamtest.c)
target_link_libraries(streamtest ${AUDIALITY2_LIBRARY})

add_executable(streamstress WIN32 MACOSX_BUNDLE streamstress.c)
target_link_libraries(streamstress ${AUDIALITY2_LIBRARY})

add_executable(timingtest WIN32 MACOSX_BUNDLE timingtest.c)
target_link_libraries(timingtest ${AUDIALITY2_LIBRARY})

if(SDL_FOUND)
	include_directories(${SDL_INCLUDE_DIR})

	add_executable(a2test WIN32 MACOSX_BUNDLE a2test.c gui.c)
	target_link_libraries(a2test ${AUDIALITY2_LIBRARY} SDLmain)

	add_executable(apistress WIN32 MACOSX_BUNDLE apistress.c)
	target_link_libraries(apistress ${AUDIALITY2_LIBRARY} SDLmain)
elseif(SDL2_FOUND)
	include_directories(${SDL2_INCLUDE_DIRS})

	add_executable(a2test WIN32 MACOSX_BUNDLE a2test.c gui.c)
	target_link_libraries(a2test ${AUDIALITY2_LIBRARY})

	add_executable(apistress WIN32 MACOSX_BUNDLE apistress.c)
	target_link_libraries(apistress ${AUDIALITY2_LIBRARY})
endif(SDL_FOUND)

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

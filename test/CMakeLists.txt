# For stand-alone build, linking to the system A2 library
#find_package(Audiality2 REQUIRED)
#include_directories(${AUDIALITY2_INCLUDE_DIR})

# For local build, not relying on an installed library
include_directories(${AUDIALITY2_BINARY_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/include)
link_directories(${AUDIALITY2_BINARY_DIR})
set(AUDIALITY2_LIBRARIES audiality2 ${AUDIALITY2_EXTRA_LIBRARIES})

function(a2_add_test testname)
	add_executable(${testname} WIN32 MACOSX_BUNDLE ${testname}.c ${ARGN})
	target_link_libraries(${testname} ${AUDIALITY2_LIBRARIES})
	if(FORCE_32BIT)
		set_target_properties(${testname} PROPERTIES
			COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
	endif(FORCE_32BIT)
endfunction(a2_add_test)

a2_add_test(waveupload)
a2_add_test(rtsubstate)
a2_add_test(renderwave)
a2_add_test(renderwave2)
a2_add_test(streamupload)
a2_add_test(wavestress)
a2_add_test(wavestress2)
a2_add_test(voicestress)
a2_add_test(streamtest)
a2_add_test(streamstress)
a2_add_test(timingtest)

if(SDL_FOUND)
	include_directories(${SDL_INCLUDE_DIR})
	a2_add_test(a2test gui.c)
	target_link_libraries(a2test SDLmain)
	a2_add_test(apistress)
	target_link_libraries(apistress SDLmain)
elseif(SDL2_FOUND)
	include_directories(${SDL2_INCLUDE_DIRS})
	a2_add_test(a2test gui.c)
	a2_add_test(apistress)
endif(SDL_FOUND)

# Release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Static release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_STATIC "-O3 -DNDEBUG")

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


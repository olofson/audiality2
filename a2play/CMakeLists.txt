# For stand-alone build, linking to the system A2 library
#find_package(Audiality2 REQUIRED)
#include_directories(${AUDIALITY2_INCLUDE_DIR})

# For local build, not relying on an installed library
include_directories(${AUDIALITY2_BINARY_DIR}/include)
include_directories(${AUDIALITY2_SOURCE_DIR}/include)
link_directories(${AUDIALITY2_BINARY_DIR})

set(AUDIALITY2_LIBRARY audiality2)

add_executable(a2play WIN32 MACOSX_BUNDLE a2play.c)
target_link_libraries(a2play ${AUDIALITY2_LIBRARY})

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

install(TARGETS a2play DESTINATION bin)

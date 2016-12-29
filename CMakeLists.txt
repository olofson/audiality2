cmake_minimum_required(VERSION 2.8)

project(AUDIALITY2)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 9)
set(VERSION_PATCH 1)
set(VERSION_BUILD 0)

set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(PACKAGE "audiality2-${VERSION_STRING}")

option(BUILD_SHARED_LIBS "Build shared libraries." ON)
option(FORCE_32BIT "Force 32 bit build." OFF)
option(USE_SDL "Use SDL (1.2 or 2.0) if present." ON)
option(USE_ALSA "Use ALSA if present." ON)
option(USE_JACK "Use JACK if present." ON)
option(FORCE_SDL12 "Use SDL 1.2 even if SDL2 is present." OFF)

# For some reason, we can't call find_package(SDL) more than once in one
# project with MXE, so we need to do this on the top level... (Both the SDL
# driver and some of the tests will use SDL when available.)
#
# Also, since FindSDL2.cmake still doesn't seem to be included in distros as
# I'm writing this, we're sticking with pkg_search_module(), which relies on
# pkg-config instead of CMake specific scripts.

include(FindPkgConfig)

if(USE_SDL)
	if(NOT FORCE_SDL12)
		pkg_check_modules(SDL2 sdl2)
	endif(NOT FORCE_SDL12)
	if(NOT SDL2_FOUND)
		find_package(SDL)
	endif(NOT SDL2_FOUND)
endif(USE_SDL)

add_subdirectory(src)
add_subdirectory(a2play)
add_subdirectory(test)

configure_file(include/audiality2.h.cmake
	${AUDIALITY2_BINARY_DIR}/include/audiality2.h @ONLY)
install(FILES ${AUDIALITY2_BINARY_DIR}/include/audiality2.h
	DESTINATION include/Audiality2)
install(DIRECTORY include/ DESTINATION include/Audiality2
	FILES_MATCHING PATTERN "*.h")

configure_file(audiality2.pc.cmake ${AUDIALITY2_BINARY_DIR}/audiality2.pc
	@ONLY)
install(FILES ${AUDIALITY2_BINARY_DIR}/audiality2.pc
	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/")

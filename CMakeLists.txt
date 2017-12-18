cmake_minimum_required(VERSION 2.8)

project(AUDIALITY2)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 9)
set(VERSION_PATCH 4)
set(VERSION_BUILD 0)

set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(PACKAGE "audiality2-${VERSION_STRING}")

option(BUILD_SHARED_LIBS "Build shared libraries." ON)
option(FORCE_32BIT "Force 32 bit build." OFF)
option(USE_SDL "Use SDL (2.0) if present." ON)
option(USE_ALSA "Use ALSA if present." ON)
option(USE_JACK "Use JACK if present." ON)

# For some reason, we can't call find_package(SDL) more than once in one
# project with MXE, so we need to do this on the top level... (Both the SDL
# driver and some of the tests will use SDL when available.)
#
# Also, since FindSDL2.cmake still doesn't seem to be included in distros as
# I'm writing this, we're sticking with pkg_search_module(), which relies on
# pkg-config instead of CMake specific scripts.

include(FindPkgConfig)

if(USE_SDL)
	pkg_check_modules(SDL2 sdl2)
endif(USE_SDL)

if(SDL2_FOUND)
	set(A2_PC_REQUIRES "sdl2 >= 2.0.0")
	set(A2_PC_LIBS "-lSDL2")
else(SDL2_FOUND)
	set(A2_PC_REQUIRES "")
	set(A2_PC_LIBS "")
endif(SDL2_FOUND)

if(NOT WIN32 AND NOT BUILD_SHARED_LIBS)
	set(AUDIALITY2_EXTRA_LIBRARIES "${A2_PC_LIBS} -lpthread -ldl -lm")
endif()

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

# CPack general
set(CPACK_GENERATOR "DEB" "RPM" "TBZ2")
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_RELOCATABLE "false")
set(CPACK_PACKAGE_NAME "audiality2")
set(CPACK_PACKAGE_VERSION "${VERSION_STRING}")
set(CPACK_PACKAGE_CONTACT "David Olofson <david@olofson.net>")
set(CPACK_RESOURCE_FILE_LICENSE "${AUDIALITY2_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${AUDIALITY2_SOURCE_DIR}/README.md")

# CPack DEB
set(CPACK_DEBIAN_PACKAGE_SECTION "Libraries")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION_SUMMARY "Realtime scripted modular audio engine")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Audiality 2 is a realtime audio and music engine, primarily intended for video games. While it supports traditional sample playback as well as additive, subtractive and granular synthesis, the distinctive feature is subsample accurate realtime scripting.")

# CPack PRM
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_SUMMARY "${CPACK_DEBIAN_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_DEBIAN_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_PACKAGE_LICENSE "zlib")
set(CPACK_RPM_PACKAGE_REQUIRES "libSDL2-2.0.so.0,libm.so.6")

include(CPack)

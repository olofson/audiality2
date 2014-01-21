cmake_minimum_required(VERSION 2.8)

project(AUDIALITY2)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 9)
set(VERSION_PATCH 1)
set(VERSION_BUILD 0)

set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(PACKAGE "audiality2-${VERSION_STRING}")

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(a2play)

configure_file(include/audiality2.h.cmake
	${CMAKE_SOURCE_DIR}/include/audiality2.h @ONLY)
install(DIRECTORY include/ DESTINATION include/Audiality2
	FILES_MATCHING PATTERN "*.h")

configure_file(audiality2.pc.cmake ${CMAKE_SOURCE_DIR}/audiality2.pc @ONLY)
install(FILES ${CMAKE_SOURCE_DIR}/audiality2.pc
	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/")

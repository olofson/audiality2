# Locate Audiality 2 library
#
# This module defines:
#  AUDIALITY2_FOUND, true if Audiality 2 was found
#  AUDIALITY2_INCLUDE_DIR, where the C headers are found
#  AUDIALITY2_LIBRARY, where the library is found
#
# Created by David Olofson

FIND_PATH(AUDIALITY2_INCLUDE_DIR audiality2.h
	PATH_SUFFIXES include
	PATHS
	~/Library/Frameworks
	/Library/Frameworks
	/usr/local/include/Audiality2
	/usr/include/Audiality2
	/sw/include/Audiality2
	/opt/local/include/Audiality2
	/opt/csw/include/Audiality2
	/opt/include/Audiality2
)

FIND_LIBRARY(AUDIALITY2_LIBRARY
	NAMES audiality2
	PATH_SUFFIXES lib64 lib
	PATHS
	~/Library/Frameworks
	/Library/Frameworks
	/usr/local
	/usr
	/sw
	/opt/local
	/opt/csw
	/opt
)

SET(AUDIALITY2_FOUND "NO")
IF(AUDIALITY2_LIBRARY AND AUDIALITY2_INCLUDE_DIR)
	SET(AUDIALITY2_FOUND "YES")
ENDIF(AUDIALITY2_LIBRARY AND AUDIALITY2_INCLUDE_DIR)

<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
    "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head>
<title>FindLZMA.cmake</title>
<style type="text/css">
.enscript-comment { font-style: italic; color: rgb(178,34,34); }
.enscript-function-name { font-weight: bold; color: rgb(0,0,255); }
.enscript-variable-name { font-weight: bold; color: rgb(184,134,11); }
.enscript-keyword { font-weight: bold; color: rgb(160,32,240); }
.enscript-reference { font-weight: bold; color: rgb(95,158,160); }
.enscript-string { font-weight: bold; color: rgb(188,143,143); }
.enscript-builtin { font-weight: bold; color: rgb(218,112,214); }
.enscript-type { font-weight: bold; color: rgb(34,139,34); }
.enscript-highlight { text-decoration: underline; color: 0; }
</style>
</head>
<body id="top">
<h1 style="margin:8px;" id="f1">FindLZMA.cmake&nbsp;&nbsp;&nbsp;<span style="font-weight: normal; font-size: 0.5em;">[<a href="?txt">plain text</a>]</span></h1>
<hr/>
<div></div>
<pre>
# - Find lzma and lzmadec
# Find the native LZMA includes and library
#
#  LZMA_INCLUDE_DIR    - where to find lzma.h, etc.
#  LZMA_LIBRARIES      - List of libraries when using liblzma.
#  LZMA_FOUND          - True if liblzma found.
#  LZMADEC_INCLUDE_DIR - where to find lzmadec.h, etc.
#  LZMADEC_LIBRARIES   - List of libraries when using liblzmadec.
#  LZMADEC_FOUND       - True if liblzmadec found.

IF (LZMA_INCLUDE_DIR)
  # Already in cache, be silent
  SET(LZMA_FIND_QUIETLY TRUE)
ENDIF (LZMA_INCLUDE_DIR)

FIND_PATH(LZMA_INCLUDE_DIR lzma.h)
FIND_LIBRARY(LZMA_LIBRARY NAMES lzma )

# handle the QUIETLY and REQUIRED arguments and set LZMA_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LZMA DEFAULT_MSG LZMA_LIBRARY LZMA_INCLUDE_DIR)

IF(LZMA_FOUND)
  SET( LZMA_LIBRARIES ${LZMA_LIBRARY} )
ELSE(LZMA_FOUND)
  SET( LZMA_LIBRARIES )

  IF (LZMADEC_INCLUDE_DIR)
    # Already in cache, be silent
    SET(LZMADEC_FIND_QUIETLY TRUE)
  ENDIF (LZMADEC_INCLUDE_DIR)

  FIND_PATH(LZMADEC_INCLUDE_DIR lzmadec.h)
  FIND_LIBRARY(LZMADEC_LIBRARY NAMES lzmadec )

  # handle the QUIETLY and REQUIRED arguments and set LZMADEC_FOUND to TRUE if 
  # all listed variables are TRUE
  INCLUDE(FindPackageHandleStandardArgs)
  FIND_PACKAGE_HANDLE_STANDARD_ARGS(LZMADEC DEFAULT_MSG LZMADEC_LIBRARY
    LZMADEC_INCLUDE_DIR)

  IF(LZMADEC_FOUND)
    SET( LZMADEC_LIBRARIES ${LZMADEC_LIBRARY} )
  ELSE(LZMADEC_FOUND)
    SET( LZMADEC_LIBRARIES )
  ENDIF(LZMADEC_FOUND)
ENDIF(LZMA_FOUND)


MARK_AS_ADVANCED( LZMA_LIBRARY LZMA_INCLUDE_DIR
  LZMADEC_LIBRARY LZMADEC_INCLUDE_DIR )
</pre>
<hr />
</body></html>
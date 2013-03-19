# CMake file for shaman /shaman/src/shaman/lzma/CMakeLists.txt
# Created on: 04.03.2013
#    Author: zmij

cmake_minimum_required(VERSION 2.6)

if (NOT LZMA_SDK)
	find_path(
		LZMA_SDK
		LzmaEnc.c
		HINTS ${LZMA_INCLUDE_DIR}/lzma
	)
endif()

if (NOT LZMA_SDK)
	message(FATAL_ERROR "LZMA SDK directory not found. Please specify directory containing LZMA_SDK")
endif()

include_directories(
	${LZMA_SDK}
)

add_definitions("-std=c++0x")
add_definitions("-D_7ZIP_ST")

set(
	SHAMAN_LZMA_SRCS
	lzma.cpp
	detail/lzma_encoder.cpp
	detail/match_finder.cpp
	
	LzmaEnc.c
	${LZMA_SDK}/LzmaDec.c
	${LZMA_SDK}/LzFind.c
)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../..
)

add_library(
	shaman_lzma
	${SHAMAN_LZMA_SRCS}
)

## Test programs

# temporary
add_executable(
	lzma-encode
	lzma-encode.cpp
)

target_link_libraries(
	lzma-encode
	shaman_lzma
)

add_executable(
	lzma-decompress
	lzma-decompress.cpp
)

target_link_libraries(
	lzma-decompress
	shaman_lzma
)

add_executable(
	lzma-compress
	lzma-compress.cpp
)

target_link_libraries(
	lzma-compress
	shaman_lzma
)

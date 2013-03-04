# CMake file for shaman /shaman/src/shaman/lzma/CMakeLists.txt
# Created on: 04.03.2013
#    Author: zmij

cmake_minimum_required(VERSION 2.6)

set(
	SHAMAN_LZMA_SRCS
	lzma.cpp
)

add_library(
	shaman_lzma
	${SHAMAN_LZMA_SRCS}
)

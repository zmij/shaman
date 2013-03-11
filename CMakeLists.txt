# CMake file for shaman
# Created on: 04.03.2013
#    Author: zmij

cmake_minimum_required(VERSION 2.6)

project(shaman)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

if (NOT LZMA_DIR)
	set(
		LZMA_DIR
		${PROJECT_SOURCE_DIR}
	)
endif()

find_package(LZMA REQUIRED)
add_definitions("-std=c++0x")

add_subdirectory(src)

option(BUILD_TESTS "Build test programs and create \"test\" make target" ON)

if (BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

# CMake file for shaman
# Created on: 04.03.2013
#    Author: zmij

cmake_minimum_required(VERSION 2.6)

project(shaman)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

add_definitions("-std=c++0x")

add_subdirectory(src)

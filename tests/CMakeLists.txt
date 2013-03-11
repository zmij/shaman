# CMake file for shaman /shaman/src/tests/CMakeLists.txt
# Created on: 04.03.2013
#    Author: zmij

cmake_minimum_required(VERSION 2.6)

find_program(
	LZMA_UTIL
	NAMES lzma
)

if (LZMA_UTIL)
	message(STATUS "Found lzma util ${LZMA_UTIL}")
	
	set(
		TEST_UNCOMPRESSED_FILE lipsum.txt
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${TEST_UNCOMPRESSED_FILE}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_UNCOMPRESSED_FILE}
		COMMENT "Copying uncompressed test data to build tree"
		COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_UNCOMPRESSED_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${TEST_UNCOMPRESSED_FILE}
	)
	
	add_custom_target(
		${TEST_UNCOMPRESSED_FILE}.lz ALL
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${TEST_UNCOMPRESSED_FILE}
		COMMENT "Compressing test data"
		COMMAND rm -f ${TEST_UNCOMPRESSED_FILE}.lz && ${LZMA_UTIL} -z -k -S.lz ${TEST_UNCOMPRESSED_FILE}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} 
	)
	
	set(
		TEST_UNCOMPRESSED_FILE
		${CMAKE_CURRENT_BINARY_DIR}/${TEST_UNCOMPRESSED_FILE}
	)
	set(
		TEST_COMPRESSED_FILE
		${TEST_UNCOMPRESSED_FILE}.lz
	)
endif()

include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${PROJECT_SOURCE_DIR}/src
)

configure_file(
	config.in.hpp
	config.hpp
)

add_definitions("-D_7ZIP_ST")

# Unit tests
set (Boost_USE_STATIC_LIBS ON)
find_package (Boost COMPONENTS unit_test_framework REQUIRED)

set(
	TEST_LZMA test_lzma
)

set(
	TEST_LZMA_SRC
	lzma-test.cpp
)

add_executable(
	${TEST_LZMA}
	${TEST_LZMA_SRC}
)

target_link_libraries(
	${TEST_LZMA}
	${Boost_LIBRARIES}
	shaman_lzma
)

add_test(
	NAME lzma
	COMMAND ${TEST_LZMA}
)

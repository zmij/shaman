/**
 * @file lzma-test.cpp
 *
 * Unit tests for boost::iostreams-based filter for lzma compression/decompression
 *
 *  Created on: 11.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */


//#define BOOST_TEST_MAIN
#define BOOST_TEST_MODULE testLZMA

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <shaman/lzma/lzma.hpp>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>

#include "config.hpp"

BOOST_AUTO_TEST_CASE( CheckLZMADecompression )
{
	namespace iostreams = boost::iostreams;

	BOOST_REQUIRE_MESSAGE(!shaman::test::COMPRESSED_FILE.empty(),
			"Test file with compressed data is not configured");
	BOOST_REQUIRE_MESSAGE(!shaman::test::UNCOMPRESSED_FILE.empty(),
			"Test file with uncompressed data is not configured");

	// Load ehtalon test data
	std::ifstream uncompressed_file( shaman::test::UNCOMPRESSED_FILE );
	std::ostringstream ethalon;
	iostreams::copy( uncompressed_file, ethalon );

	std::string const& ethalon_str = ethalon.str();

	BOOST_REQUIRE_MESSAGE( !ethalon_str.empty(),
			"Ethalon uncompressed file is empty" );

	std::ifstream compressed_file( shaman::test::COMPRESSED_FILE );

	iostreams::filtering_istreambuf sbuf;
	sbuf.push( shaman::lzma::lzma_decompressor() );
	sbuf.push( compressed_file );

	std::ostringstream inflated;
	iostreams::copy( sbuf, inflated );

	std::string const& inflated_str = inflated.str();

	BOOST_REQUIRE_EQUAL( inflated_str.size(), ethalon_str.size() );
	BOOST_REQUIRE_EQUAL( inflated_str, ethalon_str );
}

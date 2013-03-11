/**
 * @file lzma-decompress.cpp
 * Test program to demonstrate LZMA decompression using shaman::lzma::lzma_decompressor.
 * Takes one argument - file name, decompresses it and writes result to standard
 * output
 *  Created on: 11.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <iostream>
#include <fstream>

#include <shaman/lzma/lzma.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>

namespace iostreams = boost::iostreams;

int
main(int argc, char* argv[]) {
	if (argc < 2) {
		std::cerr << "Usage " << argv[0] << " <input-file>\n";
		return 1;
	}
	try {
		std::ifstream ifile(argv[1]);

		iostreams::filtering_istreambuf sbuf;
		sbuf.push( shaman::lzma::lzma_decompressor() );
		sbuf.push( ifile );

		iostreams::copy( sbuf, std::cout );

	} catch ( std::exception const& e) {
		std::cerr << "Standard exception " << e.what() << "\n";
		return 1;
	} catch (...) {
		std::cerr << "Unexpected exception\n";
		return 1;
	}
	return 0;
}




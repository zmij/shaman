LZMA boost::iostreams::filter

C++ library with a filter implementing boost::iostreams::filter concept to 
compress/decompress data in a stream or stream buffer using LZMA algorithm

CURRENT STATUS
 
Only decompression is implemented

LICENSE

This library is intended to be distributed under Boost license,
please see http://www.boost.org/LICENSE_1_0.txt , 
http://www.boost.org/users/license.html

BUILDING

The library requires LZMA SDK to be built (LzmaDec.c, LzmaEnc.c and LzFind.c)
version 9.20.
You can obtain it at 7-Zip official site http://www.7-zip.org.

You can use provided CMake file to build the library or manually build a library
from following files:

(LZMA_SDK)/LzmaEnc.c
(LZMA_SDK)/LzmaDec.c
(LZMA_SDK)/LzFind.c
lzma.cpp

USAGE

Please see documentation for boost::iostreams 
(http://www.boost.org/doc/libs/1_53_0/libs/iostreams/doc/index.html) on general
boost::iostreams usage.

Decompression example:
see lzma-decompress.cpp

#include <shaman/lzma/lzma.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>

namespace iostreams = boost::iostreams;

std::ifstream ifile("filename");

iostreams::filtering_istreambuf sbuf;
sbuf.push( shaman::lzma::lzma_decompressor() );
sbuf.push( ifile );

// Use the filtering stream buffer to read decompressed data

Compression example:
see lzma-compress.cpp

WARNING! NOT IMPLEMENTED YET!
 
#include <shaman/lzma/lzma.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>

namespace iostreams = boost::iostreams;

std::ifstream ifile("filename");

iostreams::filtering_istreambuf sbuf;
sbuf.push( shaman::lzma::lzma_compressor() );
sbuf.push( ifile );

// Use the filtering stream buffer to read compressed data
 
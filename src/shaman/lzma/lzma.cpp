/**
 * @file lzma.cpp
 *
 *  Created on: 04.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <shaman/lzma/lzma.hpp>
#include <lzma/LzmaEnc.h>
#include <lzma/LzmaDec.h>

namespace shaman {
namespace lzma {

//------------------Implementation of lzma_error------------------------------//
lzma_error::lzma_error(int error) :
		BOOST_IOSTREAMS_FAILURE("LZMA error"), error_(error)
{
}

void
lzma_error::check BOOST_PREVENT_MACRO_SUBSTITUTION(int error)
{
	switch (error) {
		case SZ_OK:
			break;
		case SZ_ERROR_MEM:
			boost::throw_exception(std::bad_alloc());
			break;
		default:
			boost::throw_exception(lzma_error(error));
			break;
	}
}

namespace detail {

lzma_base::lzma_base() :
		stream_(nullptr)
{
}

lzma_base::~lzma_base()
{
}

void
lzma_base::before( const char*& src_begin, const char* src_end,
             char*& dest_begin, char* dest_end )
{
	// TODO Initialize lzma buffers
}

void
lzma_base::after( const char*& src_begin, char*& dest_begin,
            bool compress )
{
	// TODO Get lzma buffers state
}

int
lzma_base::compress(int flush)
{
	// TODO Call compress function
	return SZ_OK;
}

int
lzma_base::decompress(int flush)
{
	// TODO Call decompress function
	return SZ_OK;
}

void
lzma_base::reset(bool compress, bool realloc)
{
	// TODO Cleanup
}


} // namespace detail
} // namespace lzma
} // namespace shaman

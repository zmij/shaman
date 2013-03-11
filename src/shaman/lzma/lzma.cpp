/**
 * @file lzma.cpp
 *
 *  Created on: 04.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <shaman/lzma/lzma.hpp>
#include <lzma/LzmaEnc.h>
#include <lzma/LzmaDec.h>

// TODO Remove this include
#include <iostream>

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

namespace {

/**
 * LZMA facade for standard allocator
 */
struct sz_alloc_impl : ISzAlloc {
	void* derived;

	lzma::lzma_alloc_func alloc_func;
	lzma::lzma_free_func  free_func;

	sz_alloc_impl(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff
		) :
		derived(d),
		alloc_func(af),
		free_func(ff)
	{
		Alloc =
			[](void* p, size_t size) -> void*
			{
				sz_alloc_impl* a = static_cast< sz_alloc_impl* >(p);
				return a->alloc_func(a->derived, 1, size);
			};
		Free =
			[](void* p, void* address)
			{
				sz_alloc_impl* a = static_cast< sz_alloc_impl* >(p);
				a->free_func(a->derived, address);
			};
	}
};

} // namespace

struct lzma_state_base {
	sz_alloc_impl alloc_;
	bool initialized_;

	lzma_state_base(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff) :
		alloc_(d, af, ff),
		initialized_(false)
	{
	}

	virtual
	~lzma_state_base()
	{
	}
};

struct lzma_encode_state : lzma_state_base {
	lzma_encode_state(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff) :
		lzma_state_base(d, af, ff)
	{
	}

	virtual
	~lzma_encode_state()
	{
	}
};

struct lzma_decode_state : lzma_state_base {
	CLzmaDec state;

	lzma_decode_state(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff) :
		lzma_state_base(d, af, ff)
	{
		LzmaDec_Construct(&state);
	}
	virtual
	~lzma_decode_state()
	{
		//std::cerr << "Destroy decode state\n";
		LzmaDec_Free(&state, &alloc_);
	}
};

//----------------------------------------------------------------------------//
//------------------lzma_base implementation (compressor)---------------------//
lzma_base<true>::lzma_base() :
		state_(nullptr)
{
}
//
lzma_base<true>::~lzma_base()
{
	delete state_;
}

void
lzma_base<true>::do_init( lzma_params const& p,
	#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
		lzma::lzma_alloc_func af,
		lzma::lzma_free_func ff,
	#endif
		void* derived
)
{
	// TODO Do smth with the workaround
	state_ = new lzma_encode_state(derived, af, ff);
}

bool
lzma_base<true>::filter(const char*& src_begin, const char* src_end,
		char*& dest_begin, char* dest_end, bool flush)
{
	// TODO Implement compression
	return false;
}

void
lzma_base<true>::reset(bool realloc)
{
	lzma_encode_state* old = state_;
	if (realloc) {
		state_ = new lzma_encode_state(old->alloc_.derived,
				old->alloc_.alloc_func, old->alloc_.free_func);
	} else {
		state_ = nullptr;
	}
	delete old;
}

//----------------------------------------------------------------------------//
//------------------lzma_base implementation (decompressor)-------------------//
lzma_base<false>::lzma_base() :
	state_(nullptr)
{
}

lzma_base<false>::~lzma_base()
{
	delete state_;
}

void
lzma_base<false>::do_init( lzma_params const& p,
	#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
		lzma::lzma_alloc_func af,
		lzma::lzma_free_func ff,
	#endif
		void* derived
)
{
	// TODO Do smth with the workaround
	state_ = new lzma_decode_state(derived, af, ff);
}

bool
lzma_base<false>::filter(const char*& src_begin, const char* src_end,
		char*& dest_begin, char* dest_end, bool /*flush*/)
{
	size_t in_sz = src_end - src_begin;
	size_t out_sz = dest_end - dest_begin;

	if (!state_->initialized_) {
		// Read header from stream (LZMA properties 5 bytes, uncompressed size 8 bytes, little endian)
		unsigned char header[LZMA_PROPS_SIZE + 8];

		memcpy(&header, src_begin, sizeof(header));
		src_begin += sizeof(header);
		in_sz -= sizeof(header);

		int result = LzmaDec_Allocate(&state_->state, header, LZMA_PROPS_SIZE, &state_->alloc_);
		lzma_error::check BOOST_PREVENT_MACRO_SUBSTITUTION(result);

		LzmaDec_Init(&state_->state);

		state_->initialized_ = true;
	}

	ELzmaStatus status;
	int result = LzmaDec_DecodeToBuf(&state_->state,
			reinterpret_cast<unsigned char*>(dest_begin), &out_sz,
			reinterpret_cast<unsigned char const*>(src_begin), &in_sz,
			LZMA_FINISH_ANY, &status);

	lzma_error::check BOOST_PREVENT_MACRO_SUBSTITUTION(result);

	src_begin += in_sz;
	dest_begin += out_sz;

	// TODO Detect eof

	return src_begin != src_end;
}

void
lzma_base<false>::reset(bool realloc)
{
	lzma_decode_state* old = state_;
	if (realloc) {
		state_ = new lzma_decode_state(old->alloc_.derived,
				old->alloc_.alloc_func, old->alloc_.free_func);
	} else {
		state_ = nullptr;
	}
	delete old;
}

} // namespace detail
} // namespace lzma
} // namespace shaman

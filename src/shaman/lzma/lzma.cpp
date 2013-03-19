/**
 * @file lzma.cpp
 *
 *  Created on: 04.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <shaman/lzma/lzma.hpp>
#include <lzma/LzmaEnc.h>
#include <lzma/LzmaDec.h>

#include <shaman/lzma/detail/lzma_encoder.hpp>

// TODO Remove this include
#include <iostream>

namespace shaman {
namespace lzma {

const uint PROPERTIES_SIZE = LZMA_PROPS_SIZE;

//------------------Implementation of lzma_params-----------------------------//
lzma_params::lzma_params(
			ulong sz,
			uint level_,
			lzma_algo algo_,
			lzma_mode mode_,
			uint lc_,
			uint lp_,
			uint pb_,
			uint fb_,
			uint hbs,
			uint mfc,
			uint ds
		) :
		size_estimate(sz),
		level(level_),
		algo(algo_),
		mode(mode_),
		lc(lc_),
		lp(lp_),
		pb(pb_),
		fb(fb_),
		hash_bytes_size(hbs),
		match_finder_cycles(mfc),
		dict_size()
{
	// Perform parameters normalization
	if (ds == 0)
		dict_size = level <= 5 ? (1 << (level * 2 + 14)) : (level == 6 ? (1 << 25) : (1 << 26));
	else
		dict_size = ds;

	if (fb.defaulted()) fb = (level < 7 ? 32 : 64);
	if (match_finder_cycles.defaulted()) {
		match_finder_cycles = (16 + (fb >> 1)) >> (mode == LZMA_HASH_CHAIN ? 0 : 1);
	}
}

void
lzma_params::write( char*& dest_begin, char* dest_end ) const
{
	size_t size = dest_end - dest_begin;
	if (size < PROPERTIES_SIZE + sizeof(ulong))
		throw std::runtime_error("Buffer size is too small for LZMA properties");

	*dest_begin++ = (byte)(( pb * 5 + lp ) * 9 + lc);
	uint ds = dict_size;
	for (uint i = 11; i <= 30; ++i) {
		if (ds <= (uint)(2 << i)) {
			ds = 2 << i;
			break;
		}
		if (ds <= (uint)(3 << i)) {
			ds = 3 << i;
			break;
		}
	}

	for (uint i = 0; i < PROPERTIES_SIZE - 1; ++i) {
		*dest_begin++ = (byte)(ds >> (i * 8));
	}

	// Write stream size
	for (uint i = 0; i < sizeof(ulong); ++i) {
		*dest_begin++ = (unsigned char)(size_estimate >> (i * 8));
	}
}

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

//----------------------------------------------------------------------------//
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

/**
 * Base lzma state. Contains an instance of allocator compatible with
 * LZMA SDK and flag that the state needs further initialization.
 */
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

/**
 * Private state holder for lzma compressor
 */
struct lzma_encode_state : lzma_state_base {
	detail::lzma_encoder enc;

	lzma_encode_state(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff,
			lzma::lzma_params const& p) :
		lzma_state_base(d, af, ff),
		enc(d, af, ff, p)
	{
	}

	virtual
	~lzma_encode_state()
	{
	}
};

/**
 * Private state holder for lzma decompressor
 */
struct lzma_decode_state : lzma_state_base {
	CLzmaDec decHandle_;

	lzma_decode_state(void* d,
			lzma::lzma_alloc_func af,
			lzma::lzma_free_func ff) :
		lzma_state_base(d, af, ff)
	{
		LzmaDec_Construct(&decHandle_);
	}
	virtual
	~lzma_decode_state()
	{
		LzmaDec_Free(&decHandle_, &alloc_);
	}
};

//----------------------------------------------------------------------------//
//------------------lzma_base implementation (compressor)---------------------//
lzma_base<true>::lzma_base() :
		state_(nullptr)
{
}

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
	state_ = new lzma_encode_state(derived, af, ff, p);
}

bool
lzma_base<true>::filter(const char*& src_begin, const char* src_end,
		char*& dest_begin, char* dest_end, bool flush)
{
	size_t in_sz = src_end - src_begin;
	size_t out_sz = dest_end - dest_begin;

	std::cerr << "Input buffer size " << (src_end - src_begin)
			<< " output buffer size " << (dest_end - dest_begin)
			<< (flush ? " " : " no ") << "flush\n";
	char* dest_orig = dest_begin;

	bool encoded = state_->enc(src_begin, src_end, dest_begin, dest_end);

	std::cerr << "After encoding input " << (src_end - src_begin)
			<< " output " <<  (dest_end - dest_begin)
			<< " encoding result " << (encoded ? "true" : "false") << "\n";

	return src_begin == src_end // consumed all
			&& (in_sz != 0 || dest_orig != dest_begin) // output was made
	;
}

void
lzma_base<true>::reset(bool realloc)
{
	lzma_encode_state* old = state_;
	if (realloc) {
		state_ = new lzma_encode_state(old->alloc_.derived,
				old->alloc_.alloc_func, old->alloc_.free_func, old->enc.params());
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

		int result = LzmaDec_Allocate(&state_->decHandle_, header, LZMA_PROPS_SIZE, &state_->alloc_);
		lzma_error::check BOOST_PREVENT_MACRO_SUBSTITUTION(result);

		LzmaDec_Init(&state_->decHandle_);

		state_->initialized_ = true;
	}

	ELzmaStatus status;
	int result = LzmaDec_DecodeToBuf(&state_->decHandle_,
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

/**
 * (C) Copyright OpenGames, LLC (sergei.a.fedorov at gmail dot com)
 */
#ifndef _SHAMAN_LZMA_LZMA_HPP_
#define _SHAMAN_LZMA_LZMA_HPP_

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

#include <cassert>
#include <iosfwd>            // streamsize.
#include <memory>            // allocator, bad_alloc.
#include <new>

#include <boost/config.hpp>  // MSVC, STATIC_CONSTANT, DEDUCED_TYPENAME, DINKUM.
#include <boost/cstdint.hpp> // uint*_t
#include <boost/detail/workaround.hpp>
#include <boost/iostreams/constants.hpp>   // buffer size.
#include <boost/iostreams/detail/config/auto_link.hpp>
#include <boost/iostreams/detail/config/dyn_link.hpp>
#include <boost/iostreams/detail/config/wide_streams.hpp>
#include <boost/iostreams/detail/ios.hpp>  // failure, streamsize.
#include <boost/iostreams/filter/symmetric.hpp>
#include <boost/iostreams/pipeline.hpp>
#include <boost/type_traits/is_same.hpp>

#include <shaman/util/ranged_value.hpp>

// Must come last.
#ifdef BOOST_MSVC
# pragma warning(push)
# pragma warning(disable:4251 4231 4660)         // Dependencies not exported.
#endif
#include <boost/config/abi_prefix.hpp>

namespace shaman {
namespace lzma {

//@{
/** @name Typedefs */
typedef uint32_t uint;
typedef uint8_t byte;
typedef uint64_t ulong;
typedef unsigned char byte;

typedef void* (*lzma_alloc_func)(void*, lzma::uint, lzma::uint);
typedef void (*lzma_free_func)(void*, void*);
//@}

//@{
/** @name Constants */
extern const lzma::uint PROPERTIES_SIZE;
//@}

enum lzma_algo {
	LZMA_FAST,
	LZMA_NORMAL
};

enum lzma_mode {
	LZMA_HASH_CHAIN,
	LZMA_BIN_TREE
};

/**
 * Parameters for LZMA library
 */
struct lzma_params {
	//@{
	/** @name Typedefs */
	/**
	 * [0, 9], default = 5
	 */
	typedef util::ranged_value< uint, 0, 9, 5 >		level_type;

	//@{
	/** @name Typedefs for esoteric params */
	/**
	 * [0, 8], default = 3
	 */
	typedef util::ranged_value< uint, 0, 8, 3 >		lc_type;
	/**
	 * [0, 4], default = 0
	 */
	typedef util::ranged_value< uint, 0, 4 >		lp_type;
	/**
	 * [0, 4], default = 2
	 */
	typedef util::ranged_value< uint, 0, 4, 2 >		pb_type;
	//@}

	/**
	 * [5, 273], default = 32
	 * TODO Move 273 (LZMA_MATCH_LEN_MAX) to a constant
	 */
	typedef util::ranged_value< uint, 5, 273, 32 >	fast_bytes_size_type;
	/**
	 * [2, 4], default = 4
	 */
	typedef util::ranged_value< uint, 2, 4, 4 >		hash_bytes_size_type;
	/**
	 * [1, 1 << 30], default = 32
	 */
	typedef util::ranged_value< uint, 1, 1 << 30, 32>	match_finder_cycles_type;

	/**
	 * 32 bit [(1 << 12), (1 << 27)]
	 * 64 bit [(1 << 12), (1 << 30)]
	 * default = (1 << 24)
	 */
	typedef util::ranged_value< uint, 1 << 12, 1 << 30, 1 << 24 >
			dictionary_size_type;
	//@}
	//@{
	/** @name Member data fields */

	ulong						size_estimate;

	level_type					level;

	lzma_algo					algo;
	lzma_mode					mode;

	//@{
	/** @name Esoteric params, TODO refactor to literal names */
	lc_type						lc;
	lp_type						lp;
	pb_type						pb;
	//@}

	fast_bytes_size_type		fb;
	hash_bytes_size_type		hash_bytes_size;
	match_finder_cycles_type	match_finder_cycles;
	dictionary_size_type		dict_size;
	//@}

	lzma_params(
			ulong size_estimate = 0,
			uint level = level_type::default_value,
			lzma_algo = LZMA_NORMAL,
			lzma_mode = LZMA_BIN_TREE,
			uint lc = lc_type::default_value,
			uint lp = lp_type::default_value,
			uint pb = pb_type::default_value,
			uint fb = fast_bytes_size_type::default_value,
			uint hash_bytes_size = hash_bytes_size_type::default_value,
			uint match_finder_cycles = match_finder_cycles_type::default_value,
			uint dict_size = 0
	);

	void
	write( char*& dest_begin, char* dest_end ) const;
};

class BOOST_IOSTREAMS_DECL lzma_error : public BOOST_IOSTREAMS_FAILURE {
public:
	explicit lzma_error(int error);
	int error() const { return error_; }
	static void check BOOST_PREVENT_MACRO_SUBSTITUTION(int error);
private:
	int error_;
};

namespace detail {

template <typename Alloc>
struct lzma_allocator_traits {
#ifndef BOOST_NO_STD_ALLOCATOR
	typedef typename Alloc::template rebind<char>::other type;
#else
	typedef std::allocator<char> type;
#endif
};

template <typename Alloc,
		  typename Base =
				  BOOST_DEDUCED_TYPENAME lzma_allocator_traits<Alloc>::type >
struct lzma_allocator : private Base {
private:
	typedef typename Base::size_type size_type;
public:
	BOOST_STATIC_CONSTANT(bool, custom =
			(!boost::is_same< std::allocator<char>, Base >::value));
	typedef typename lzma_allocator_traits<Alloc>::type allocator_type;
	static void* allocate(void* self, lzma::uint items, lzma::uint size);
	static void deallocate(void* self, void* address);
};

struct lzma_encode_state;

template < bool Compress >
class lzma_base;

template <>
class lzma_base< true > {
public:
	typedef char char_type;
protected:
	lzma_base();
	~lzma_base();

	template< typename Alloc >
	void
	init(lzma_params const& p, lzma_allocator< Alloc >& lzalloc);

    void
    reset(bool realloc);
public:
	bool
	filter( const char*& src_begin, const char* src_end,
			char*& dest_begin, char* dest_end, bool flush);
private:
    void
    do_init( lzma_params const& p,
		#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
    		lzma::lzma_alloc_func,
    		lzma::lzma_free_func,
		#endif
    		void* derived
    );
private:
    lzma_encode_state*		state_;
};

struct lzma_decode_state;
/**
 * Decompressor implementation
 */
template<>
class lzma_base< false > {
public:
	typedef char char_type;
protected:
	lzma_base();
	~lzma_base();

	template< typename Alloc >
	void
	init(lzma_params const& p, lzma_allocator< Alloc >& lzalloc);

    void
    reset(bool realloc);
public:
	bool
	filter( const char*& src_begin, const char* src_end,
			char*& dest_begin, char* dest_end, bool flush);


private:
    void
    do_init( lzma_params const& p,
		#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
    		lzma::lzma_alloc_func,
    		lzma::lzma_free_func,
		#endif
    		void* derived
    );
private:
    lzma_decode_state*		state_;
};

/**
 * Model of C-Style filter implementing compression by delegating to the
 * lzma function LzmaEncode
 */
template< typename Alloc = std::allocator<char> >
class lzma_compressor_impl : public lzma_base< true >, public lzma_allocator< Alloc > {
public:
	lzma_compressor_impl(lzma_params const& p = lzma_params());
	~lzma_compressor_impl();

	void
	close();
};

template < typename Alloc = std::allocator<char> >
class lzma_decompressor_impl : public lzma_base< false >, public lzma_allocator< Alloc > {
public:
	lzma_decompressor_impl(lzma_params const& p = lzma_params());
	~lzma_decompressor_impl();

	void
	close();
	bool
	eof() const { return eof_; }
private:
	bool eof_;
};

} // namespace detail

/**
 * Model of InputFilter and OutputFilter implementing compression using LZMA lib
 */
template < typename Alloc = std::allocator<char> >
struct basic_lzma_compressor :
		boost::iostreams::symmetric_filter< detail::lzma_compressor_impl< Alloc >, Alloc >	{
private:
	typedef detail::lzma_compressor_impl< Alloc > impl_type;
	typedef typename boost::iostreams::symmetric_filter< impl_type, Alloc > base_type;
public:
	typedef typename base_type::char_type char_type;
	typedef typename base_type::category  category;

	basic_lzma_compressor( lzma_params const& p = lzma_params(),
			int buffer_size = boost::iostreams::default_device_buffer_size );
	//lzma::ulong crc()
	//int total_in()
};

BOOST_IOSTREAMS_PIPABLE(basic_lzma_compressor, 1)

typedef basic_lzma_compressor<> lzma_compressor;

/**
 * Model of InputFilter and OutputFilter implementing decompression using LZMA lib
 */
template < typename Alloc = std::allocator<char> >
struct basic_lzma_decompressor :
		boost::iostreams::symmetric_filter< detail::lzma_decompressor_impl< Alloc >, Alloc > {
private:
	typedef detail::lzma_decompressor_impl< Alloc > impl_type;
	typedef boost::iostreams::symmetric_filter< impl_type, Alloc > base_type;
public:
	typedef typename base_type::char_type char_type;
	typedef typename base_type::category  category;

	basic_lzma_decompressor( lzma_params const& p = lzma_params(),
			int buffer_size = boost::iostreams::default_device_buffer_size );

	//lzma::ulong crc()
	//int total_out()
	bool
	eof() { return this->filter().eof(); }
};

BOOST_IOSTREAMS_PIPABLE(basic_lzma_decompressor, 1)

typedef basic_lzma_decompressor<> lzma_decompressor;

} // namespace lzma
} // namespace shaman

#include <shaman/lzma/lzma.inl>

#include <boost/config/abi_suffix.hpp> // Pops abi_suffix.hpp pragmas.
#ifdef BOOST_MSVC
# pragma warning(pop)
#endif

#endif /* _SHAMAN_LZMA_LZMA_HPP_ */

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
typedef uint32_t ulong;

//@}

/**
 * Parameters for LZMA library
 */
struct lzma_params {

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

class BOOST_IOSTREAMS_DECL lzma_base {
public:
	typedef char char_type;
protected:
	lzma_base();
	~lzma_base();

    void
    before( const char*& src_begin, const char* src_end,
                 char*& dest_begin, char* dest_end );
    void
    after( const char*& src_begin, char*& dest_begin,
                bool compress );

    int
    compress(int flush);
    int
    decompress(int flush);

    void
    reset(bool compress, bool realloc);
private:
	void*		stream_;
};

/**
 * Model of C-Style filter implementing compression by delegating to the
 * lzma function LzmaEncode
 */
template< typename Alloc = std::allocator<char> >
class lzma_compressor_impl : public lzma_base, public lzma_allocator< Alloc > {
public:
	lzma_compressor_impl(lzma_params const& p = lzma_params());
	~lzma_compressor_impl();

	bool
	filter( const char*& src_begin, const char* src_end,
			char*& dest_begin, char* dest_end, bool flush);
	void
	close();
};

template < typename Alloc = std::allocator<char> >
class lzma_decompressor_impl : public lzma_base, public lzma_allocator< Alloc > {
public:
	lzma_decompressor_impl(lzma_params const& p = lzma_params());
	~lzma_decompressor_impl();

	bool
	filter( const char*& src_begin, const char* src_end,
			char*& dest_begin, char* dest_end, bool flush);
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

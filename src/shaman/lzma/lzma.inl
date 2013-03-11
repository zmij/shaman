/**
 * @file lzma.inl
 *
 *  Created on: 04.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef _SHAMAN_LZMA_LZMA_INL_
#define _SHAMAN_LZMA_LZMA_INL_

#include <shaman/lzma/lzma.hpp>

namespace shaman {
namespace lzma {
namespace detail {

//----------------------------------------------------------------------------//
//------------------Implementation of lzma_allocator--------------------------//
template<typename Alloc, typename Base>
void*
lzma_allocator<Alloc, Base>::allocate(void* self, lzma::uint items, lzma::uint size)
{
	size_type len = items * size;
	char* ptr = static_cast<allocator_type*>(self)->allocate(
			len + sizeof(size_type)
			#if BOOST_WORKAROUND(BOOST_DINKUMWARE_STDLIB, == 1)
				, (char*)0
			#endif
			);
	*reinterpret_cast<size_type*>(ptr) = len;
	return ptr + sizeof(size_type);
}

template<typename Alloc, typename Base>
void
lzma_allocator<Alloc, Base>::deallocate(void* self, void* address)
{
	if (address != 0) {
		char* ptr = reinterpret_cast<char*>(address) - sizeof(size_type);
		size_type len = *reinterpret_cast<size_type*>(ptr) + sizeof(size_type);
		static_cast<allocator_type*>(self)->deallocate(ptr, len);
	}
}

//------------------Implementation of lzma_base (compressor)------------------//
template< typename Alloc >
void
lzma_base< true >::init(lzma_params const& p, lzma_allocator< Alloc >& lzalloc)
{
	bool custom = lzma_allocator< Alloc >::custom;
	do_init(
		p,
		#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
			custom ? lzma_allocator< Alloc >::allocate : 0,
			custom ? lzma_allocator< Alloc >::deallocate : 0,
		#endif
			&lzalloc
	);
}

//------------------Implementation of lzma_base (decompressor)----------------//
template< typename Alloc >
void
lzma_base< false >::init(lzma_params const& p, lzma_allocator< Alloc >& lzalloc)
{
	//bool custom = lzma_allocator< Alloc >::custom;
	do_init(
		p,
		#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
			lzma_allocator< Alloc >::allocate,
			lzma_allocator< Alloc >::deallocate,
		#endif
			&lzalloc
	);
}


//------------------Implementation of lzma_compressor_impl--------------------//
template< typename Alloc >
lzma_compressor_impl<Alloc>::lzma_compressor_impl(lzma_params const& p)
{
	init(p, static_cast< lzma_allocator< Alloc >& >(*this));
}

template< typename Alloc >
lzma_compressor_impl<Alloc>::~lzma_compressor_impl()
{
	reset(false);
}

//template< typename Alloc >
//bool
//lzma_compressor_impl<Alloc>::filter( const char*& src_begin, const char* src_end,
//		char*& dest_begin, char* dest_end, bool flush)
//{
//	before(src_begin, src_end, dest_begin, dest_end);
//	int result = compress(flush/* TODO Convert into LZMA flush flag (if any) */);
//	after(src_begin, dest_begin, true);
//	lzma_error::check BOOST_PREVENT_MACRO_SUBSTITUTION(result);
//	/* TODO check stream end */
//	return !flush;
//}

template< typename Alloc >
void
lzma_compressor_impl<Alloc>::close()
{
	reset(true);
}

//------------------Implementation of lzma_decompressor_impl------------------//
template< typename Alloc >
lzma_decompressor_impl<Alloc>::lzma_decompressor_impl(lzma_params const& p) :
	eof_(false)
{
	init(p, static_cast< lzma_allocator< Alloc >& >(*this));
}

template< typename Alloc >
lzma_decompressor_impl<Alloc>::~lzma_decompressor_impl()
{
	reset(false);
}

template< typename Alloc >
void
lzma_decompressor_impl<Alloc>::close()
{
	eof_ = false;
	reset(true);
}

} // namespace detail

//------------------Implementation of lzma_compressor-------------------------//
template< typename Alloc >
basic_lzma_compressor<Alloc>::basic_lzma_compressor(lzma_params const& p, int buffer_size) :
	base_type(buffer_size, p)
{
}

//------------------Implementation of lzma_decompressor-----------------------//
template< typename Alloc >
basic_lzma_decompressor<Alloc>::basic_lzma_decompressor(lzma_params const& p, int buffer_size) :
	base_type(buffer_size, p)
{
}

} // namespace lzma
} // namespace shaman

#endif /* _SHAMAN_LZMA_LZMA_INL_ */

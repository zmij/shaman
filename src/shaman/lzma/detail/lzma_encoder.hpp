/**
 * @file lzma_encoder.hpp
 *
 *  Created on: 12.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef SHAMAN_LZMA_DETAIL_LZMA_ENCODER_HPP_
#define SHAMAN_LZMA_DETAIL_LZMA_ENCODER_HPP_

#include <shaman/lzma/lzma.hpp>

namespace shaman {
namespace lzma {
namespace detail {

class lzma_encoder {
public:
	lzma_encoder(void* allocator, lzma::lzma_alloc_func, lzma::lzma_free_func,
			lzma_params const& = lzma_params());
	~lzma_encoder();

	lzma_params const&
	params() const;

	/**
	 * Encode buffer [src_begin, src_end) and write it to [dest_begin, dest_end)
	 * Return true if the buffer was fully encoded and written to destination buffer
	 *
	 * @param src_begin 	Pointer to source buffer start
	 * @param src_end		Pointer after source buffer
	 * @param dest_begin	Pointer to destination buffer start
	 * @param dest_end		Pointer after destination buffer
	 * @return	False if all consumed bytes were written to destination buffer.
	 * 			True if the destination buffer was not enough
	 * @pre		src_end - src_begin is input buffer size
	 * @pre		dest_end - dest_begin is output buffer size
	 *
	 * @post	src_begin points after the last byte consumed
	 * @post	dest_begin points after the last byte written to destination buffer
	 * @post	original source buffer size - (src_end - src_begin) is the count of bytes consumed
	 * @post	original destination buffer size - (dest_end - dest_begin) is the count of bytes written
	 */
	bool
	operator()(char const*& src_begin, char const* src_end, char*& dest_begin, char* dest_end);
private:
	struct impl;
private:
	impl* pimpl_;
};

} // namespace detail
} // namespace lzma
} // namespace shaman

#endif /* SHAMAN_LZMA_DETAIL_LZMA_ENCODER_HPP_ */

/**
 * @file match_finder.hpp
 *
 *  Created on: 14.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef SHAMAN_LZMA_DETAIL_MATCH_FINDER_HPP_
#define SHAMAN_LZMA_DETAIL_MATCH_FINDER_HPP_

#include <shaman/lzma/lzma.hpp>

namespace shaman {
namespace lzma {
namespace detail {

class match_finder {
public:
	match_finder(void* allocator, lzma_alloc_func, lzma_free_func, lzma_params const&);
	~match_finder();

	/**
	 * Copy a block from source to buffer
	 *
	 * @param source_begin pointer to start byte of source buffer
	 * @param source_end   pointer after the last byte of source buffer
	 *
	 * @post source_begin points after the last byte consumed
	 */
	void
	read_block(char const*& source_begin, char const* source_end);

	lzma::uint
	available_bytes() const;

	bool
	need_more_input() const;
	bool
	at_stream_end() const;

	lzma::uint
	get_matches(lzma::uint* distances);

	void
	skip(lzma::uint num);

	lzma::byte*
	current_pos();

	lzma::byte
	get_byte(int index);
private:
	struct impl;
private:
	impl* pimpl_;
};

} // namespace detail
} // namespace lzma
} // namespace shaman

#endif /* SHAMAN_LZMA_DETAIL_MATCH_FINDER_HPP_ */

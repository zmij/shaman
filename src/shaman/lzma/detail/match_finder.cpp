/**
 * @file match_finder.cpp
 *
 *  Created on: 14.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <shaman/lzma/detail/match_finder.hpp>
#include <shaman/lzma/detail/lzma_constants.hpp>

namespace shaman {
namespace lzma {
namespace detail {

namespace {

const lzma::uint EMPTY_HASH_VALUE = 0;
const lzma::uint kMaxValForNormalize = ((lzma::uint)0xffffffff);
const lzma::uint kNormalizeStepMin = (1 << 10) /* it must be power of 2 */;
const lzma::uint NORMALIZE_MASK = (~(kNormalizeStepMin - 1));
const lzma::uint kMaxHistorySize = ((lzma::uint)3 << 30);

const lzma::uint kStartMaxLen = 3;

} // namespace


struct match_finder::impl {
	//@{
	/** @name Matching strategies */
	struct matcher {
		impl* impl_;

		matcher(impl* imp) : impl_(imp) {}
		virtual
		~matcher() {}

		virtual lzma::uint
		get_matches(lzma::uint* distances) = 0;
		virtual void
		skip(lzma::uint num) = 0;

		bool
		get_matches_header( uint min_len, byte*& cur );

		void
		skip_matches_spec(/**/);
	};

	struct hc4_matcher;
	struct bt2_matcher;
	struct bt3_matcher;
	struct bt4_matcher;
	//@}

	void*			allocator;
	lzma_alloc_func	alloc_func;
	lzma_free_func	free_func;

	lzma::byte*		bufferBase;
	lzma::byte*		buffer;

	lzma::uint		pos;
	lzma::uint		posLimit;
	lzma::uint		streamPos;
	lzma::uint		lenLimit;

	lzma::uint		cyclicBufferPos;
	lzma::uint		cyclicBufferSize;

	lzma::uint		matchMaxLen;
	lzma::uint*		hash;
	lzma::uint*		son;
	lzma::uint		hashMask;
	lzma::uint		cutValue;

	lzma::uint		blockSize;
	lzma::uint		keepSizeBefore;
	lzma::uint		keepSizeAfter;

	lzma::uint		numHashBytes;

	//@{
	// TODO Either use or remove
	bool			directInput;
	size_t			directInputRem;
	//@}

	lzma_mode		mode;
	int				bigHash;
	lzma::uint		historySize;
	lzma::uint		fixedHashSize;
	lzma::uint		hashsum_size;
	lzma::uint		sons_size;

	lzma::uint		crc[256];

	bool			at_sream_end;
	bool			need_more_input;

	// TODO virtual functions implementation
	matcher*		matcher_;

	impl( void* alc,
		lzma_alloc_func af,
		lzma_free_func ff,
		lzma_params const& p ) :
			allocator(alc),
			alloc_func(af),
			free_func(ff),
			bufferBase(nullptr),
			buffer(nullptr),
			pos(0),
			posLimit(0),
			streamPos(0),
			lenLimit(0),
			cyclicBufferPos(0),
			cyclicBufferSize(p.dict_size + 1),
			matchMaxLen(p.fb),
			hash(nullptr),
			son(nullptr),
			hashMask(0),
			cutValue(p.match_finder_cycles),
			blockSize(0),
			keepSizeBefore(0),
			keepSizeAfter(0),
			numHashBytes(p.hash_bytes_size),
			directInput(false),
			directInputRem(0),
			mode(p.mode),
			bigHash(p.dict_size > BIG_HASH_DIC_LIMIT),
			historySize(p.dict_size),
			fixedHashSize(0),
			hashsum_size(0),
			sons_size(mode == LZMA_BIN_TREE ? cyclicBufferSize * 2 : cyclicBufferSize),
			crc(),
			at_sream_end(false),
			need_more_input(true),
			matcher_(create_matcher())
	{
		uint keepAddBufferBefore  = NUM_OPTS;
		uint keepAddBufferAfter = MATCH_LEN_MAX;
		uint sizeReserv = (historySize > (uint)2 << 30 ) ? historySize >> 2 : historySize >> 1
				+ (keepAddBufferBefore + matchMaxLen + keepAddBufferAfter) / 2 + (1 << 19);

		keepSizeBefore = historySize + keepAddBufferBefore + 1;
		keepSizeAfter  = matchMaxLen + keepAddBufferAfter;

		//@{
		/** @name LzInWindowCreate */
		{
			blockSize = keepSizeBefore + keepSizeAfter + sizeReserv;
			if (!directInput) {
				realloc_buffer();
			}
		}
		//@}
		uint hs;
		{
			if (numHashBytes == 2) {
				hs = (1 << 16) - 1;
			} else {
				hs = historySize - 1;
				hs |= (hs >> 1);
				hs |= (hs >> 2);
				hs |= (hs >> 4);
				hs |= (hs >> 8);
				hs >>= 1;
				hs |= 0xffff; /* requred for deflate */
				if (hs > (1 << 24)) {
					if (numHashBytes == 3) {
						hs = (1 << 24) - 1;
					} else {
						hs >>= 1;
					}
				}
			}
			hashMask = hs;
			++hs;
			if (numHashBytes > 2)
				fixedHashSize += HASH2_SIZE;
			if (numHashBytes > 3)
				fixedHashSize += HASH3_SIZE;
			if (numHashBytes > 4)
				fixedHashSize += HASH4_SIZE;
			hs += fixedHashSize;
		}
		{
//			uint prevSize = hashSizeSum + numSons;
			hashsum_size = hs;

			uint hash_size = hashsum_size + sons_size;
			hash = reinterpret_cast<uint*>( alloc_func(allocator, hash_size, sizeof(uint)) );

			memset(hash, 0, hash_size * sizeof(uint));
			son = hash + hashsum_size;
		}

		for (lzma::uint i = 0; i < 256; ++i) {
			lzma::uint r = i;
			for (lzma::uint j = 0; j < 8; ++j) {
				r = (r >> 1) ^ (CRC_POLY & ~((r & 1) - 1));
			}
			crc[i] = r;
		}
	}

	~impl()
	{
		free_func(allocator, bufferBase);
		free_func(allocator, hash);

		delete matcher_;
	}

	matcher*
	create_matcher();

	void
	realloc_buffer()
	{
		free_func(allocator, bufferBase);
		bufferBase = reinterpret_cast<byte*>(alloc_func(allocator, 1, blockSize ));
		buffer = bufferBase;
	}

	/**
	 * Copy a block from source to buffer
	 *
	 * @param source_begin pointer to start byte of source buffer
	 * @param source_end   pointer after the last byte of source buffer
	 *
	 * @post source_begin points after the last byte consumed
	 */
	void
	read_block(char const*& source_begin, char const* source_end)
	{
		size_t source_sz = source_end - source_begin;
		if (source_sz != 0) {
			// TODO Handle direct input if any
			for (;;) {
				byte* dest = buffer + (streamPos - pos);
				size_t dest_sz = bufferBase + blockSize - dest;
				if (dest_sz == 0) {
					need_more_input = false;
					break;
				}

				size_t read_sz = source_sz <= dest_sz ? source_sz : dest_sz;
				memcpy(dest, source_begin, read_sz);
				source_begin += read_sz;
				source_sz -= read_sz;
				streamPos += read_sz;

				if (source_sz == 0) {
					// mark that more input is needed
					need_more_input = true;
					break;
				}

				if (streamPos - pos > keepSizeAfter) {
					need_more_input = false;
					break;
				}
			}
		} else {
			at_sream_end = true;
		}
		set_limits();
	}

	bool
	need_move() const
	{
		return (bufferBase + blockSize - buffer) <= keepSizeAfter;
	}

	void
	move_block()
	{
		memmove(bufferBase, buffer - keepSizeBefore, streamPos - pos + keepSizeBefore);
		buffer = bufferBase + keepSizeBefore;
	}

	void
	set_limits()
	{
		uint limit = MAX_VAL_FOR_NORMALIZE - pos;
		uint limit2 = cyclicBufferSize - cyclicBufferPos;
		if (limit2 < limit)
			limit = limit2;

		limit2 = streamPos - pos;
		if (limit2 <= keepSizeAfter) {
			if (limit2 > 0)
				limit2 - 1;
		} else {
			limit2 -= keepSizeAfter;
		}

		if (limit2 < limit)
			limit = limit2;

		uint limit3 = streamPos - pos;
		if (limit3 > matchMaxLen)
			limit3 = matchMaxLen;
		lenLimit = limit3;
		posLimit = pos + limit;
	}

	inline lzma::uint const
	sub_value()
	{
		return (pos - historySize - 1) & NORMALIZE_MASK;
	}

	void
	reduce_offsets(lzma::uint subValue)
	{
		posLimit -= subValue;
		pos -= subValue;
		streamPos -= subValue;
	}

	void
	normalize(lzma::uint subValue, lzma::uint* items, lzma::uint num_items)
	{
		for (lzma::uint i = 0; i < num_items; ++i) {
			lzma::uint& value = items[i];
			if (value <= subValue) {
				value = EMPTY_HASH_VALUE;
			} else {
				value -= subValue;
			}
		}
	}

	void
	normalize()
	{
		lzma::uint subValue = sub_value();
		normalize(subValue, hash, hashsum_size + sons_size);
		reduce_offsets(subValue);
	}
	void
	check_limits()
	{
		if (pos == MAX_VAL_FOR_NORMALIZE) {
			normalize();
		}
		if (!at_sream_end && keepSizeAfter == streamPos - pos) {
			// check and move and read
			// mark more input is needed
			need_more_input = true;
		}
		if (cyclicBufferPos == cyclicBufferSize) {
			cyclicBufferPos = 0;
		}
		set_limits();
	}

	lzma::byte
	get_byte(int index) const
	{
		return *(buffer + index);
	}

	lzma::byte*
	current_pos() const
	{
		return buffer;
	}

	lzma::uint
	available_bytes() const
	{
		return streamPos - pos;
	}

	void
	move_pos()
	{
		++cyclicBufferPos;
		++buffer;
		if (++pos == posLimit) {
			// check limits
			check_limits();
		}
	}

	uint*
	get_matches_spec(uint len_limit, uint cur_match, uint* distances, uint max_len) const
	{
		uint* ptr0 = son + (cyclicBufferPos << 1) + 1;
		uint* ptr1 = son + (cyclicBufferPos << 1);

		uint len0(0), len1(0);
		uint cut_val = cutValue;

		byte const* cur = buffer;

		for (;;) {
			uint delta = pos - cur_match;
			if (cut_val-- == 0 || delta >= cyclicBufferSize) {
				*ptr0 = EMPTY_HASH_VALUE;
				*ptr1 = EMPTY_HASH_VALUE;
				return distances;
			}

			uint* pair = son +
					((cyclicBufferPos - delta +
							(delta > cyclicBufferPos) ? cyclicBufferSize : 0) << 1);
			byte const* pb = cur - delta;
			uint len = (len0 < len1 ? len0 : len1);
			if (pb[len] == cur[len]) {
				if (++len != len_limit && pb[len] == cur[len]) {
					while (++len != len_limit) {
						if (pb[len] != cur[len])
							break;
					}
				}

				if (max_len < len) {
					max_len = len;
					*distances++ = max_len;
					*distances++ = delta - 1;

					if (len == len_limit) {
						*ptr1 = pair[0];
						*ptr0 = pair[1];
						return distances;
					}
				}
			}
			if (pb[len] < cur[len]) {
				*ptr1 = cur_match;
				ptr1 = pair + 1;
				cur_match = *ptr1;
				len1 = len;
			} else {
				*ptr0 = cur_match;
				ptr0 = pair;
				cur_match = *ptr0;
				len0 = len;
			}
		}
		return distances;
	}

	void
	skip_matches_spec(uint len_limit, uint cur_match) const
	{
		uint* ptr0 = son + (cyclicBufferPos << 1) + 1;
		uint* ptr1 = son + (cyclicBufferPos << 1);

		uint len0(0), len1(0);
		uint cut_val = cutValue;

		byte const* cur = buffer;

		for (;;) {
			uint delta = pos - cur_match;

			if (cut_val-- == 0 || delta >= cyclicBufferSize) {
				*ptr0 = EMPTY_HASH_VALUE;
				*ptr1 = EMPTY_HASH_VALUE;
				return;
			}

			uint* pair = son +
					((cyclicBufferPos - delta +
							(delta > cyclicBufferPos) ? cyclicBufferSize : 0) << 1);
			byte const* pb = cur - delta;
			uint len = (len0 < len1 ? len0 : len1);
			if (pb[len] == cur[len]) {
				while (++len != len_limit) {
					if (pb[len] != cur[len])
						break;
				}

				if (len == len_limit) {
					*ptr1 = pair[0];
					*ptr0 = pair[1];
					return;
				}
			}

			if (pb[len] < cur[len]) {
				*ptr1 = cur_match;
				ptr1 = pair + 1;
				cur_match = *ptr1;
				len1 = len;
			} else {
				*ptr0 = cur_match;
				ptr0 = pair;
				cur_match = *ptr0;
				len0 = len;
			}
		}
	}

	lzma::uint
	get_matches(lzma::uint* distances)
	{
		assert(matcher_);
		return matcher_->get_matches(distances);
	}

	void
	skip(lzma::uint num)
	{
		assert(matcher_);
		matcher_->skip(num);
	}

};

bool
match_finder::impl::matcher::get_matches_header(uint min_len, byte*& buff)
{
	uint len_limit = impl_->lenLimit;
	if (len_limit < min_len) {
		impl_->move_pos();
		return true;
	}
	buff = impl_->buffer;
	return false;
}

//void
//match_finder::impl::matcher::skip_matches_spec(/**/)
//{
//	;
//}


struct match_finder::impl::hc4_matcher : match_finder::impl::matcher {
	hc4_matcher(impl* i) : matcher(i) {}

	virtual lzma::uint
	get_matches(lzma::uint* distances)
	{
		return 0;
	}
	virtual void
	skip(lzma::uint num)
	{

	}
};

struct match_finder::impl::bt2_matcher : match_finder::impl::matcher {
	bt2_matcher(impl* i) : matcher(i) {}

	virtual lzma::uint
	get_matches(lzma::uint* distances)
	{
		return 0;
	}
	virtual void
	skip(lzma::uint num)
	{

	}
};

struct match_finder::impl::bt3_matcher : match_finder::impl::matcher {
	bt3_matcher(impl* i) : matcher(i) {}

	virtual lzma::uint
	get_matches(lzma::uint* distances)
	{
		return 0;
	}
	virtual void
	skip(lzma::uint num)
	{

	}
};

struct match_finder::impl::bt4_matcher : match_finder::impl::matcher {
	bt4_matcher(impl* i) : matcher(i) {}

	void
	hash_calc(byte* cur, uint& hashValue, uint& hash2Value, uint& hash3Value)
	{
		uint tmp = impl_->crc[ cur[0] ^ cur[1] ];
		hash2Value = tmp & (HASH2_SIZE -1);
		hash3Value = (tmp ^ ((uint)cur[2] << 8)) & HASH3_SIZE;
		hashValue  = ((tmp ^ ((uint)cur[2] << 8)) ^ ( impl_->crc[ cur[3] ] << 5 ))
				& impl_->hashMask;
	}

	virtual lzma::uint
	get_matches(lzma::uint* distances)
	{
		// get header(4)
		byte* cur = nullptr;
		if (get_matches_header(4, cur))
			return 0;

		uint len_limit = impl_->lenLimit;

		// hash 4 calc
		uint hashValue, hash2Value, hash3Value;
		hash_calc(cur, hashValue, hash2Value, hash3Value);
		uint delta2 = impl_->pos - impl_->hash[hash2Value];
		uint delta3 = impl_->pos - impl_->hash[FIX_HASH3_SIZE + hash3Value];

		uint cur_match = impl_->hash[FIX_HASH4_SIZE + hashValue];
		impl_->hash[hash2Value] = impl_->pos;
		impl_->hash[FIX_HASH3_SIZE + hash3Value] = impl_->pos;
		impl_->hash[FIX_HASH4_SIZE + hashValue]  = impl_->pos;

		uint max_len(1), offset(0);

		if (delta2 < impl_->cyclicBufferSize && *(cur - delta3) == *cur) {
			max_len = 2;
			distances[0] = max_len;
			distances[1] = delta2 - 1;
			offset = 2;
		}

		if (delta2 != delta3 && delta3 < impl_->cyclicBufferSize && *(cur - delta3) == *cur) {
			max_len = 3;
			distances[offset + 1] = delta3 - 1;
			offset += 2;
			delta2 = delta3;
		}

		if (offset != 0) {
			for (; max_len != len_limit; ++max_len) {
				if (cur[ max_len - delta2 ] != cur[max_len])
					break;

				distances[offset - 2] = max_len;
				if (max_len == len_limit) {
					// SkipMatchesSpec
					impl_->skip_matches_spec(len_limit, cur_match);

					// move_pos_ret
					impl_->move_pos();
					return offset;
				}
			}
		}
		if (max_len < 3) {
			max_len = 3;
		}

		offset = impl_->get_matches_spec(
				len_limit, cur_match, distances, max_len) - distances;

		impl_->move_pos();
		return offset;
	}

	virtual void
	skip(lzma::uint num)
	{
		do {
			// Skip header (4)
			byte* cur = nullptr;
			if (get_matches_header(4, cur))
				continue;

			uint len_limit = impl_->lenLimit;

			// hash 4 calc
			uint hashValue, hash2Value, hash3Value;
			hash_calc(cur, hashValue, hash2Value, hash3Value);
			uint cur_match = impl_->hash[FIX_HASH4_SIZE + hashValue];
			impl_->hash[hash2Value] = impl_->pos;
			impl_->hash[FIX_HASH3_SIZE + hash3Value] = impl_->pos;
			impl_->hash[FIX_HASH4_SIZE + hashValue]  = impl_->pos;

			// skip footer
			impl_->skip_matches_spec(len_limit, cur_match);
			impl_->move_pos();

		} while (--num != 0);
	}
};


match_finder::impl::matcher*
match_finder::impl::create_matcher()
{
	if (mode == LZMA_HASH_CHAIN) {
		return new hc4_matcher(this);
	} else if (numHashBytes == 2) {
		return new bt2_matcher(this);
	} else if (numHashBytes == 3) {
		return new bt3_matcher(this);
	} else if (numHashBytes == 4) {
		return new bt4_matcher(this);
	}
	return nullptr;
}

match_finder::match_finder(void* allocator, lzma_alloc_func af, lzma_free_func ff,
		lzma_params const& p) :
	pimpl_(new impl(allocator, af, ff, p))
{
}

match_finder::~match_finder()
{
	delete pimpl_;
}

void
match_finder::read_block(char const*& source_begin, char const* source_end)
{
	pimpl_->read_block(source_begin, source_end);
}

lzma::uint
match_finder::available_bytes() const
{
	return pimpl_->available_bytes();
}

bool
match_finder::need_more_input() const
{
	return pimpl_->need_more_input;
}

bool
match_finder::at_stream_end() const
{
	return pimpl_->at_sream_end;
}

lzma::uint
match_finder::get_matches(lzma::uint* distances)
{
	return pimpl_->get_matches(distances);
}

void
match_finder::skip(lzma::uint num)
{
	pimpl_->skip(num);
}

lzma::byte*
match_finder::current_pos()
{
	return pimpl_->current_pos();
}

lzma::byte
match_finder::get_byte(int index)
{
	return pimpl_->get_byte(index);
}

} // namespace detail
} // namespace lzma
} // namespace shaman


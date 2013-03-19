/**
 * @file lzma_encoder.cpp
 *
 *  Created on: 12.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <shaman/lzma/detail/lzma_encoder.hpp>
#include <shaman/lzma/detail/lzma_constants.hpp>
#include <shaman/lzma/detail/match_finder.hpp>

#include <string.h> // size_t and memset

#include <list>

namespace shaman {
namespace lzma {
namespace detail {

namespace {

const int LITERAL_NEXT_STATES[NUM_STATES]	= {0, 0, 0, 0, 1, 2, 3, 4,  5,  6,   4, 5};
const int MATCH_NEXT_STATES[NUM_STATES]		= {7, 7, 7, 7, 7, 7, 7, 10, 10, 10, 10, 10};
const int REP_NEXT_STATES[NUM_STATES]		= {8, 8, 8, 8, 8, 8, 8, 11, 11, 11, 11, 11};
const int SHORT_REP_NEXT_STATES[NUM_STATES]	= {9, 9, 9, 9, 9, 9, 9, 11, 11, 11, 11, 11};

const lzma::uint NUM_TOP_BITS = 24;
const lzma::uint TOP_VALUE = 1 << NUM_TOP_BITS;
const lzma::uint INFINITY_PRICE = 1 << 30;

struct ProbPrices {
	lzma::uint	probPrices[BIT_MODEL_TOTAL >> NUM_MOVE_REDUCING_BITS];

	ProbPrices()
	{
		const int CYCLES_BITS = NUM_BIT_PRICE_SHIFT_BITS;
		for( lzma::uint i = (1 << NUM_MOVE_REDUCING_BITS) / 2; i < BIT_MODEL_TOTAL;
				i += (1 << NUM_MOVE_REDUCING_BITS) ) {
			lzma::uint w = i;
			lzma::uint bitCount = 0;
			for (lzma::uint j = 0; j < CYCLES_BITS; ++j) {
				w = w * w;
				bitCount <<= 1;
				while ( w >= ((lzma::uint)1 << 16) ) {
					w >>= 1;
					++bitCount;
				}
			}
			probPrices[ i >> NUM_MOVE_REDUCING_BITS ] =
					(NUM_BIT_MODEL_TOTAL_BITS << CYCLES_BITS) - 15 -bitCount;
		}
	}

	//@{
	/** @name Esoteric functions */
	/** @todo Give more mnemonic names */
	inline uint
	get_price(LzmaProb prob, uint symbol) const
	{
		return probPrices[ (prob ^ ((-(int)symbol) & BIT_MODEL_TOTAL - 1)) >> NUM_MOVE_REDUCING_BITS ];
	}

	inline uint
	get_price_0(uint prob) const
	{
		return probPrices[ prob >> NUM_MOVE_REDUCING_BITS];
	}

	inline uint
	get_price_1(uint prob) const
	{
		return probPrices[ (prob ^ BIT_MODEL_TOTAL) >> NUM_MOVE_REDUCING_BITS ];
	}
	//@}

	uint
	get_price(LzmaProb const* probs, uint symbol)
	{
		uint price = 0;

		symbol |= 0x100;
		do {
			price += get_price(probs[symbol >> 8], (symbol >> 7) & 1);
			symbol <<= 1;
		} while (symbol < 0x10000);

		return price;
	}

	uint
	get_tree_price(LzmaProb const* probs, int num_bits_level, uint symbol) const
	{
		uint price = 0;

		symbol |= 1 << num_bits_level;
		while (symbol != 1) {
			price += get_price(probs[symbol >> 1], symbol & 1);
			symbol >>= 1;
		}

		return price;
	}

	uint
	get_tree_reverse_price(LzmaProb const* probs, int num_bits_level, uint symbol) const
	{
		uint price = 0;
		uint m = 1;
		for (int i = num_bits_level; i != 0; --i) {
			uint bit = symbol & 1;
			symbol >>= 1;
			price += get_price(probs[m], bit);
			m = (m << 1) | bit;
		}

		return price;
	}

	uint
	get_price_matched( LzmaProb const* probs, uint symbol, uint match_byte ) const
	{
		uint price = 0;
		uint offs = 0x100;
		symbol |= 0x100;
		do {
			match_byte <<= 1;
			price += get_price(probs[ offs + (match_byte & offs) + (symbol >> 8) ],
					(symbol >> 7) & 1 );
			symbol <<= 1;
			offs &= ~(match_byte ^ symbol);
		} while (symbol < 0x10000);
		return price;
	}
};

struct Optimal {
	lzma::uint	price;

	lzma::uint	state;
	bool		prev1IsChar;
	bool		prev2;

	lzma::uint	posPrev2;
	lzma::uint	backPrev2;

	lzma::uint	posPrev;
	lzma::uint	backPrev;
	lzma::uint	backs[NUM_REPS];

	void
	make_as_char()
	{
		backPrev = (uint)(-1);
		prev1IsChar = false;
	}

	void
	make_as_short_rep()
	{
		backPrev = 0;
		prev1IsChar = false;
	}

	bool
	is_short_rep() const
	{
		return backPrev == 0;
	}
};

struct RangeEnc {
	struct buffer_page {
		lzma::byte* buf_begin;
		lzma::byte* buf_end;
		lzma::byte* write_current;
		lzma::byte* read_current;

		void
		reset()
		{
			write_current = buf_begin;
			read_current = buf_begin;
		}

		bool
		full() const
		{
			return write_current == buf_end;
		}

		/**
		 * Test if there are no bytes ready for output
		 * @return
		 */
		bool
		empty() const
		{
			return (write_current != buf_end ?
					write_current == read_current : buf_end == read_current);
		}

		bool
		flushed() const
		{
			// There is no place to write and no place to read
			return write_current == buf_end && read_current == buf_end;
		}

		size_t
		available_write() const
		{
			return buf_end - write_current;
		}

		size_t
		available_read() const
		{
			if (write_current != buf_end) {
				// This buffer is currently being written
				return write_current - read_current;
			}
			return buf_end - read_current;
		}

		bool
		flush(char*& dest_begin, char* dest_end)
		{
			size_t write_sz = dest_end - dest_begin;
			size_t avail_sz = available_read();
			if (avail_sz < write_sz)
				write_sz = avail_sz;

			if (write_sz > 0) {
				memcpy(dest_begin, read_current, write_sz);
				dest_begin += write_sz;
				read_current += write_sz;
			}
			return empty();
		}
	};

	typedef std::list< buffer_page > buffer_pages_container;
	typedef buffer_pages_container::iterator page_iterator;

	void*			allocator;
	lzma_alloc_func	alloc_func;
	lzma_free_func	free_func;

	lzma::uint		range;
	lzma::byte		cache;
	lzma::ulong		low;
	lzma::ulong		cacheSize;

	buffer_pages_container buffer_pages;

	lzma::ulong		processed;

	bool 			data_flushed;

	RangeEnc(void* alc,
			lzma_alloc_func af,
			lzma_free_func ff) :
	  allocator(alc),
	  alloc_func(af),
	  free_func(ff),
	  range(0xffffffff),
	  cache(0),
	  low(0),
	  cacheSize(1),
	  processed(0),
	  data_flushed(false)
	{
		create_buffer_page();
	}

	~RangeEnc()
	{
		while (!buffer_pages.empty())
			pop_buffer();
	}

	void
	create_buffer_page()
	{
		byte* b = reinterpret_cast<byte*>(alloc_func(allocator, 1, RC_BUFFER_SIZE));;
		buffer_pages.push_back(
				{b, b + RC_BUFFER_SIZE, b, b}
		);
	}

	void
	free_buffer_page()
	{
		buffer_page page = buffer_pages.front();
		buffer_pages.pop_front();
		free_func(allocator, page.buf_begin);
	}

	buffer_page&
	write_page()
	{
		assert(!buffer_pages.empty());
		return buffer_pages.back();
	}

	buffer_page&
	read_page()
	{
		assert(!buffer_pages.empty());
		return buffer_pages.front();
	}

	inline size_t
	pages_size() const
	{
		return buffer_pages.size();
	}

	size_t
	read_bytes_available()
	{
		size_t sz(0);
		if (pages_size() >= 2) {
			sz += (pages_size() - 2) + RC_BUFFER_SIZE;
			sz += write_page().available_read();
		}

		sz += read_page().available_read();

		return sz;
	}

	bool
	output_ready()
	{
		return !read_page().empty();
	}

	void
	push_buffer()
	{
		create_buffer_page();
	}

	void
	pop_buffer()
	{
		free_buffer_page();
	}

	/**
	 *
	 * @param dest_begin
	 * @param dest_end
	 * @return true if all encoded data has been output to buffer
	 */
	bool
	flush(char*& dest_begin, char* dest_end)
	{
		while( dest_begin != dest_end && output_ready()) {
			buffer_page& page = read_page();
			if (page.flush(dest_begin, dest_end)) {
				// page is fully output to buffer
				if (pages_size() > 1) {
					pop_buffer();
				} else {
					page.reset();
					break;
				}
			}
		}
		return !output_ready();
	}

	void
	flush_data()
	{
		if (!data_flushed) {
			for (int i = 0; i < 5; ++i) {
				shift_low();
			}
			data_flushed = true;
		}
	}

	void
	shift_low()
	{
		buffer_page page = write_page();

		if (low < 0xff000000 || (int)(low >> 32) != 0) {
			byte tmp = cache;
			do {
				*page.write_current++ = (byte)(tmp + (byte)(low >> 32));
				if (page.full()) {
					write_page() = page;
					push_buffer();
					page = write_page();
				}
				tmp = 0xff;
			} while (--cacheSize != 0);
			cache = (byte)((uint)low >> 24);
		}
		write_page() = page;
		++cacheSize;
		low = (uint)low << 8;
	}

	void
	encode_bit(LzmaProb* prob, lzma::uint symbol)
	{
		uint ttt = *prob;
		uint new_bound = (range >> NUM_BIT_MODEL_TOTAL_BITS) * ttt;
		if (symbol == 0) {
			range = new_bound;
			ttt += (BIT_MODEL_TOTAL - ttt) >> NUM_MOVE_BITS;
		} else {
			low += new_bound;
			range -= new_bound;
			ttt -= ttt >> NUM_MOVE_BITS;
		}
		*prob = (LzmaProb)ttt;
		if (range < TOP_VALUE) {
			range <<= 8;
			shift_low();
		}
	}

	void
	encode_direct_bits(uint value, uint num_bits)
	{
		do {
			range >>= 1;
			low += range & (0 - ((value >> --num_bits) & 1));
			if (range < TOP_VALUE) {
				range <<= 8;
				shift_low();
			}
		} while (num_bits != 0);
	}

	// LitEnc_Encode
	void
	encode(LzmaProb* probs, uint symbol)
	{
		symbol |= 0x100;
		do {
			encode_bit(probs + (symbol >> 8), (symbol >> 7) & 1);
			symbol <<= 1;
		} while (symbol < 0x10000);
	}

	// LitEnc_EncodeMatched
	void
	encode_matched(LzmaProb* probs, uint symbol, uint match_byte )
	{
		uint offs = 0x100;
		symbol |= 0x100;
		do {
			match_byte <<= 1;
			encode_bit(probs + (offs + (match_byte & offs) + (symbol >> 8)), (symbol >> 7) & 1);
			symbol <<= 1;
			offs &= ~(match_byte ^ symbol);
		} while (symbol < 0x10000);
	}

	/**
	 * RcTree_Encode
	 * @param probs
	 * @param num_bit_levels
	 * @param symbol
	 */
	void
	encode_tree( LzmaProb* probs, int num_bit_levels, uint symbol )
	{
		uint m = 1;
		for (int i = num_bit_levels; i != 0;) {
			--i;
			uint bit = (symbol >> i) & 1;
			encode_bit(probs + m, bit);
			m = (m << 1) | bit;
		}
	}

	/**
	 * RcTree_ReverseEncode
	 * @param probs
	 * @param num_bit_levels
	 * @param symbol
	 */
	void
	encode_reverse_tree( LzmaProb* probs, int num_bit_levels, uint symbol )
	{
		uint m = 1;
		for (int i = 0; i < num_bit_levels; ++i) {
			uint bit = symbol & 1;
			encode_bit(probs + m, bit);
			m = (m << 1) | bit;
			symbol >>= 1;
		}
	}
};

struct LenEnc {
	LzmaProb	choice;
	LzmaProb	choice2;
	LzmaProb	low[NUM_PB_STATES_MAX << LEN_NUM_LOW_BITS];
	LzmaProb	mid[NUM_PB_STATES_MAX << LEN_NUM_MID_BITS];
	LzmaProb	high[LEN_NUM_HIGH_SYMBOLS];

	LenEnc() :
		choice(PROB_INIT_VALUE),
		choice2(PROB_INIT_VALUE)
	{
		for (uint i = 0; i < (NUM_PB_STATES_MAX << LEN_NUM_LOW_BITS); ++i) {
			low[i] = PROB_INIT_VALUE;
		}
		for (uint i = 0; i < (NUM_PB_STATES_MAX << LEN_NUM_MID_BITS); ++i) {
			mid[i] = PROB_INIT_VALUE;
		}
		for (uint i = 0; i < LEN_NUM_HIGH_SYMBOLS; ++i) {
			high[i] = PROB_INIT_VALUE;
		}
	}

	void
	set_prices( ProbPrices const& prob_prices, uint* prices, uint pos_state, uint num_symbols )
	{
		uint a0 = prob_prices.get_price_0(choice);

		for (uint i = 0; i < LEN_NUM_LOW_SYMBOLS; ++i) {
			if (i >= num_symbols)
				return;
			prices[i] = a0 + prob_prices.get_tree_price(
					low + (pos_state << LEN_NUM_LOW_BITS), LEN_NUM_LOW_BITS, i);
		}

		uint a1 = prob_prices.get_price_1(choice);
		uint b0 = a1 + prob_prices.get_price_0(choice2);

		for (uint i = LEN_NUM_LOW_SYMBOLS; i < LEN_NUM_LOW_SYMBOLS + LEN_NUM_MID_SYMBOLS; ++i) {
			if (i >= num_symbols)
				return;
			prices[i] = b0 + prob_prices.get_tree_price(
					mid + (pos_state << LEN_NUM_MID_BITS),
					LEN_NUM_MID_BITS, i - LEN_NUM_LOW_SYMBOLS);
		}

		uint b1 = a1 + prob_prices.get_price_1(choice2);

		for (uint i = LEN_NUM_LOW_SYMBOLS + LEN_NUM_MID_SYMBOLS; i < num_symbols; ++i) {
			prices[i] = b1 + prob_prices.get_tree_price(high, LEN_NUM_HIGH_BITS,
					i - LEN_NUM_LOW_SYMBOLS - LEN_NUM_MID_SYMBOLS);
		}
	}

	/**
	 * LenEnc_Encode
	 * @param rc
	 * @param symbol
	 * @param pos_state
	 */
	void
	encode(RangeEnc& rc, uint symbol, uint pos_state)
	{
		if (symbol < LEN_NUM_LOW_SYMBOLS) {
			rc.encode_tree(low + (pos_state << LEN_NUM_LOW_BITS), LEN_NUM_LOW_BITS, symbol);
		} else {
			rc.encode_bit(&choice, 1);
			if (symbol < LEN_NUM_LOW_SYMBOLS + LEN_NUM_MID_SYMBOLS) {
				rc.encode_bit(&choice2, 0);
				rc.encode_tree(mid + (pos_state << LEN_NUM_MID_BITS),
						LEN_NUM_MID_BITS, symbol - LEN_NUM_LOW_SYMBOLS);
			} else {
				rc.encode_bit(&choice2, 1);
				rc.encode_tree(high, LEN_NUM_HIGH_BITS,
						symbol - LEN_NUM_LOW_SYMBOLS - LEN_NUM_MID_SYMBOLS);
			}
		}
	}
};

struct LenPriceEnc {
	LenEnc		p;
	lzma::uint	prices[NUM_PB_STATES_MAX][LEN_NUM_SYMBOLS_TOTAL];
	lzma::uint	tableSize;
	lzma::uint	counters[NUM_PB_STATES_MAX];

	LenPriceEnc(uint tbl_size) :
		tableSize(tbl_size)
	{
	}

	void
	update_table(ProbPrices const& prob_prices, uint pos_state)
	{
		p.set_prices(prob_prices, prices[pos_state], pos_state, tableSize);
		counters[pos_state] = tableSize;
	}

	void
	update_tables(ProbPrices const& prob_prices, uint num_pos_states)
	{
		for (uint pos_state = 0; pos_state < num_pos_states; ++pos_state) {
			update_table(prob_prices, pos_state);
		}
	}

	/**
	 * LenEnc_Encode2
	 * @param rc
	 * @param symbol
	 * @param pos_state
	 * @param probPrices
	 * @param updatePrices
	 */
	void
	encode(RangeEnc& rc, uint symbol, uint pos_state, ProbPrices& prob_prices, bool updatePrices)
	{
		p.encode(rc, symbol, pos_state);

		if (updatePrices) {
			if (--counters[pos_state] == 0) {
				update_table(prob_prices, pos_state);
			}
		}
	}
};

} // namespace

struct lzma_encoder::impl {
	//@{
	/** @name Allocator */
	void*					allocator;
	lzma::lzma_alloc_func	alloc_func;
	lzma::lzma_free_func	free_func;
	//@}

	lzma_params				params;

	/*
	 * Here go the matchFinder and matchFinderObj
	 */
	match_finder			matchFinder;

	lzma::uint	optimumEndIndex;
	lzma::uint	optimumCurrentIndex;

	lzma::uint	longestMatchLength;
	lzma::uint	numPairs;
	lzma::uint	num_avail_;
	Optimal		opt[NUM_OPTS];

	lzma::byte	fastPos[1 << NUM_LOG_BITS];

	ProbPrices	probPrices;
	lzma::uint	matches[MATCH_LEN_MAX * 2 + 2 + 1];
	lzma::uint	numFastBytes;
	lzma::uint	additionalOffset;
	lzma::uint	reps[NUM_REPS];
	lzma::uint	state_;

	lzma::uint	posSlotPrices[NUM_LEN_TO_POS_STATES][DIST_TABLE_SIZE_MAX];
	lzma::uint	distancesPrices[NUM_LEN_TO_POS_STATES][NUM_FULL_DISTANCES];
	lzma::uint	alignPrices[ALIGN_TABLE_SIZE];
	lzma::uint	alignPriceCount;

	lzma::uint	distTableSize;

	lzma::uint	lpMask, pbMask;

	LzmaProb*	litProbs;

	LzmaProb	isMatch[NUM_STATES][NUM_PB_STATES_MAX];
	LzmaProb	isRep[NUM_STATES];
	LzmaProb	isRepG0[NUM_STATES];
	LzmaProb	isRepG1[NUM_STATES];
	LzmaProb	isRepG2[NUM_STATES];
	LzmaProb	isRep0Long[NUM_STATES][NUM_PB_STATES_MAX];

	LzmaProb	posSlotEncoder[NUM_LEN_TO_POS_STATES][1 << NUM_POS_SLOT_BITS];
	LzmaProb	posEncoders[NUM_FULL_DISTANCES - END_POS_MODEL_INDEX];
	LzmaProb	posAlignEncoder[1 << NUM_ALIGN_BITS];

	LenPriceEnc lenEnc;
	LenPriceEnc repLenEnc;

	bool		fastMode;

	RangeEnc	rc;

	bool		writeEndMark;
	lzma::ulong	curr_pos_64;
	lzma::uint	matchPriceCount;
	bool		finished;
	bool		multiThread;

//	  SRes result;
	lzma::uint	dictSize; // contained in params
	lzma::uint	matchFinderCycles; // contained in params

	bool		needInit;


	impl(
			void* alc,
			lzma_alloc_func af,
			lzma_free_func ff,
			lzma_params const& p) :
		allocator(alc),
		alloc_func(af),
		free_func(ff),
		params(p),
		matchFinder(alc, af, ff, p),
		optimumEndIndex(0),
		optimumCurrentIndex(0),
		longestMatchLength(0),
		numPairs(0),
		num_avail_(0),
		numFastBytes(p.fb),
		additionalOffset(0),
		state_(0),
		alignPriceCount(0),
		distTableSize(0),
		lpMask((1 << p.lp) - 1),
		pbMask((1 << p.pb) - 1),
		litProbs(
				reinterpret_cast< LzmaProb* >(
						alloc(0x300 << (p.lc + p.lp),
								sizeof(LzmaProb)))),
		lenEnc(numFastBytes + 1 - MATCH_LEN_MIN),
		repLenEnc(numFastBytes + 1 - MATCH_LEN_MIN),
		fastMode(p.algo == LZMA_FAST),
		rc(alc, af, ff),
		writeEndMark(false),
		curr_pos_64(0),
		matchPriceCount(0),
		finished(false),
		multiThread(false),
		dictSize(p.dict_size),
		matchFinderCycles(p.match_finder_cycles),
		needInit(true)
	{
		// TODO lzma enc properties
		//@{
		/** @name LzmaEnc_AllocAndInit */

		// TODO Create an accessor in lzma_params for distTableSize
		uint i = 0;
		for (; i < DIC_LOG_SIZE_MAX_COMPRESS; ++i) {
			if (p.dict_size <= (lzma::uint)(1 << i))
				break;
		}
		distTableSize = i * 2;
		//@{
		/** @name LzmaEnc_Init */
		memset(reps, 0, sizeof(reps));
		for (uint i = 0; i < NUM_STATES; ++i) {
			for (uint j = 0; j < NUM_PB_STATES_MAX; ++j) {
				isMatch[i][j]		= PROB_INIT_VALUE;
				isRep0Long[i][j]	= PROB_INIT_VALUE;
			}
			isRep[i]	= PROB_INIT_VALUE;
			isRepG0[i]	= PROB_INIT_VALUE;
			isRepG1[i]	= PROB_INIT_VALUE;
			isRepG2[i]	= PROB_INIT_VALUE;
		}
		uint num = 0x300 << (params.lp + params.lc);
		for (uint i = 0; i < num; ++i) {
			litProbs[i] = PROB_INIT_VALUE;
		}
		for (uint i = 0; i < NUM_LEN_TO_POS_STATES; ++i) {
			for (uint j = 0; j < (1 << NUM_POS_SLOT_BITS); ++j) {
				posSlotEncoder[i][j] = PROB_INIT_VALUE;
			}
		}
		for (uint i = 0; i < NUM_FULL_DISTANCES - END_POS_MODEL_INDEX; ++i) {
			posEncoders[i] = PROB_INIT_VALUE;
		}

		for (uint i = 0; i < (1 << NUM_ALIGN_BITS); ++i) {
			posAlignEncoder[i] = PROB_INIT_VALUE;
		}

		//@}
		//@{
		/** @name FastPosInit */
		init_fast_pos();

		//@}
		//@{
		/** @name LzmaEnc_InitPrices */
		if (!fastMode) {
			fill_distances_prices();
			fill_align_prices();
		}
		lenEnc.update_tables(probPrices, 1 << params.pb);
		repLenEnc.update_tables(probPrices, 1 << params.pb);
		//@}
		//@}
	}
	~impl()
	{
		// Free litProbs
		free(litProbs);
	}

	void*
	alloc(size_t items, size_t size)
	{
		return alloc_func(allocator, items, size);
	}

	void
	free(void* address)
	{
		free_func(allocator, address);
	}

	void
	init_fast_pos()
	{
		uint c = 2;
		fastPos[0] = 0;
		fastPos[1] = 1;
		for (uint slot_fast = 2; slot_fast < NUM_LOG_BITS * 2; ++slot_fast) {
			uint k = (1 << ((slot_fast >> 1) - 1));
			for (uint j = 0; j < k; ++j, ++c) {
				fastPos[c] = (byte)slot_fast;
			}
		}
	}

	inline uint
	get_pos_slot(uint i)
	{
		return fastPos[i];
	}
	inline uint
	get_distance_slot(uint pos)
	{
		uint i = 6 + ((NUM_LOG_BITS - 1) &
				(0 - ((((uint)1 << (NUM_LOG_BITS + 6)) - 1 - pos ) >> 31)));
		return fastPos[pos >> i] + (i * 2);
	}

	inline uint
	get_slot(uint pos)
	{
		if (pos < NUM_FULL_DISTANCES) {
			return fastPos[pos];
		} else {
			return get_distance_slot(pos);
		}
	}


	void
	fill_distances_prices()
	{
		uint temp_prices[NUM_FULL_DISTANCES];
		for (uint i = START_POS_MODEL_INDEX; i < NUM_FULL_DISTANCES; ++i) {
			uint pos_slot = get_pos_slot(i);
			uint footer_bits = ((pos_slot >> 1) - 1);
			uint base = ((2 | (pos_slot & 1)) << footer_bits);
			temp_prices[i] = probPrices.get_tree_reverse_price(
								posEncoders + base - pos_slot - 1,
								footer_bits, i - base
							);
		}

		for (uint len_to_pos_state = 0; len_to_pos_state < NUM_LEN_TO_POS_STATES; ++len_to_pos_state) {
			const LzmaProb* encoder = posSlotEncoder[len_to_pos_state];
			uint* pos_slot_prices = posSlotPrices[len_to_pos_state];

			for (uint pos_slot = 0; pos_slot < distTableSize; ++pos_slot) {
				pos_slot_prices[pos_slot] = probPrices.get_tree_price(
						encoder, NUM_POS_SLOT_BITS, pos_slot);
			}
			for (uint pos_slot = END_POS_MODEL_INDEX; pos_slot < distTableSize; ++pos_slot) {
				pos_slot_prices[pos_slot] += (((pos_slot >> 1) - 1 - NUM_ALIGN_BITS) << NUM_BIT_PRICE_SHIFT_BITS);
			}

			uint* distance_prices = distancesPrices[len_to_pos_state];
			for (uint i = 0; i < START_POS_MODEL_INDEX; ++i) {
				distance_prices[i] = pos_slot_prices[i];
			}
			for (uint i = START_POS_MODEL_INDEX; i < NUM_FULL_DISTANCES; ++i) {
				distance_prices[i] = pos_slot_prices[ get_pos_slot(i) ] + temp_prices[i];
			}
		}
		matchPriceCount = 0;
	}

	void
	fill_align_prices()
	{
		for (uint i = 0; i < ALIGN_TABLE_SIZE; ++i) {
			alignPrices[i] = probPrices.get_tree_reverse_price(posAlignEncoder, NUM_ALIGN_BITS, i);
		}
		alignPriceCount = 0;
	}

	lzma::uint
	read_match_distances(uint& num_pairs)
	{
		uint len_res = 0;

		num_avail_ = matchFinder.available_bytes();
		num_pairs = matchFinder.get_matches(matches);

		if (num_pairs > 0) {
			len_res = matches[ num_pairs - 2 ];
			if (len_res == numFastBytes) {
				byte const* pby = matchFinder.current_pos() - 1;
				uint distance = matches[ num_pairs - 1 ] + 1;
				uint num_avail = num_avail_;
				if (num_avail > MATCH_LEN_MAX)
					num_avail = MATCH_LEN_MAX;

				byte const* pby2 = pby - distance;
				for (; len_res < num_avail && pby[len_res] == pby2[len_res]; ++len_res);
			}
		}
		++additionalOffset;
		return len_res;
	}

	void
	write_end_marker(uint pos_state)
	{
		if (!finished) {
			rc.encode_bit(&isMatch[state_][pos_state], 1);
			rc.encode_bit(&isRep[state_], 0);

			state_ = MATCH_NEXT_STATES[state_];

			uint len = MATCH_LEN_MIN;
			lenEnc.encode(rc, len - MATCH_LEN_MIN, pos_state, probPrices, !fastMode);
			rc.encode_tree(
					posSlotEncoder[ get_len_to_pos_state(len) ],
					NUM_POS_SLOT_BITS,
					(1 << NUM_POS_SLOT_BITS) - 1);
			rc.encode_direct_bits(
					(((uint)1 << 30) - 1) >> NUM_ALIGN_BITS,
							30 - NUM_ALIGN_BITS);
			rc.encode_reverse_tree(posAlignEncoder, NUM_ALIGN_BITS, ALIGN_MASK);
			finished = true;
		}
	}
	/**
	 * Write encoded data to output buffer.
	 * Return true when all encoded data was written to destination buffer
	 * @param dest_begin
	 * @param dest_end
	 * @return
	 */
	bool
	flush(char*& dest_begin, char* dest_end, uint pos_state)
	{
		if (matchFinder.at_stream_end()) {
			//write_end_marker(pos_state);
			rc.flush_data();
		}
		return rc.flush(dest_begin, dest_end);
	}

	void
	move_pos(uint num)
	{
		if (num != 0) {
			additionalOffset += num;
			matchFinder.skip(num);
		}
	}

	inline LzmaProb*
	get_lit_probs(lzma::uint pos, lzma::byte prev_byte)
	{
		return litProbs +
				((((pos) & lpMask) << params.lc) + ((prev_byte) >> (8 - params.lc))) * 0x300;
	}

	inline bool
	is_char_state( uint s)
	{
		return s < 7;
	}

	uint
	get_rep_len_price(uint state, uint pos_state)
	{
		return probPrices.get_price_0(isRepG0[state]) +
				probPrices.get_price_0(isRep0Long[state][pos_state]);
	}

	uint
	get_pure_rep_price( uint rep_index, uint state, uint pos_state )
	{
		uint price = 0;
		if (rep_index == 0) {
			price = probPrices.get_price_0( isRepG0[state] ) +
					probPrices.get_price_1(isRep0Long[state][pos_state]);
		} else {
			price = probPrices.get_price_1(isRepG0[state]) +
					probPrices.get_price_0(isRepG1[state]);
			if (rep_index != 1) {

				//
				price += probPrices.get_price(isRepG2[state], rep_index - 2);
			}
		}

		return price;
	}

	uint
	get_rep_price( uint rep_index, uint len, uint state, uint pos_state )
	{
		return repLenEnc.prices[pos_state][len - MATCH_LEN_MIN] +
				get_pure_rep_price(rep_index, state, pos_state);
	}

	inline uint
	get_len_to_pos_state(uint len)
	{
		return len < NUM_LEN_TO_POS_STATES ? len - 2 : NUM_LEN_TO_POS_STATES - 1;
	}

	uint
	backward(uint cur, uint& back_res)
	{
		uint pos_mem = opt[cur].posPrev;
		uint back_mem = opt[cur].backPrev;
		optimumEndIndex = cur;

		do {
			if (opt[cur].prev1IsChar) {
				opt[pos_mem].make_as_char();
				opt[pos_mem].posPrev = pos_mem - 1;
				if (opt[cur].prev2 != 0) {
					opt[pos_mem - 1].prev1IsChar = false;
					opt[pos_mem - 1].posPrev = opt[cur].posPrev2;
					opt[pos_mem - 1].backPrev = opt[cur].backPrev2;
				}
			}
			uint pos_prev = pos_mem;
			uint back_cur = back_mem;

			back_mem = opt[pos_prev].backPrev;
			pos_mem = opt[pos_prev].posPrev;

			opt[pos_prev].backPrev = back_cur;
			opt[pos_prev].posPrev = cur;
			cur = pos_prev;
		} while (cur != 0);

		back_res = opt[0].backPrev;
		optimumCurrentIndex = opt[0].posPrev;
		return optimumCurrentIndex;
	}

	inline uint
	match_lenght( byte const* lhs, byte const* rhs, uint limit, uint start_val = 0 )
	{
		if (start_val == 0) {
			if (lhs[0] != rhs[0] || lhs[1] != rhs [1])
				return 0;
		}

		uint len_test = start_val == 0 ? 2 : start_val;
		for (; len_test < limit && lhs[len_test] == rhs[len_test]; ++len_test);
		return len_test;
	}

	uint
	get_optimum(uint position, uint& back_res)
	{
		if (optimumEndIndex != optimumCurrentIndex) {
			Optimal const& o = opt[optimumCurrentIndex];
			uint len_res = o.posPrev - optimumCurrentIndex;
			back_res = o.backPrev;
			optimumCurrentIndex = o.posPrev;
			return len_res;
		}

		optimumCurrentIndex = 0;
		optimumEndIndex = 0;

		uint main_len(0), num_pairs(0);

		if (additionalOffset == 0) {
			main_len = read_match_distances(num_pairs);
		} else {
			main_len = longestMatchLength;
			num_pairs = numPairs;
		}

		uint num_avail = num_avail_;
		if (num_avail < 2) {
			back_res = (uint)(-1);
			return 1;
		}

		if (num_avail > MATCH_LEN_MAX)
			num_avail = MATCH_LEN_MAX;

		byte* data = matchFinder.current_pos() - 1;
		uint rep_max_index = 0;
		uint reps_tmp[NUM_REPS], rep_lens_tmp[NUM_REPS];
		for (uint i = 0; i < NUM_REPS; ++i) {
			reps_tmp[i] = reps[i];
			byte const* data2 = data - (reps_tmp[i] + 1);

			rep_lens_tmp[i] = match_lenght(data, data2, num_avail);

			if (rep_lens_tmp[i] > rep_lens_tmp[rep_max_index])
				rep_max_index = i;
		}

		if (rep_lens_tmp[rep_max_index] >= numFastBytes) {
			uint len_res = rep_lens_tmp[rep_max_index];
			back_res = rep_max_index;
			move_pos(len_res - 1);
			return len_res;
		}

		if (main_len >= numFastBytes) {
			back_res = matches[num_pairs - 1] + NUM_REPS;
			move_pos(main_len - 1);
			return main_len;
		}

		byte cur_byte = *data;
		byte match_byte = *(data - reps_tmp[0] + 1 );

		if (main_len < 2 && cur_byte != match_byte && rep_lens_tmp[rep_max_index] < 2) {
			back_res = (uint)(-1);
			return 1;
		}

		opt[0].state = state_;

		uint pos_state = (position & pbMask);

		{
			LzmaProb const* probs = get_lit_probs(position, *(data - 1));
			opt[1].price = probPrices.get_price_0( isMatch[state_][pos_state] ) +
					( !is_char_state(state_) ?
							probPrices.get_price_matched(probs, cur_byte, match_byte) :
							probPrices.get_price(probs, cur_byte));
		}

		opt[1].make_as_char();

		uint match_price = probPrices.get_price_1( isMatch[state_][pos_state] );
		uint rep_match_price = match_price + probPrices.get_price_1(isRep[state_]);

		if (match_byte == cur_byte) {
			uint short_rep_price = rep_match_price + get_rep_len_price(state_, pos_state);
			if (short_rep_price < opt[1].price) {
				opt[1].price = short_rep_price;
				opt[1].make_as_short_rep();
			}
		}

		uint len_end = (main_len >= rep_lens_tmp[rep_max_index]) ? main_len :
				rep_lens_tmp[rep_max_index];

		if (len_end < 2) {
			back_res = opt[1].backPrev;
			return 1;
		}

		opt[1].posPrev = 0;
		for (uint i = 0; i < NUM_REPS; ++i) {
			opt[0].backs[i] = reps_tmp[i];
		}

		uint len = len_end;
		do {
			opt[len--].price = INFINITY_PRICE;
		} while ( len >= 2 );

		for (uint i = 0; i < NUM_REPS; ++i) {
			uint rep_len = rep_lens_tmp[i];
			if (rep_len < 2)
				continue;

			uint price = rep_match_price + get_pure_rep_price(i, state_, pos_state);
			do {
				uint cur_price = price + repLenEnc.prices[pos_state][rep_len - 2];
				Optimal& o = opt[rep_len];
				if (cur_price < o.price) {
					o.price = cur_price;
					o.posPrev = 0;
					o.backPrev = i;
					o.prev1IsChar = false;
				}

			} while( --rep_len >= 2 );
		}


		len = (rep_lens_tmp[0] >= 2) ? rep_lens_tmp[0] + 1 : 2;

		if (len <= main_len) {
			uint normal_match_price = match_price + probPrices.get_price_0(isRep[state_]);
			uint offs = 0;

			while (len > matches[offs])
				offs += 2;

			for (;; ++len) {
				uint distance = matches[offs + 1];
				uint curr_price = normal_match_price +
						lenEnc.prices[pos_state][len - MATCH_LEN_MIN];
				uint len_to_pos_state = get_len_to_pos_state(len);
				if (distance < NUM_FULL_DISTANCES) {
					curr_price += distancesPrices[len_to_pos_state][distance];
				} else {
					uint slot = get_distance_slot(distance);
					curr_price += alignPrices[distance & ALIGN_MASK] +
							posSlotPrices[len_to_pos_state][slot];
				}

				Optimal& o = opt[len];
				if (curr_price < o.price) {
					o.price = curr_price;
					o.posPrev = 0;
					o.backPrev = distance + NUM_REPS;
					o.prev1IsChar = false;
				}
				if (len == matches[offs]) {
					offs += 2;
					if (offs == num_pairs)
						break;
				}
			}
		}

		uint cur = 0;

		for (;;) {
			++cur;
			if (cur == len_end)
				return backward(cur, back_res);

			uint new_len = read_match_distances(num_pairs);
			if (new_len >= numFastBytes) {
				numPairs = num_pairs;
				longestMatchLength = new_len;
				return backward(cur, back_res);
			}

			++position;
			Optimal& cur_opt = opt[cur];
			uint pos_prev = cur_opt.posPrev;
			uint state (0);

			if (cur_opt.prev1IsChar) {
				--pos_prev;
				state = LITERAL_NEXT_STATES[state];
			} else {
				state = opt[pos_prev].state;
			}

			if (pos_prev == cur - 1) {
				if (cur_opt.is_short_rep()) {
					state = SHORT_REP_NEXT_STATES[state];
				} else {
					state = LITERAL_NEXT_STATES[state];
				}
			} else {
				uint pos(0);
				if (cur_opt.prev1IsChar && cur_opt.prev2) {
					pos_prev = cur_opt.posPrev2;
					pos = cur_opt.backPrev2;
					state = REP_NEXT_STATES[state];
				} else {
					pos = cur_opt.backPrev;
					if (pos < NUM_REPS) {
						state = REP_NEXT_STATES[state];
					} else {
						state = MATCH_NEXT_STATES[state];
					}
				}

				Optimal& prev_opt = opt[pos_prev];
				if (pos < NUM_REPS) {
					reps_tmp[0] = prev_opt.backs[pos];
					for (uint i = 1; i <= pos; ++i) {
						reps_tmp[i] = prev_opt.backs[i - 1];
					}
					for (uint i = pos + 1; i < NUM_REPS; ++i) {
						reps_tmp[i] = prev_opt.backs[i];
					}
				} else {
					reps_tmp[0] = pos - NUM_REPS;
					for (uint i = 1; i < NUM_REPS; ++i) {
						reps_tmp[i] = prev_opt.backs[i - 1];
					}
				}
			}

			cur_opt.state = state;

			// TODO use std::copy
			for (uint i = 0; i < NUM_REPS; ++i)
				cur_opt.backs[i] = reps_tmp[i];

			uint cur_price = cur_opt.price;
			bool next_is_char = false;

			// TODO Check code repeat
			data = matchFinder.current_pos() - 1;
			cur_byte = *data;
			match_byte = *(data - (reps_tmp[0] + 1));

			pos_state = (position & pbMask);

			uint cur_price1 = cur_price + probPrices.get_price_0(isMatch[state][pos_state]);
			{
				LzmaProb const* probs = get_lit_probs(position, *(data - 1));
				cur_price1 += !is_char_state(state) ?
						probPrices.get_price_matched(probs, cur_byte, match_byte) :
						probPrices.get_price(probs, cur_byte);
			}

			Optimal& next_opt = opt[cur + 1];

			if (cur_price1 < next_opt.price) {
				next_opt.price = cur_price1;
				next_opt.posPrev = cur;
				next_opt.make_as_char();
				next_is_char = true;
			}

			match_price = cur_price + probPrices.get_price_1(isMatch[state][pos_state]);
			rep_match_price = match_price + probPrices.get_price_1(isRep[state]);

			if (match_byte == cur_byte && !(next_opt.posPrev < cur && next_opt.backPrev == 0)) {
				uint short_rep_price = rep_match_price + get_rep_len_price(state, pos_state);
				if (short_rep_price <= next_opt.price) {
					next_opt.price = short_rep_price;
					next_opt.posPrev = cur;
					next_opt.make_as_short_rep();
					next_is_char = true;
				}
			}

			uint num_avail_full = num_avail_;
			{
				uint tmp = NUM_OPTS - 1 - cur;
				if (tmp < num_avail_full)
					num_avail_full = tmp;
			}

			if (num_avail_full < 2)
				continue;
			num_avail = std::min(num_avail_full, numFastBytes);

			if (!next_is_char && match_byte != cur_byte) { /* speed optimization */
				byte const* data2 = data - (reps_tmp[0] + 1);
				uint limit = numFastBytes + 1;
				if (limit > num_avail_full)
					limit = num_avail_full;

				uint tmp = 1;
				for (; tmp < limit && data[tmp] == data2[tmp]; ++tmp);
				uint len_test = tmp - 1;
				if (len_test >= 2) {
					uint next_state = LITERAL_NEXT_STATES[state];
					uint next_pos_state = (position + 1) & pbMask;
					uint next_rep_match_price = cur_price1
							+ probPrices.get_price_1(isMatch[next_state][next_pos_state])
							+ probPrices.get_price_1(isRep[next_state]);

					uint offset = cur + 1 + len_test;
					while (len_end < offset) {
						opt[++len_end].price = INFINITY_PRICE;
					}
					uint cur_len_price = next_rep_match_price +
							get_rep_price(0, len_test, next_state, next_pos_state);
					Optimal& o = opt[offset];
					if (cur_len_price < o.price) {
						o.price = cur_len_price;
						o.posPrev = cur + 1;
						o.backPrev = 0;
						o.prev1IsChar = true;
						o.prev2 = false;
					}
				}
			}

			uint start_len = 2;
			for  (uint rep_index = 0; rep_index < NUM_REPS; ++rep_index) {
				byte const* data2 = data - (reps_tmp[rep_index] + 1);
				uint len_test = match_lenght(data, data2, num_avail);
				if (len_test == 0)
					continue;

				while (len_end < cur + len_test) {
					opt[++len_end].price = INFINITY_PRICE;
				}

				uint len_test_tmp = len_test;
				uint price = rep_match_price + get_pure_rep_price(rep_index, state, pos_state);

				do {
					uint cur_len_price = price + repLenEnc.prices[pos_state][len_test_tmp - 2];
					Optimal& o = opt[cur + len_test_tmp];
					if (cur_len_price < o.price) {
						o.price = cur_len_price;
						o.posPrev = cur;
						o.backPrev = rep_index;
						o.prev1IsChar = false;
					}
				} while (--len_test_tmp >= 2);

				if (rep_index == 0)
					start_len = len_test + 1;

				uint limit = len_test + 1 + numFastBytes;

				if (limit > num_avail_full)
					limit = num_avail_full;

				uint len_test2 = match_lenght(data, data2, limit,
						len_test + 1) - len_test - 1;
				if (len_test2 >= 2) {
					uint state2 = REP_NEXT_STATES[state];
					uint next_pos_state = (position + len_test) & pbMask;
					uint cur_len_char_price = price
							+ repLenEnc.prices[pos_state][len_test - 2]
							+ probPrices.get_price_0(isMatch[state2][next_pos_state])
							+ probPrices.get_price_matched(
								get_lit_probs( position + len_test, data[len_test - 1] ),
								data[len_test], data2[len_test]);

					state2 = LITERAL_NEXT_STATES[state2];
					next_pos_state = (position + len_test + 1) & pbMask;
					uint next_rep_match_price = cur_len_char_price
							+ probPrices.get_price_1(isMatch[state2][next_pos_state])
							+ probPrices.get_price_1(isRep[state2]);

					uint offset = cur + len_test + 1 + len_test2;
					while (len_end < offset) {
						opt[++len_end].price = INFINITY_PRICE;
					}

					uint cur_len_price = next_rep_match_price +
							get_rep_price(0, len_test2, state2, next_pos_state);
					Optimal& o = opt[offset];
					if (cur_len_price < o.price) {
						o.price = cur_len_price;
						o.posPrev = cur + len_test + 1;
						o.backPrev = 0;
						o.prev1IsChar = true;
						o.prev2 = true;
						o.posPrev2 = cur;
						o.backPrev2 = rep_index;
					}
				}
			}

			if (new_len > num_avail) {
				new_len = num_avail;
				for (num_pairs = 0; new_len > matches[num_pairs]; num_pairs += 2);
				matches[num_pairs] = new_len;
				num_pairs += 2;
			}

			if (new_len >= start_len) {
				uint normal_match_price = match_price + probPrices.get_price_0( isRep[state]);

				while (len_end < cur + new_len)
					opt[++len_end].price = INFINITY_PRICE;

				uint offs = 0;
				while (start_len > matches[offs])
					offs += 2;

				uint cur_back = matches[offs + 1];
				uint pos_slot = get_distance_slot(cur_back);
				for (uint len_test = start_len; ; ++len_test) {
					uint cur_len_price = normal_match_price + lenEnc.prices[pos_state][len_test - MATCH_LEN_MIN];
					uint len_to_pos_state = get_len_to_pos_state(len_test);
					if (cur_back < NUM_FULL_DISTANCES) {
						cur_len_price += distancesPrices[len_to_pos_state][cur_back];
					} else {
						cur_len_price += posSlotPrices[len_to_pos_state][pos_slot]
						     + alignPrices[cur_back * ALIGN_MASK];
					}

					Optimal& o = opt[cur + len_test];
					if (cur_len_price < o.price) {
						o.price = cur_len_price;
						o.posPrev = cur;
						o.backPrev = cur_back + NUM_REPS;
						o.prev1IsChar = false;
					}

					if (len_test == matches[offs]) {
						// TODO check code repeat
						byte const* data2 = data - (cur_back + 1);
						uint limit = len_test + 1 + numFastBytes;
						if (limit > num_avail_full) {
							limit = num_avail_full;
						}

						uint len_test2 = match_lenght(data, data2, limit, len_test + 1)
								- len_test - 1;
						if (len_test2 >= 2) {
							// TODO Refactor code repeat
							uint state2 = MATCH_NEXT_STATES[state];
							uint next_pos_state = (position + len_test) & pbMask;
							uint cur_len_char_price = cur_len_price
									+ probPrices.get_price_0(isMatch[state2][next_pos_state])
									+ probPrices.get_price_matched(
										get_lit_probs(position + len_test, data[len_test - 1]),
										data[len_test], data2[len_test]
									);
							state2 = LITERAL_NEXT_STATES[state2];
							next_pos_state = (next_pos_state + 1) & pbMask;
							uint next_rep_match_price = cur_len_char_price
									+ probPrices.get_price_1(isMatch[state2][next_pos_state])
									+ probPrices.get_price_1(isRep[state2]);

							uint offset = cur + len_test + 1 + len_test2;
							while (len_end < offset) {
								opt[++len_end].price = INFINITY_PRICE;
							}

							uint cur_len_price = next_rep_match_price +
									get_rep_price(0, len_test2, state2, next_pos_state);
							Optimal& o = opt[offset];
							if (cur_len_price < o.price) {
								o.price = cur_len_price;
								o.posPrev = cur + len_test + 1;
								o.backPrev = 0;
								o.prev1IsChar = true;
								o.prev2 = true;
								o.posPrev2 = cur;
								o.backPrev2 = cur_back + NUM_REPS;
							}
						}
						offs += 2;
						if (offs == num_pairs)
							break;
						cur_back = matches[offs + 1];
						if (cur_back >= NUM_FULL_DISTANCES)
							pos_slot = get_distance_slot(cur_back);
					}
				}
			}
		}

		return 0;
	}

	inline bool
	change_pair( uint small_dist, uint big_dist )
	{
		return (big_dist >> 7) > small_dist;
	}


	uint
	get_optimum_fast(uint& back_res)
	{
		uint main_len(0);
		uint num_pairs(0);

		if (additionalOffset == 0) {
			main_len = read_match_distances(num_pairs);
		} else {
			main_len = longestMatchLength;
			num_pairs = numPairs;
		}

		uint num_avail = num_avail_;

		back_res = (uint)-1;
		if (num_avail < 2)
			return 1;

		if (num_avail > MATCH_LEN_MAX)
			num_avail = MATCH_LEN_MAX;

		byte const* data = matchFinder.current_pos() - 1;

		uint rep_len(0), rep_index(0);

		for (uint i = 0; i < NUM_REPS; ++i) {
			byte const* data2 = data - (reps[i] + 1);

			uint len = match_lenght(data, data2, num_avail);
			if (len >= numFastBytes) {
				back_res = i;
				move_pos(len - 1);
				return len;
			}

			if (len > rep_len) {
				rep_index = i;
				rep_len = len;
			}
		}

		if (main_len >= numFastBytes) {
			back_res= matches[num_pairs - 1] + NUM_REPS;
			move_pos(main_len - 1);
			return main_len;
		}

		uint main_dist = 0;
		if (main_len >= 2) {
			main_dist = matches[num_pairs - 1];
			while (num_pairs > 2 && main_len == matches[num_pairs - 4] + 1) {
				if (!change_pair(matches[num_pairs - 3], main_dist))
					break;
				num_pairs -= 2;
				main_len = matches[num_pairs - 2];
				main_dist = matches[num_pairs - 1];
			}

			if (main_len == 2 && main_dist >= 0x80)
				main_len = 1;
		}

		if (rep_len >= 2 && (
				(rep_len + 1 >= main_len) ||
				(rep_len + 2 >= main_len && main_dist >= (1 << 9)) ||
				(rep_len + 3 >= main_len && main_dist >= (1 << 25)) )) {
			back_res = rep_index;
			move_pos(rep_len - 1);
			return rep_len;
		}

		longestMatchLength = read_match_distances(num_pairs);
		if (longestMatchLength >= 2) {
			uint new_distance = matches[numPairs - 1];
			if ((longestMatchLength >= main_len && new_distance < main_dist ) ||
				(longestMatchLength == main_len + 1 && !change_pair(main_dist, new_distance)) ||
				(longestMatchLength > main_len + 1) ||
				(longestMatchLength + 1 >= main_len && main_len >= 3 && change_pair(new_distance, main_dist))) {
				return 1;
			}
		}

		data = matchFinder.current_pos() - 1;
		for (uint i = 0; i < NUM_REPS; ++i) {
			byte const* data2 = data - (reps[i] + 1);
			uint len = match_lenght(data, data2, main_len - 1);
			if (len > main_len)
				return 1;
		}

		back_res = main_dist + NUM_REPS;
		move_pos(main_len - 2);

		return main_len;
	}

	bool
	encode(char const*& src_begin, char const* src_end, char*& dest_begin, char* dest_end)
	{
		size_t out_sz = dest_end - dest_begin;
		if (needInit) {
			// Need to write parameters
			if (out_sz >= 5 + sizeof(ulong)) {
				params.write(dest_begin, dest_end);
				needInit = false;
				out_sz -= 5 + sizeof(ulong);
			}
		}
		matchFinder.read_block(src_begin, src_end);

		if (matchFinder.at_stream_end() || !matchFinder.need_more_input()) {
			// encode
			uint curr_pos = curr_pos_64;
			uint start_pos = curr_pos;

			if (curr_pos_64 == 0) {
				if (matchFinder.available_bytes() == 0) {
					return flush(dest_begin, dest_end, curr_pos);
				}
				uint num_pairs;
				read_match_distances(num_pairs);
				rc.encode_bit(&isMatch[state_][0], 0);
				state_ = LITERAL_NEXT_STATES[state_];
				byte cur_byte = matchFinder.get_byte( -additionalOffset);
				rc.encode(litProbs, cur_byte);
				--additionalOffset;
				++curr_pos;
			}

			if (matchFinder.available_bytes() != 0) {
				for (;;) {
					uint pos;
					uint len = fastMode ? get_optimum_fast(pos) : get_optimum(curr_pos, pos);
					uint pos_state = curr_pos & pbMask;
					if (len == 1 && pos == (uint)(-1)) {
						rc.encode_bit(&isMatch[state_][pos_state], 0);
						byte const* data = matchFinder.current_pos() - additionalOffset;
						byte cur_byte = *data;
						LzmaProb* probs = get_lit_probs(curr_pos, *(data - 1));
						if (is_char_state(state_)) {
							rc.encode(probs, cur_byte);
						} else {
							rc.encode_matched(probs, cur_byte, *(data - reps[0] - 1));
						}
						state_ = LITERAL_NEXT_STATES[state_];
					} else {
						rc.encode_bit(&isMatch[state_][pos_state], 1);
						if (pos < NUM_REPS) {
							if (pos == 0) {
								rc.encode_bit(&isRepG0[state_], 0);
								rc.encode_bit(&isRep0Long[state_][pos_state], len == 1 ? 0 : 1);
							} else {
								uint distance = reps[pos];
								rc.encode_bit(&isRepG0[state_], 1);
								if (pos == 1) {
									rc.encode_bit(&isRepG1[state_], 0);
								} else {
									rc.encode_bit(&isRepG1[state_], 1);
									rc.encode_bit(&isRepG2[state_], pos - 2);
									if (pos == 3) {
										reps[3] = reps[2];
									}
									reps[2] = reps[1];
								}
								reps[1] = reps[0];
								reps[0] = distance;
							}
							if (len == 1) {
								state_ = SHORT_REP_NEXT_STATES[state_];
							} else {
								lenEnc.encode(rc, len - MATCH_LEN_MIN, pos_state, probPrices, !fastMode);
								state_ = REP_NEXT_STATES[state_];
							}
						} else {
							rc.encode_bit(&isRep[state_], 0);
							state_ = MATCH_NEXT_STATES[state_];
							lenEnc.encode(rc, len - MATCH_LEN_MIN, pos_state, probPrices, !fastMode);
							pos -= NUM_REPS;
							uint pos_slot = get_slot(pos);
							rc.encode_tree(posSlotEncoder[ get_len_to_pos_state(len) ],
									NUM_POS_SLOT_BITS, pos_slot);

							if (pos_slot >= START_POS_MODEL_INDEX) {
								uint footer_bits = (pos_slot >> 1) - 1;
								uint base = ((2 | (pos_slot & 1)) << footer_bits);
								uint pos_reduced = pos - base;

								if (pos_slot < END_POS_MODEL_INDEX) {
									rc.encode_reverse_tree(posEncoders + base - pos_slot - 1,
											footer_bits, pos_reduced);
								} else {
									rc.encode_direct_bits(pos_reduced >> NUM_ALIGN_BITS,
											footer_bits - NUM_ALIGN_BITS);
									rc.encode_reverse_tree(posAlignEncoder,
											NUM_ALIGN_BITS, pos_reduced & ALIGN_MASK);
									++alignPriceCount;
								}
							}
							reps[3] = reps[2];
							reps[2] = reps[1];
							reps[1] = reps[0];
							reps[0] = pos;
							++matchPriceCount;
						}
					}

					additionalOffset -= len;
					curr_pos += len;
					if (additionalOffset == 0) {
						if (matchPriceCount >= (1 << 7)) {
							fill_distances_prices();
						}
						if (alignPriceCount >= ALIGN_TABLE_SIZE) {
							fill_align_prices();
						}
						if (matchFinder.available_bytes() == 0)
							break;
					}

					// check bytes availability, end of stream, break out of the loop
				}
			}

			curr_pos_64 += curr_pos - start_pos;
			// Output data to buffer
			return flush(dest_begin, dest_end, curr_pos);
		}
		return false;
	}
};

lzma_encoder::lzma_encoder(void* allocator, lzma::lzma_alloc_func af,
		lzma::lzma_free_func ff, lzma_params const& p) :
		pimpl_(new impl(allocator, af, ff, p))
{
}

lzma_encoder::~lzma_encoder()
{
	delete pimpl_;
}

lzma_params const&
lzma_encoder::params() const
{
	return pimpl_->params;
}

bool
lzma_encoder::operator()(char const*& src_begin, char const* src_end, char*& dest_begin, char* dest_end)
{
	return pimpl_->encode(src_begin, src_end, dest_begin, dest_end);
}


} // namespace detail
} // namespace lzma
} // namespace shaman

/**
 * @file lzma_constants.hpp
 *
 *  Created on: 14.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef SHAMAN_LZMA_DETAIL_LZMA_CONSTANTS_HPP_
#define SHAMAN_LZMA_DETAIL_LZMA_CONSTANTS_HPP_

#include <shaman/lzma/lzma.hpp>

namespace shaman {
namespace lzma {
namespace detail {

//@{
/** @name Typedefs */
#ifdef _LZMA_PROB32
typedef uint32_t LzmaProb;
#else
typedef uint16_t LzmaProb;
#endif
//@}

//@{
/** @name Constants */
const lzma::uint CRC_POLY					= 0xedb88320;

const lzma::uint NUM_BIT_MODEL_TOTAL_BITS	= 11;
const lzma::uint BIT_MODEL_TOTAL			= 1 << NUM_BIT_MODEL_TOTAL_BITS;
const lzma::uint NUM_MOVE_REDUCING_BITS		= 4;
const lzma::uint NUM_MOVE_BITS				= 5;
const lzma::uint NUM_BIT_PRICE_SHIFT_BITS	= 4;
const lzma::uint BIT_PRICE					= 1 << NUM_BIT_PRICE_SHIFT_BITS;
const lzma::uint NUM_REPS					= 4;
const lzma::uint NUM_OPTS					= 1 << 12;
const LzmaProb   PROB_INIT_VALUE			= BIT_MODEL_TOTAL >> 1;

const lzma::uint NUM_LEN_TO_POS_STATES		= 4;
const lzma::uint NUM_POS_SLOT_BITS			= 6;
const lzma::uint DIC_LOG_SIZE_MIN 			= 0;
const lzma::uint DIC_LOG_SIZE_MAX 			= 32;
const lzma::uint DIST_TABLE_SIZE_MAX 		= (DIC_LOG_SIZE_MAX * 2);
const lzma::uint NUM_LOG_BITS				= 9 + (lzma::uint)sizeof(size_t) / 2;
const lzma::uint DIC_LOG_SIZE_MAX_COMPRESS	= (NUM_LOG_BITS - 1) * 2 + 7;

const lzma::uint NUM_ALIGN_BITS				= 4;
const lzma::uint ALIGN_TABLE_SIZE			= (1 << NUM_ALIGN_BITS);
const lzma::uint ALIGN_MASK					= (ALIGN_TABLE_SIZE - 1);

const lzma::uint START_POS_MODEL_INDEX		= 4;
const lzma::uint END_POS_MODEL_INDEX		= 14;
const lzma::uint NUM_POS_MODELS				= (END_POS_MODEL_INDEX - START_POS_MODEL_INDEX);

const lzma::uint NUM_FULL_DISTANCES			= (1 << (END_POS_MODEL_INDEX >> 1));

const lzma::uint LEN_NUM_LOW_BITS			= 3;
const lzma::uint LEN_NUM_LOW_SYMBOLS		= 1 << LEN_NUM_LOW_BITS;
const lzma::uint LEN_NUM_MID_BITS			= 3;
const lzma::uint LEN_NUM_MID_SYMBOLS		= 1 << LEN_NUM_MID_BITS;
const lzma::uint LEN_NUM_HIGH_BITS			= 8;
const lzma::uint LEN_NUM_HIGH_SYMBOLS		= 1 << LEN_NUM_HIGH_BITS;

const lzma::uint LEN_NUM_SYMBOLS_TOTAL		= LEN_NUM_LOW_SYMBOLS +
		LEN_NUM_MID_SYMBOLS + LEN_NUM_HIGH_SYMBOLS;

const lzma::uint MATCH_LEN_MIN				= 2;
const lzma::uint MATCH_LEN_MAX				= MATCH_LEN_MIN + LEN_NUM_SYMBOLS_TOTAL - 1;

const lzma::uint NUM_STATES					= 12;
const lzma::uint NUM_PB_STATES_MAX			= 1 << 4;

const lzma::uint RC_BUFFER_SIZE				= 1 << 16;
const lzma::uint BIG_HASH_DIC_LIMIT			= 1 << 24;

const lzma::uint HASH2_SIZE					= 1 << 10;
const lzma::uint HASH3_SIZE					= 1 << 16;
const lzma::uint HASH4_SIZE					= 1 << 20;

const lzma::uint FIX_HASH3_SIZE				= (HASH2_SIZE);
const lzma::uint FIX_HASH4_SIZE				= (HASH2_SIZE + HASH3_SIZE);
const lzma::uint FIX_HASH5_SIZE				= (HASH2_SIZE + HASH3_SIZE + HASH4_SIZE);

const lzma::uint MAX_VAL_FOR_NORMALIZE		= 0xffffffff;
//@}


} // namespace detail
} // namespace lzma
} // namespace shaman


#endif /* SHAMAN_LZMA_DETAIL_LZMA_CONSTANTS_HPP_ */

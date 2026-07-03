/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief L1Block.sol calldata and storage byte layout constants.
 * @file L1BlockLayout.h
 *
 * Mirrors op-geth contracts-bedrock L1Block.sol non-standard slot packing and
 * setL1BlockValues* deposit calldata layout.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bcos::evm::l1block
{

inline constexpr size_t EVM_WORD_SIZE = 32;
inline constexpr size_t EVM_ADDRESS_SIZE = 20;

// setL1BlockValues* calldata field offsets (after 4-byte function selector).
inline constexpr size_t ATTR_OFFSET_BASE_FEE_SCALAR = 4;
inline constexpr size_t ATTR_OFFSET_BLOB_BASE_FEE_SCALAR = 8;
inline constexpr size_t ATTR_OFFSET_SEQUENCE_NUMBER = 12;
inline constexpr size_t ATTR_OFFSET_TIMESTAMP = 20;
inline constexpr size_t ATTR_OFFSET_L1_BLOCK_NUMBER = 28;
inline constexpr size_t ATTR_OFFSET_L1_BASE_FEE = 36;
inline constexpr size_t ATTR_OFFSET_L1_BLOB_BASE_FEE = 68;
inline constexpr size_t ATTR_OFFSET_HASH = 100;
inline constexpr size_t ATTR_OFFSET_BATCHER_HASH = 132;
inline constexpr size_t ATTR_OFFSET_OPERATOR_FEE_SCALAR = 164;
inline constexpr size_t ATTR_OFFSET_OPERATOR_FEE_CONSTANT = 168;
inline constexpr size_t ATTR_OFFSET_DA_FOOTPRINT_GAS_SCALAR = 176;

// Slot 0: bytes[16..24)=timestamp, bytes[24..32)=l1BlockNumber
inline constexpr size_t SLOT0_TIMESTAMP_BYTE = 16;
inline constexpr size_t SLOT0_NUMBER_BYTE = 24;

// Slot 3: bytes[16..24)=baseFeeScalar + blobBaseFeeScalar, bytes[24..32)=sequenceNumber
inline constexpr size_t SLOT3_SCALARS_BYTE = 16;
inline constexpr size_t SLOT3_SEQUENCE_BYTE = 24;

// Slot 8: bytes[18..20)=daFootprint(Jovian), [20..24)=operatorScalar, [24..32)=operatorConstant
inline constexpr size_t SLOT8_DA_FOOTPRINT_BYTE = 18;
inline constexpr size_t SLOT8_OPERATOR_SCALAR_BYTE = 20;
inline constexpr size_t SLOT8_OPERATOR_CONSTANT_BYTE = 24;

// gasPayingToken() return encoding for default ETH gas token chains.
inline constexpr std::array<uint8_t, EVM_ADDRESS_SIZE> ETH_GAS_PAYING_TOKEN_SENTINEL = {0xee, 0xee,
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
    0xee, 0xee};
inline constexpr size_t GAS_PAYING_TOKEN_NAME_OFFSET = 18;

}  // namespace bcos::evm::l1block

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief L1Block predeploy storage packing and calldata parsing.
 * @file L1BlockStorage.h
 *
 * Slot packing mirrors op-geth L1Block.sol (non-standard: scalars share a word instead of
 * full ABI slots). setL1BlockValues* calldata is a flat big-endian tuple, not standard
 * ABI encoding — offsets are fixed in parseIsthmus/JovianL1Attributes.
 *
 * Slot map (OpStackConstants):
 *   0 number+timestamp | 1 l1BaseFee | 2 hash | 3 fee scalars+sequence
 *   4 batcherHash | 5 feeOverhead(legacy) | 6 feeScalar(legacy) | 7 blobBaseFee
 *   8 operatorFee+daFootprint(Jovian) | 9 featureEnabled mapping base
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>
#include <string_view>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{
// L1 block attributes injected by the sequencer deposit tx (Isthmus+).
// Field order matches setL1BlockValuesIsthmus calldata layout in L1BlockStorage.cpp:
//   baseFeeScalar, blobBaseFeeScalar → slot 3
//   sequenceNumber → slot 3 low bytes; timestamp/l1BlockNumber → slot 0
//   l1BaseFee → slot 1; hash → slot 2; batcherHash → slot 4
//   l1BlobBaseFee → slot 7; operatorFee* → slot 8
struct IsthmusL1Attributes
{
    uint32_t baseFeeScalar{0};
    uint32_t blobBaseFeeScalar{0};
    uint64_t sequenceNumber{0};
    uint64_t timestamp{0};
    uint64_t l1BlockNumber{0};
    evmc_bytes32 l1BaseFee{};
    evmc_bytes32 l1BlobBaseFee{};
    evmc_bytes32 hash{};
    evmc_bytes32 batcherHash{};
    uint32_t operatorFeeScalar{0};
    uint64_t operatorFeeConstant{0};
};

// Jovian extends Isthmus with DA footprint gas scalar (slot 8 bytes[18..20)).
struct JovianL1Attributes : IsthmusL1Attributes
{
    uint16_t daFootprintGasScalar{0};
};

// Parse setL1BlockValues* calldata (4-byte selector + payload; see OpStackConstants length).
std::optional<IsthmusL1Attributes> parseIsthmusL1Attributes(bytesConstRef calldata);
std::optional<JovianL1Attributes> parseJovianL1Attributes(bytesConstRef calldata);

// slot 0: bytes[16..24)=timestamp, bytes[24..32)=l1BlockNumber
evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number);
// slot 3: bytes[16..20)=baseFeeScalar, bytes[20..24)=blobBaseFeeScalar,
// bytes[24..32)=sequenceNumber
evmc_bytes32 packL1FeeScalarsSlot(
    uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar, uint64_t sequenceNumber);
// slot 8: bytes[18..20)=daFootprintGasScalar(Jovian), bytes[20..24)=operatorFeeScalar,
// bytes[24..32)=operatorFeeConstant
evmc_bytes32 packOperatorFeeParams(
    uint32_t operatorFeeScalar, uint64_t operatorFeeConstant, uint16_t daFootprintGasScalar = 0);

u256 unpackNumber(evmc_bytes32 const& packed);
u256 unpackTimestamp(evmc_bytes32 const& packed);
u256 unpackSequenceNumber(evmc_bytes32 const& packed);
u256 unpackBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackBlobBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeConstant(evmc_bytes32 const& packed);
u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed);

// ABI-encode getter return values (string, address, gasPayingToken sentinel).
bytes encodeAbiString(std::string_view value);
bytes encodeAbiAddress(evmc_address const& address);
bytes encodeGasPayingToken();

// isFeatureEnabled(key): mapping lookup via keccak256(abi.encode(key,
// L1_FEATURE_ENABLED_MAPPING_SLOT)).
bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key);
}  // namespace bcos::evm

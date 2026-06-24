#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>
#include <string_view>

namespace bcos::evm
{
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

std::optional<IsthmusL1Attributes> parseIsthmusL1Attributes(bytesConstRef calldata);

evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number);
evmc_bytes32 packL1FeeScalarsSlot(
    uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar, uint64_t sequenceNumber);
evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant);

u256 unpackNumber(evmc_bytes32 const& packed);
u256 unpackTimestamp(evmc_bytes32 const& packed);
u256 unpackSequenceNumber(evmc_bytes32 const& packed);
u256 unpackBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackBlobBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeConstant(evmc_bytes32 const& packed);
u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed);

bytes encodeAbiString(std::string_view value);
bytes encodeAbiAddress(evmc_address const& address);
bytes encodeGasPayingToken();

bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key);
}  // namespace bcos::evm

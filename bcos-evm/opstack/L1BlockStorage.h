#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>

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

evmc_bytes32 packL1FeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar);
evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant);

u256 unpackBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackBlobBaseFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeScalar(evmc_bytes32 const& packed);
u256 unpackOperatorFeeConstant(evmc_bytes32 const& packed);
}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth-eest-test/BlockValidationErrors.h"
#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm::reference_tests
{

/// EIP-1559 base fee from parent header. Denominator 8, elasticity 2.
inline uint64_t calcBaseFee(uint64_t parentGasLimit, uint64_t parentGasUsed, uint64_t parentBaseFee)
{
    constexpr uint64_t ELASTICITY = 2;
    constexpr uint64_t DENOM = 8;
    uint64_t const target = parentGasLimit / ELASTICITY;
    if (parentGasUsed == target)
        return parentBaseFee;
    if (parentGasUsed > target)
    {
        uint64_t const delta = parentBaseFee * (parentGasUsed - target) / target / DENOM;
        return parentBaseFee + std::max<uint64_t>(delta, 1);
    }
    uint64_t const delta = parentBaseFee * (target - parentGasUsed) / target / DENOM;
    return parentBaseFee - delta;
}

// Forward decls for blob helpers (implemented in Task 1.2, same header).
uint64_t calcExcessBlobGas(evmc_revision rev, BlobSchedule const& schedule,
    uint64_t parentBlobGasUsed, uint64_t parentExcessBlobGas);
BlobParams blobParamsFor(BlobSchedule const& schedule, std::string_view network);

/// Validate block-level consensus rules unrelated to individual transactions.
/// Returns nullopt if valid, else a BlockError code string.
inline std::optional<std::string> validateBlock(evmc_revision rev, BlobSchedule const& schedule,
    TestBlock const& tb, TestBlockHeader const* parent)
{
    auto const& h = tb.expectedBlockHeader;

    if (parent == nullptr)  // #1
        return BlockError::INVALID_BLOCK_PARENT;
    if (h.blockNumber != parent->blockNumber + 1)  // #2
        return BlockError::INVALID_BLOCK_NUMBER;
    if (h.gasUsed > h.gasLimit)  // #3
        return BlockError::INCORRECT_BLOCK_FORMAT;

    uint64_t const pgl = static_cast<uint64_t>(parent->gasLimit);
    uint64_t const gl = static_cast<uint64_t>(h.gasLimit);
    uint64_t const maxDelta = pgl / 1024;
    if (gl >= pgl + maxDelta)  // #4
        return BlockError::INVALID_GASLIMIT;
    if (gl + maxDelta <= pgl)  // #5
        return BlockError::INVALID_GASLIMIT;
    if (gl < 5000)  // #6
        return BlockError::INVALID_GASLIMIT;

    if (h.timestamp <= parent->timestamp)  // #7
        return BlockError::INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT;

    if (rev >= EVMC_PARIS && tb.hasOmmers)  // #8
        return BlockError::INCORRECT_BLOCK_FORMAT;

    if (h.extraData.size() > 32)  // #9
        return BlockError::INCORRECT_BLOCK_FORMAT;

    if (rev >= EVMC_LONDON)  // #10
    {
        uint64_t const expected =
            calcBaseFee(pgl, static_cast<uint64_t>(parent->gasUsed), parent->baseFeePerGas);
        if (h.baseFeePerGas != expected)
            return BlockError::INVALID_BASEFEE_PER_GAS;
    }

    // #11-#13 blob checks: added in Task 1.2.
    // #14 withdrawals parse, #15 EIP-7934 rlp size: added in Task 1.3.
    (void)schedule;
    return std::nullopt;
}

}  // namespace bcos::evm::reference_tests

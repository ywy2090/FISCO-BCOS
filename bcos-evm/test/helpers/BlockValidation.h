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

// ── Blob helpers (EIP-4844) ─────────────────────────────────
inline BlobParams blobParamsFor(BlobSchedule const& schedule, std::string_view network)
{
    if (auto it = schedule.find(std::string(network)); it != schedule.end())
        return it->second;
    // Defaults keyed by fork (used when fixture omits an explicit schedule).
    if (network == "Prague")
        return BlobParams{6, 9, 5007716};
    if (network == "Osaka" || network == "Amsterdam")
        return BlobParams{6, 9, 5007716};
    return BlobParams{3, 6, 3338477};  // Cancun
}

inline uint64_t fakeExponential(uint64_t factor, uint64_t numerator, uint64_t denominator)
{
    // Sum_{i>=0} factor * (numerator/denominator)^i / i!  (integer approximation, EIP-4844).
    unsigned __int128 i = 1;
    unsigned __int128 output = 0;
    unsigned __int128 numeratorAccum = static_cast<unsigned __int128>(factor) * denominator;
    while (numeratorAccum > 0)
    {
        output += numeratorAccum;
        numeratorAccum =
            (numeratorAccum * numerator) / (static_cast<unsigned __int128>(denominator) * i);
        ++i;
    }
    return static_cast<uint64_t>(output / denominator);
}

inline uint64_t computeBlobGasPrice(BlobParams const& p, uint64_t excessBlobGas)
{
    constexpr uint64_t MIN_BLOB_BASE_FEE = 1;
    return fakeExponential(MIN_BLOB_BASE_FEE, excessBlobGas, p.baseFeeUpdateFraction);
}

inline uint64_t maxBlobGasPerBlock(BlobParams const& p)
{
    return uint64_t(p.max) * GAS_PER_BLOB;
}

/// evmone: calc_excess_blob_gas (Cancun/Prague form; EIP-7918 Osaka refinement deferred to M3).
inline uint64_t calcExcessBlobGas(evmc_revision /*rev*/, BlobSchedule const& schedule,
    uint64_t parentBlobGasUsed, uint64_t parentExcessBlobGas)
{
    BlobParams const p = blobParamsFor(schedule, "Cancun");
    uint64_t const targetGas = uint64_t(p.target) * GAS_PER_BLOB;
    uint64_t const sum = parentExcessBlobGas + parentBlobGasUsed;
    return sum < targetGas ? 0 : sum - targetGas;
}

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

    auto const blockTs = static_cast<uint64_t>(h.timestamp);
    auto const parentTs = static_cast<uint64_t>(parent->timestamp);
    if (blockTs <= parentTs)  // #7 (uint64: UINT64_MAX timestamps)
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

    if (rev >= EVMC_CANCUN)
    {
        if (!tb.inputBlobGasUsed.has_value() || !tb.inputExcessBlobGas.has_value())  // #11
            return BlockError::INCORRECT_BLOCK_FORMAT;
        uint64_t const expectedExcess = calcExcessBlobGas(rev, schedule,  // #12
            parent->blobGasUsed.value_or(0), parent->excessBlobGas.value_or(0));
        if (tb.inputExcessBlobGas.value() != expectedExcess)
            return BlockError::INCORRECT_EXCESS_BLOB_GAS;
    }
    else if (tb.inputBlobGasUsed.has_value() || tb.inputExcessBlobGas.has_value())  // #13
        return BlockError::INCORRECT_BLOCK_FORMAT;

    if (!tb.withdrawalsParseSuccess)  // #14
        return BlockError::INCORRECT_BLOCK_FORMAT;

    constexpr size_t MAX_RLP_BLOCK_SIZE = 10u * 1024 * 1024 - 2u * 1024 * 1024;  // 8 MiB (EIP-7934)
    if (rev >= EVMC_OSAKA && tb.rlpSize > MAX_RLP_BLOCK_SIZE)                    // #15
        return BlockError::RLP_BLOCK_LIMIT_EXCEEDED;

    return std::nullopt;
}

}  // namespace bcos::evm::reference_tests

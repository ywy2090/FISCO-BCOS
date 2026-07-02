#pragma once

/*
 * @brief Compile-time precompile metadata (address suffix, fork, base gas).
 *
 * Used by transaction-executor PrecompileTraits lookup; gas_base/per_word == -1
 * marks entries with revision-specific pricers (modexp, BLS MSM, etc.).
 */

#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>

namespace bcos::evm::precompiles
{

struct PrecompileTraits
{
    uint16_t address_suffix;
    evmc_revision since;
    evmc_revision deprecated_in;
    int64_t gas_base;
    int64_t gas_per_word;
};

/// Static catalog aligned with Ethereum fork schedule; -1 gas fields → custom pricer.
inline constexpr PrecompileTraits ALL_ETHEREUM_PRECOMPILES[] = {
    {0x0001, EVMC_FRONTIER, EVMC_MAX_REVISION, 3000, 0},
    {0x0002, EVMC_FRONTIER, EVMC_MAX_REVISION, 60, 12},
    {0x0003, EVMC_FRONTIER, EVMC_MAX_REVISION, 600, 120},
    {0x0004, EVMC_FRONTIER, EVMC_MAX_REVISION, 15, 3},
    {0x0005, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1, -1},
    {0x0006, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 150, 0},
    {0x0007, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 6000, 0},
    {0x0008, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1, -1},
    {0x0009, EVMC_ISTANBUL, EVMC_MAX_REVISION, -1, -1},
    {0x000a, EVMC_CANCUN, EVMC_MAX_REVISION, 50000, 0},
    {0x000b, EVMC_PRAGUE, EVMC_MAX_REVISION, 500, 0},
    {0x000c, EVMC_PRAGUE, EVMC_MAX_REVISION, -1, -1},
    {0x000d, EVMC_PRAGUE, EVMC_MAX_REVISION, 800, 0},
    {0x000e, EVMC_PRAGUE, EVMC_MAX_REVISION, -1, -1},
    {0x000f, EVMC_PRAGUE, EVMC_MAX_REVISION, -1, -1},
    {0x0010, EVMC_PRAGUE, EVMC_MAX_REVISION, 5500, 0},
    {0x0011, EVMC_PRAGUE, EVMC_MAX_REVISION, 75000, 0},
    {0x0100, EVMC_OSAKA, EVMC_MAX_REVISION, 6900, 0},
};

constexpr uint16_t toLookupIndex(const evmc_address& addr) noexcept
{
    return static_cast<uint16_t>((addr.bytes[18] << 8) | addr.bytes[19]);
}

constexpr const PrecompileTraits* findPrecompile(
    evmc_revision rev, const evmc_address& addr) noexcept
{
    const auto idx = toLookupIndex(addr);
    for (const auto& t : ALL_ETHEREUM_PRECOMPILES)
    {
        if (t.address_suffix == idx)
        {
            return (t.since <= rev && rev <= t.deprecated_in) ? &t : nullptr;
        }
    }
    return nullptr;
}

inline bool isEthereumPrecompile(evmc_revision rev, const evmc_address& addr) noexcept
{
    return findPrecompile(rev, addr) != nullptr;
}

inline bool hasRevisionAwarePricer(const PrecompileTraits* t) noexcept
{
    return t && (t->gas_base == -1 || t->gas_per_word == -1);
}

}  // namespace bcos::evm::precompiles

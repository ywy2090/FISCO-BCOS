/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Single-source active precompile set (warm + dispatch gating).
 *
 * Used by CallTargetResolver and tx-entry warming so classification and
 * EIP-2929 warm rules share the same fork/EIP activation logic.
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm::precompiled
{

/// True when @p addr is a known low-byte precompile index (0x01–0x11).
inline bool isLowPrecompile(evmc_address const& addr) noexcept
{
    auto const suffix = precompileSuffix(addr);
    return suffix.has_value() && *suffix <= ETH_PRECOMPILE_INDEX_LAST;
}

inline bool isP256Precompile(evmc_address const& addr) noexcept
{
    auto const suffix = precompileSuffix(addr);
    return suffix.has_value() && *suffix == static_cast<uint16_t>(P256VERIFY_PRECOMPILE_INDEX);
}

/// Fork/EIP gate: must pass before EthPrecompiles dispatch in production.
inline bool isActivePrecompile(
    bcos::evm::RevisionConfig const& cfg, evmc_address const& addr) noexcept
{
    if (isP256Precompile(addr))
    {
        return cfg.revision >= EVMC_OSAKA && cfg.eip7212;
    }
    if (!isLowPrecompile(addr))
    {
        return false;
    }
    auto const suffix = addr.bytes[19];
    if (suffix >= ETH_PRECOMPILE_INDEX_FIRST && suffix <= 0x04)
    {
        return cfg.revision >= EVMC_FRONTIER;
    }
    if (suffix >= 0x05 && suffix <= 0x08)
    {
        return cfg.revision >= EVMC_BYZANTIUM;
    }
    if (suffix == CLASSIC_PRECOMPILE_INDEX_LAST)
    {
        return cfg.revision >= EVMC_ISTANBUL;
    }
    if (suffix == POINT_EVALUATION_PRECOMPILE_INDEX)
    {
        return cfg.revision >= EVMC_CANCUN;
    }
    if (suffix >= BLS_PRECOMPILE_INDEX_FIRST && suffix <= BLS_PRECOMPILE_INDEX_LAST)
    {
        return cfg.revision >= EVMC_PRAGUE && cfg.eip2537;
    }
    return false;
}

/// Iterate all precompiles active at @p cfg (tx warm-up, access-list helpers).
template <typename Consumer>
void forEachActivePrecompile(bcos::evm::RevisionConfig const& cfg, Consumer&& consume)
{
    static constexpr unsigned precompileHi = sizeof(evmc_address) - 1;
    for (uint8_t i = ETH_PRECOMPILE_INDEX_FIRST; i <= ETH_PRECOMPILE_INDEX_LAST; ++i)
    {
        evmc_address precompile{};
        precompile.bytes[precompileHi] = i;
        if (isActivePrecompile(cfg, precompile))
        {
            consume(precompile);
        }
    }
    evmc_address p256{};
    p256.bytes[18] = static_cast<uint8_t>(P256VERIFY_PRECOMPILE_INDEX >> 8);
    p256.bytes[19] = static_cast<uint8_t>(P256VERIFY_PRECOMPILE_INDEX & 0xFF);
    if (isActivePrecompile(cfg, p256))
    {
        consume(p256);
    }
}

}  // namespace bcos::evm::precompiled

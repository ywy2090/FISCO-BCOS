/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Single-source active precompile set (warm + dispatch).
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm::precompiled
{

inline bool isHigh18BytesZero(evmc_address const& addr) noexcept
{
    for (size_t i = 0; i < 18; ++i)
    {
        if (addr.bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

inline bool isLowPrecompile(evmc_address const& addr) noexcept
{
    if (!isHigh18BytesZero(addr))
    {
        return false;
    }
    return addr.bytes[18] == 0x00 && addr.bytes[19] >= ETH_PRECOMPILE_INDEX_FIRST &&
           addr.bytes[19] <= ETH_PRECOMPILE_INDEX_LAST;
}

inline bool isP256Precompile(evmc_address const& addr) noexcept
{
    return isHigh18BytesZero(addr) &&
           addr.bytes[18] == static_cast<uint8_t>(P256VERIFY_PRECOMPILE_INDEX >> 8) &&
           addr.bytes[19] == static_cast<uint8_t>(P256VERIFY_PRECOMPILE_INDEX & 0xFF);
}

inline bool isActivePrecompile(
    bcos::evm_standard::RevisionConfig const& cfg, evmc_address const& addr) noexcept
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
    if (suffix >= ETH_PRECOMPILE_INDEX_FIRST && suffix <= CLASSIC_PRECOMPILE_INDEX_LAST)
    {
        return cfg.revision >= EVMC_BERLIN;
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

template <typename Consumer>
void forEachActivePrecompile(bcos::evm_standard::RevisionConfig const& cfg, Consumer&& consume)
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

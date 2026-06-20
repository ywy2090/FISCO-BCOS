/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Revision-gated active precompile address set (P0-6).
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include <evmc/evmc.h>

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
    return addr.bytes[18] == 0x00 && addr.bytes[19] >= 0x01 && addr.bytes[19] <= 0x11;
}

inline bool isP256Precompile(evmc_address const& addr) noexcept
{
    return isHigh18BytesZero(addr) && addr.bytes[18] == 0x01 && addr.bytes[19] == 0x00;
}

inline bool isActivePrecompile(evmc_revision revision,
    bcos::evm_standard::RevisionConfig const& cfg, evmc_address const& addr) noexcept
{
    if (isP256Precompile(addr))
    {
        return revision >= EVMC_OSAKA && cfg.eip7212;
    }
    if (!isLowPrecompile(addr))
    {
        return false;
    }
    auto const suffix = addr.bytes[19];
    if (suffix >= 0x01 && suffix <= 0x0a)
    {
        return true;
    }
    if (suffix >= 0x0b && suffix <= 0x11)
    {
        return revision >= EVMC_PRAGUE;
    }
    return false;
}

}  // namespace bcos::evm::precompiled

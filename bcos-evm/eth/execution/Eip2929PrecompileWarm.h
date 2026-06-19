/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-2929 active precompile address enumeration by fork revision.
 */

#pragma once

#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm::execution
{

template <typename AddressConsumer>
void forEachActivePrecompileAddress(evmc_revision revision, AddressConsumer&& consume)
{
    static constexpr unsigned precompileHi = sizeof(evmc_address) - 1;
    for (uint8_t i = 1; i <= 9; ++i)
    {
        evmc_address precompile{};
        precompile.bytes[precompileHi] = i;
        consume(precompile);
    }
    if (revision >= EVMC_CANCUN)
    {
        evmc_address precompile{};
        precompile.bytes[precompileHi] = 0x0a;
        consume(precompile);
    }
    if (revision >= EVMC_PRAGUE)
    {
        for (uint8_t i = 0x0b; i <= 0x11; ++i)
        {
            evmc_address precompile{};
            precompile.bytes[precompileHi] = i;
            consume(precompile);
        }
    }
    if (revision >= EVMC_OSAKA)
    {
        evmc_address precompile{};
        precompile.bytes[18] = 0x01;
        precompile.bytes[19] = 0x00;
        consume(precompile);
    }
}

}  // namespace bcos::evm::execution

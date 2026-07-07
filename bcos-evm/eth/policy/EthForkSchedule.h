/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Ethereum mainnet EVM hardfork activation by block number.
 * @file EthForkSchedule.h
 *
 * Maps block height to evmc_revision (geth mainnet chain config). Temporal OP Stack
 * forks live in opstack/policy/OpStackForkSchedule.h; EIP flag derivation lives in
 * eth/core/RevisionConfig.h (revisionConfigFromRevision).
 */

#pragma once

#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm
{

inline constexpr int64_t ETH_MAINNET_PARIS_BLOCK = 15'537'394;
inline constexpr int64_t ETH_MAINNET_SHANGHAI_BLOCK = 17'034'870;
inline constexpr int64_t ETH_MAINNET_CANCUN_BLOCK = 19'426'587;
inline constexpr int64_t ETH_MAINNET_PRAGUE_BLOCK = 22'000'000;
inline constexpr int64_t ETH_MAINNET_OSAKA_BLOCK = 25'000'000;

inline evmc_revision evmcRevisionFromBlockNumber(int64_t blockNum) noexcept
{
    if (blockNum >= ETH_MAINNET_OSAKA_BLOCK)
    {
        return EVMC_OSAKA;
    }
    if (blockNum >= ETH_MAINNET_PRAGUE_BLOCK)
    {
        return EVMC_PRAGUE;
    }
    if (blockNum >= ETH_MAINNET_CANCUN_BLOCK)
    {
        return EVMC_CANCUN;
    }
    if (blockNum >= ETH_MAINNET_SHANGHAI_BLOCK)
    {
        return EVMC_SHANGHAI;
    }
    if (blockNum >= ETH_MAINNET_PARIS_BLOCK)
    {
        return EVMC_PARIS;
    }
    return EVMC_LONDON;
}

}  // namespace bcos::evm

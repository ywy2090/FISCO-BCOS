/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Eth apply-layer EVMCResult construction helpers.
 * @file EthEvmResult.h
 */

#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <bcos-protocol/TransactionStatus.h>
#include <evmc/evmc.h>

namespace bcos::evm
{

inline EVMCResult makeEvmcResult(protocol::TransactionStatus status,
    evmc_status_code evmcStatus = EVMC_FAILURE, int64_t gasLeft = 0)
{
    evmc_result result{};
    result.status_code = evmcStatus;
    result.gas_left = gasLeft;
    return EVMCResult(result, status);
}

}  // namespace bcos::evm

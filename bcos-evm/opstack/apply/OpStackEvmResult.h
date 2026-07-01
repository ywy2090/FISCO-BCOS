/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack apply-layer EVMCResult construction helpers.
 * @file OpStackEvmResult.h
 */

#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

inline EVMCResult makeOutOfGasLimitResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
}

inline EVMCResult makeInternalErrorResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_INTERNAL_ERROR;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::Unknown);
}

}  // namespace bcos::evm

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Execute-entry balance and EIP-7623 floor gas precheck for Op Stack txs.
 * @file OpStackFloorGasPrecheck.h
 *
 * Runs before EVM execution. Rejects when:
 *   balance < txValue  (unless skipTransactionChecks)
 *   gasLimit < floorDataGas(inputData)
 *
 * floorDataGasOut receives the computed floor for downstream post-execute settlement.
 */

#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <optional>

struct evmc_message;

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{

/// Input bundle for execute-entry floor gas and balance prechecks (before EVM runs).
struct OpStackFloorGasPrecheckInput
{
    evmc_message const& message;
    state::State& state;
    uint64_t gasLimit;
    bool skipTransactionChecks;
    bcos::bytesConstRef inputData;
    uint64_t& floorDataGasOut;
};

std::optional<EVMCResult> opStackFloorGasPrecheck(OpStackFloorGasPrecheckInput const& input);
}  // namespace bcos::evm

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execution gas metering for Eth normal txs.
 * @file EthTxFinalize.h
 */

#pragma once

#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"

namespace bcos::evm
{

struct EthMessageResult;

struct EthTxFinalizeResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
};

/// Intrinsic or gas-afford rejection before EVM runs; no gas charged.
bool isEthPreExecutionReject(StateTransitionExitKind exitKind) noexcept;

/// Revert checkpoint after buyGas; zero gasUsed and publish state diff.
void abortEthAfterBuyGas(
    StateTransitionContext& ctx, EthMessageResult& output, int64_t originalGasLimit);

/// Post-execution gasUsed/gasRemaining for Eth normal txs (EIP-7623 via settlement snapshot).
EthTxFinalizeResult finalizeEthNormal(StateTransitionContext const& ctx,
    StateTransitionExitKind exitKind, gas::TxGasSettlementContext const& snapshot,
    bool topLevelIncludedTxVmError);

}  // namespace bcos::evm

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief State-based Eth fee settlement (buyGas / refundGas).
 * @file EthFeeSettlement.h
 */

#pragma once
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{
struct EthTxFinalizeResult;

struct EthFeeSettlement
{
    task::Task<bool> buyGas(EthSettlementProjection view);
    task::Task<gas::FeeSettlementPlan> refundGas(
        EthSettlementProjection& view, EthTxFinalizeResult const& settled);
};
}  // namespace bcos::evm

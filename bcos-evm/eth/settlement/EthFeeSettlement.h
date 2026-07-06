/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief State-based Eth fee settlement (buyGas / refundGas).
 * @file EthFeeSettlement.h
 */

#pragma once
#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{
struct EthSettlementProjection;

struct EthFeeSettlement
{
    task::Task<bool> buyGas(EthSettlementProjection view);
    task::Task<gas::FeeSettlementPlan> refundGas(
        EthSettlementProjection& view, gas::PostExecuteGasResult const& settled);
};
}  // namespace bcos::evm

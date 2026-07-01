/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack post-settlement fee plan (State-free).
 * @file OpStackPostSettlementPlan.h
 */
#pragma once

#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"
#include <bcos-utilities/Common.h>

namespace bcos::evm
{

struct OpStackPostSettlementInputs
{
    gas::FeeInputs fee;
    int64_t gasUsed{0};
    int64_t gasRemaining{0};
    uint64_t blockTime{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
};

struct OpStackPostSettlementPlan
{
    gas::FeeSettlementPlan core1559;
    bcos::u256 l1FeeRouted{0};
    bcos::u256 operatorFeeCharged{0};
    bcos::u256 senderOperatorRefund{0};
};

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept;

}  // namespace bcos::evm

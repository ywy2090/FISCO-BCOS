/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack L1/operator fee params and fork-aware cost selectors.
 * @file OpStackFeeParams.h
 */

#pragma once

#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include <bcos-utilities/Common.h>
#include <functional>

namespace bcos::evm
{

struct OpStackFeeParams
{
    u256 l1BaseFee;
    u256 l1BlobBaseFee;
    u256 l1BaseFeeScalar;
    u256 l1BlobBaseFeeScalar;
    u256 operatorFeeScalar;
    u256 operatorFeeConstant;
};

u256 l1CostFjord(RollupCostData const& data, OpStackFeeParams const& params);
u256 operatorCostIsthmus(uint64_t gas, OpStackFeeParams const& params);
u256 operatorCostJovian(uint64_t gas, OpStackFeeParams const& params);

OpStackFeeParams loadOpStackFeeParams(state::StateView const& state);

using L1CostFunc = std::function<u256(RollupCostData const&, uint64_t blockTime)>;
using OperatorCostFunc = std::function<u256(uint64_t gas, uint64_t blockTime)>;

L1CostFunc selectL1CostFunc(OpStackForkSchedule const& schedule, OpStackFeeParams const& params);
OperatorCostFunc selectOperatorCostFunc(
    OpStackForkSchedule const& schedule, OpStackFeeParams const& params);
L1CostFunc wireL1CostFuncWithState(
    OpStackForkSchedule const& schedule, state::StateView const& state);
OperatorCostFunc wireOperatorCostFuncWithState(
    OpStackForkSchedule const& schedule, state::StateView const& state);

}  // namespace bcos::evm

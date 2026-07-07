/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack L1/operator fee params and fork-aware cost selectors.
 * @file OpStackFeeParams.h
 *
 * Params are read from the L1Block predeploy (loadOpStackFeeParams). Cost functions are
 * injected via OpStackFeeHooks; fork gates live in OpStackForkSchedule.
 *
 * Fjord L1 data fee (l1CostFjord):
 *   scaledL1BaseFee     = l1BaseFeeScalar * l1BaseFee
 *   calldataCostPerByte = scaledL1BaseFee * FJORD_L1_FEE_CALLDATA_BYTE_NUMERATOR
 *   blobCostPerByte     = l1BlobBaseFeeScalar * l1BlobBaseFee
 *   l1FeeScaled         = calldataCostPerByte + blobCostPerByte
 *   l1Cost              = estimatedDASizeScaled(data) * l1FeeScaled / FJORD_DIVISOR
 *
 * Isthmus operator fee (operatorCostIsthmus):
 *   operatorCost = gas * operatorFeeScalar / OPERATOR_FEE_SCALAR_DIVISOR + operatorFeeConstant
 *
 * Jovian operator fee (operatorCostJovian):
 *   operatorCost = gas * operatorFeeScalar * JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER
 *                  + operatorFeeConstant
 */

#pragma once

#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <functional>

namespace bcos::evm::state
{
class StateView;
}

namespace bcos::evm
{

struct OpStackForkSchedule;

/// On-chain fee scalars from L1Block predeploy storage (see OpStackConstants.h slots).
struct OpStackFeeParams
{
    u256 l1BaseFee;             ///< L1 base fee per gas (slot 1)
    u256 l1BlobBaseFee;         ///< L1 blob base fee (slot 7, Ecotone+)
    u256 l1BaseFeeScalar;       ///< Ecotone+ L1 base fee scalar (slot 3)
    u256 l1BlobBaseFeeScalar;   ///< Ecotone+ L1 blob base fee scalar (slot 3)
    u256 operatorFeeScalar;     ///< Isthmus+ operator fee scalar (slot 8)
    u256 operatorFeeConstant;   ///< Isthmus+ operator fee constant wei (slot 8)
    u256 daFootprintGasScalar;  ///< Jovian receipt metadata scalar (slot 8)
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

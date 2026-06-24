#pragma once

#include "bcos-evm/eth/state/EvmStateReader.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-evm/opstack/RollupCost.h"
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

OpStackFeeParams loadOpStackFeeParams(state::EvmStateReader const& state);

using L1CostFunc = std::function<u256(RollupCostData const&, uint64_t blockTime)>;
using OperatorCostFunc = std::function<u256(uint64_t gas, uint64_t blockTime)>;

L1CostFunc selectL1CostFunc(OpStackForkSchedule const& schedule, OpStackFeeParams const& params);
OperatorCostFunc selectOperatorCostFunc(
    OpStackForkSchedule const& schedule, OpStackFeeParams const& params);
L1CostFunc wireL1CostFuncWithState(
    OpStackForkSchedule const& schedule, state::EvmStateReader const& state);
OperatorCostFunc wireOperatorCostFuncWithState(
    OpStackForkSchedule const& schedule, state::EvmStateReader const& state);

}  // namespace bcos::evm

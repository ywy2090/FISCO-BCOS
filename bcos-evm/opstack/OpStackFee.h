#pragma once

#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/RollupCost.h"
#include <bcos-utilities/Common.h>

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

OpStackFeeParams loadOpStackFeeParams(state::StateView const& state);

}  // namespace bcos::evm

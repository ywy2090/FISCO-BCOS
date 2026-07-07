#include "bcos-evm/opstack/fee/OpStackFeeParams.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/l1/L1BlockStorage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-utilities/Common.h"
#include "eth/state/StateKeyHash.hpp"
#include "eth/state/StateView.hpp"
#include "opstack/fee/RollupCost.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace bcos::evm
{
namespace
{
evmc_bytes32 slotKey(u256 slot)
{
    return state::toEvmC(slot);
}

using FeeParamsResolver = std::function<OpStackFeeParams(uint64_t blockTime)>;

struct L1CostCache
{
    uint64_t forBlock = std::numeric_limits<uint64_t>::max();
    bool fjordActive = false;
    OpStackFeeParams params{};
};

struct OperatorCostCache
{
    uint64_t forBlock = std::numeric_limits<uint64_t>::max();
    bool isthmusActive = false;
    OpStackFeeParams params{};
};

L1CostFunc makeCachedL1CostFunc(OpStackForkSchedule schedule, FeeParamsResolver resolveParams)
{
    auto cache = std::make_shared<L1CostCache>();
    return [schedule = std::move(schedule), resolveParams = std::move(resolveParams), cache](
               RollupCostData const& data, uint64_t blockTime) -> u256 {
        if (data.isEmpty())
        {
            return 0;
        }

        // Per-block cache: fork flag + L1Block params are constant within one block.
        if (cache->forBlock != blockTime)
        {
            cache->forBlock = blockTime;
            cache->fjordActive = isOpStackFjord(schedule, blockTime);
            cache->params = resolveParams(blockTime);
        }

        if (!cache->fjordActive)
        {
            throw std::invalid_argument("OpStack: pre-Fjord L1 cost unsupported");
        }

        // Delegates to l1CostFjord — see OpStackFeeParams.h formula block.
        return l1CostFjord(data, cache->params);
    };
}

OperatorCostFunc makeCachedOperatorCostFunc(
    OpStackForkSchedule schedule, FeeParamsResolver resolveParams)
{
    auto cache = std::make_shared<OperatorCostCache>();
    return [schedule = std::move(schedule), resolveParams = std::move(resolveParams), cache](
               uint64_t gas, uint64_t blockTime) -> u256 {
        if (cache->forBlock != blockTime)
        {
            cache->forBlock = blockTime;
            cache->isthmusActive = isOpStackIsthmus(schedule, blockTime);
            cache->params = resolveParams(blockTime);
        }

        if (!cache->isthmusActive)
        {
            return 0;
        }

        if (cache->params.operatorFeeScalar == 0 && cache->params.operatorFeeConstant == 0)
        {
            return 0;
        }

        if (isOpStackJovian(schedule, blockTime))
        {
            return operatorCostJovian(gas, cache->params);
        }
        // Isthmus formula: gas * scalar / divisor + constant
        return operatorCostIsthmus(gas, cache->params);
    };
}
}  // namespace

u256 l1CostFjord(RollupCostData const& data, OpStackFeeParams const& params)
{
    if (data.isEmpty())
    {
        return 0;
    }

    // l1Cost = estimatedDASizeScaled * l1FeeScaled / FJORD_DIVISOR
    // where l1FeeScaled = (l1BaseFeeScalar * l1BaseFee * calldataByteNum)
    //                   + (l1BlobBaseFeeScalar * l1BlobBaseFee)
    auto const scaledL1BaseFee = params.l1BaseFeeScalar * params.l1BaseFee;
    auto const calldataCostPerByte = scaledL1BaseFee * u256(FJORD_L1_FEE_CALLDATA_BYTE_NUMERATOR);
    auto const blobCostPerByte = params.l1BlobBaseFeeScalar * params.l1BlobBaseFee;
    auto const l1FeeScaled = calldataCostPerByte + blobCostPerByte;

    auto const scaled = estimatedDASizeScaled(data);
    return u256(scaled) * l1FeeScaled / u256(FJORD_DIVISOR);
}

u256 operatorCostIsthmus(uint64_t gas, OpStackFeeParams const& params)
{
    if (params.operatorFeeScalar == 0 && params.operatorFeeConstant == 0)
    {
        return 0;
    }

    // operatorCost = gas * scalar / OPERATOR_FEE_SCALAR_DIVISOR + constant
    auto fee = u256(gas) * params.operatorFeeScalar / u256(OPERATOR_FEE_SCALAR_DIVISOR);
    fee += params.operatorFeeConstant;
    return fee;
}

u256 operatorCostJovian(uint64_t gas, OpStackFeeParams const& params)
{
    if (params.operatorFeeScalar == 0 && params.operatorFeeConstant == 0)
    {
        return 0;
    }

    // operatorCost = gas * scalar * JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER + constant
    auto fee = u256(gas) * params.operatorFeeScalar * u256(JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER);
    fee += params.operatorFeeConstant;
    return fee;
}

OpStackFeeParams loadOpStackFeeParams(state::StateView const& state)
{
    OpStackFeeParams params{};

    auto const readSlot = [&](u256 slot) -> evmc_bytes32 {
        return state.get_storage(OP_L1_BLOCK_PREDEPLOY, slotKey(slot));
    };

    params.l1BaseFee = state::fromEvmC(readSlot(L1_BASE_FEE_SLOT));
    params.l1BlobBaseFee = state::fromEvmC(readSlot(L1_BLOB_BASE_FEE_SLOT));

    auto const feeScalars = readSlot(L1_FEE_SCALARS_SLOT);
    params.l1BaseFeeScalar = unpackBaseFeeScalar(feeScalars);
    params.l1BlobBaseFeeScalar = unpackBlobBaseFeeScalar(feeScalars);

    auto const operatorFeeParams = readSlot(OPERATOR_FEE_PARAMS_SLOT);
    if (state::isZeroBytes32(operatorFeeParams))
    {
        return params;
    }

    params.operatorFeeScalar = unpackOperatorFeeScalar(operatorFeeParams);
    params.operatorFeeConstant = unpackOperatorFeeConstant(operatorFeeParams);
    params.daFootprintGasScalar = unpackDaFootprintGasScalar(operatorFeeParams);
    return params;
}

L1CostFunc selectL1CostFunc(OpStackForkSchedule const& schedule, OpStackFeeParams const& params)
{
    return makeCachedL1CostFunc(schedule, [params](uint64_t) { return params; });
}

OperatorCostFunc selectOperatorCostFunc(
    OpStackForkSchedule const& schedule, OpStackFeeParams const& params)
{
    return makeCachedOperatorCostFunc(schedule, [params](uint64_t) { return params; });
}

L1CostFunc wireL1CostFuncWithState(
    OpStackForkSchedule const& schedule, state::StateView const& state)
{
    return makeCachedL1CostFunc(
        schedule, [&state](uint64_t) { return loadOpStackFeeParams(state); });
}

OperatorCostFunc wireOperatorCostFuncWithState(
    OpStackForkSchedule const& schedule, state::StateView const& state)
{
    return makeCachedOperatorCostFunc(
        schedule, [&state](uint64_t) { return loadOpStackFeeParams(state); });
}

}  // namespace bcos::evm

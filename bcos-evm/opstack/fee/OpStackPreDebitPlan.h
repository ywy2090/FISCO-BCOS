#pragma once

#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-utilities/Common.h>
#include <functional>
#include <optional>

namespace bcos::evm
{

struct OpStackFeeHooks
{
    std::function<bcos::u256(RollupCostData const&, uint64_t)> const* l1CostFunc{nullptr};
    std::function<bcos::u256(uint64_t gasLimit, uint64_t blockTime)> const* operatorCostFunc{
        nullptr};
};

struct OpStackFeeSidecarWrite
{
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
};

struct OpStackPreDebitInputs
{
    gas::FeeInputs fee;
    bcos::u256 txValue{0};
    uint64_t blockTime{0};
    bool hasGasFeeCap{true};
    bcos::u256 blobGasFeeCap{0};
    bcos::u256 blobBaseFee{0};
    size_t blobCount{0};
    std::optional<RollupCostData> const* rollupCostData{nullptr};
};

struct OpStackPreDebitPlan
{
    gas::FeeSettlementPlan core1559;
    OpStackFeeSidecarWrite sidecar;
    bcos::u256 blobDebit{0};
    bcos::u256 blobBalanceCheck{0};
    bcos::u256 totalDebit{0};
    bcos::u256 balanceCheck{0};
};

OpStackPreDebitPlan planOpStackPreDebit(
    OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks) noexcept;

}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSidecar.h"
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{

struct OpStackFeeParams;
struct OpStackMessageResult;

struct GasPoolHooks
{
    std::function<bool(uint64_t)> subGas;
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> returnGas;
};

struct OpStackTxFinalizeResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

bool isNormalPreExecutionReject(StateTransitionExitKind exitKind) noexcept;

void abortNormalAfterBuyGas(StateTransitionContext& ctx, GasPoolHooks const& gasPool,
    OpStackMessageResult& output, int64_t originalGasLimit);

OpStackTxFinalizeResult finalizeNormal(StateTransitionContext const& ctx,
    OpStackFeeSidecar const& sidecar, StateTransitionExitKind exitKind);

OpStackTxFinalizeResult finalizeDeposit(
    StateTransitionContext& ctx, StateTransitionExitKind exitKind, evmc_status_code evmStatus);

task::Task<OpStackTxFinalizeResult> settleDeposit(StateTransitionContext& ctx,
    StateTransitionExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);

}  // namespace bcos::evm

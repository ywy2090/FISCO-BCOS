#pragma once

#include "bcos-evm/eth/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/OpStackFeeSidecar.h"
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{

struct OpStackFeeParams;
struct OpStackExecutionResult;

struct GasPoolHooks
{
    std::function<bool(uint64_t)> subGas;
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> returnGas;
};

struct OpStackSettlementResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

bool isNormalPreExecutionReject(StateTransitionExitKind exitKind) noexcept;

void abortNormalAfterBuyGas(StateTransitionContext& ctx, GasPoolHooks const& gasPool,
    OpStackExecutionResult& output, int64_t originalGasLimit);

OpStackSettlementResult finalizeNormal(StateTransitionContext const& ctx,
    OpStackFeeSidecar const& sidecar, StateTransitionExitKind exitKind);

OpStackSettlementResult finalizeDeposit(
    StateTransitionContext& ctx, StateTransitionExitKind exitKind, evmc_status_code evmStatus);

task::Task<OpStackSettlementResult> settleDeposit(StateTransitionContext& ctx,
    StateTransitionExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);

}  // namespace bcos::evm

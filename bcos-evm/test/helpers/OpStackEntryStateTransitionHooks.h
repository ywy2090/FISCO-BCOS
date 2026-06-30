#pragma once

#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/OpStackStateTransitionHooks.h"
#include <optional>

namespace bcos::evm::test
{

inline std::optional<EVMCResult> runOpStackEntryLifecycleCheck(
    OpStackExecutionRequest const& input, state::StateView const& stateView)
{
    StateTransitionContext ctx{stateView, input.message, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};
    OpStackStateTransitionHooks policy(view);
    policy.lifecycleCheckEntryRules(ctx);
    if (ctx.earlyExit)
    {
        return std::optional<EVMCResult>{std::move(ctx.evmcResult)};
    }
    return std::nullopt;
}

}  // namespace bcos::evm::test

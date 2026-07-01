#pragma once

#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/apply/OpStackStateTransitionHooks.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/types/OpStackDepositTx.h"
#include <optional>

namespace bcos::evm::test
{

inline std::optional<EVMCResult> runOpStackEntryLifecycleCheck(
    OpStackMessageRequest const& input, state::StateView const& stateView)
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

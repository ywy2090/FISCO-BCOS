#pragma once

#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/apply/EthStateTransitionHooks.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/StateView.hpp"
#include <optional>

namespace bcos::evm::test
{

inline std::optional<EVMCResult> ethPreCheckRulesError(
    EthMessageRequest const& input, state::StateView const& stateView)
{
    StateTransitionContext ctx(stateView, input.message, input.revisionConfig, bcos::u256(0));
    EthStateTransitionHooks hooks(input);
    hooks.onPreCheckRules(ctx);
    if (ctx.earlyExit)
    {
        return std::optional<EVMCResult>(std::move(ctx.evmcResult));
    }
    return std::nullopt;
}

}  // namespace bcos::evm::test

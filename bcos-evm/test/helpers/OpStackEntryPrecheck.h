#pragma once

#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackPrecheckPolicy.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include <optional>

namespace bcos::evm::test
{

inline std::optional<EVMCResult> runOpStackEntryPrecheck(
    OpStackExecutionRequest const& input, state::StateView const& stateView)
{
    TxPipelineContext ctx{stateView, input.message, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};
    OpStackPrecheckPolicy policy(view);
    policy.checkEntryRules(ctx);
    if (ctx.earlyExit)
    {
        return std::optional<EVMCResult>{std::move(ctx.evmcResult)};
    }
    return std::nullopt;
}

}  // namespace bcos::evm::test

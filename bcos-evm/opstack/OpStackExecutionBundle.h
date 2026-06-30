#pragma once

#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include <stdexcept>

namespace bcos::evm
{

/// Owns OpStack chain call-target adapter for one tx lifecycle.
struct OpStackExecutionBundle
{
    OpStackExecutionBundle(StateTransitionContext& ctx, OpStackExecutionRequest const& input)
      : m_chainAdapter(
            &ctx.state, input.blockInfo.baseFee, input.forkSchedule, input.blockInfo.timestamp)
    {
        ctx.wireExecutionEnvironment(input.vm, nullptr, &m_chainAdapter);
    }

    OpStackChainCallTargetAdapter const& chainAdapter() const noexcept { return m_chainAdapter; }

private:
    OpStackChainCallTargetAdapter m_chainAdapter;
};

}  // namespace bcos::evm

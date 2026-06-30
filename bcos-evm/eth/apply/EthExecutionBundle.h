#pragma once

#include "bcos-evm/eth/apply/ApplyReferenceMessage.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/eth/policy/EthVmHostPolicy.h"

namespace bcos::evm
{

/// Owns Eth reference-path EvmHostHooks; wires execution environment into ctx.
struct EthExecutionBundle
{
    EthExecutionBundle(StateTransitionContext& ctx, EthReferenceRequest const& input)
      : m_extension()
    {
        ctx.wireExecutionEnvironment(input.vm, &m_extension, nullptr);
    }

private:
    state::EthVmHostPolicy m_extension;
};

}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth/pipeline/ExecutionSession.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include <stdexcept>

namespace bcos::evm
{

/// Owns OpStack chain call-target adapter for one tx lifecycle (ADR-027).
struct OpStackExecutionBundle
{
    OpStackExecutionBundle(TxPipelineContext& ctx, OpStackExecutionRequest const& input)
      : m_chainAdapter(
            &ctx.state, input.blockInfo.baseFee, input.forkSchedule, input.blockInfo.timestamp)
    {
        m_view.vm = input.vm;
        m_view.blockHashes = input.blockHashes;
        m_view.extension = nullptr;
        m_view.chainPort = &m_chainAdapter;
        if (m_view.chainPort == nullptr)
        {
            throw std::invalid_argument("OpStackExecutionBundle requires chainPort");
        }
        m_view.wire(ctx);
    }

    ExecutionSession const& view() const noexcept { return m_view; }
    OpStackChainCallTargetAdapter const& chainAdapter() const noexcept { return m_chainAdapter; }

private:
    OpStackChainCallTargetAdapter m_chainAdapter;
    ExecutionSession m_view;
};

}  // namespace bcos::evm

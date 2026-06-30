#pragma once

#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include <stdexcept>

namespace bcos::evm
{

/// Owns OpStack chain call-target adapter for one tx lifecycle.
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

    EvmTxContextView const& view() const noexcept { return m_view; }
    OpStackChainCallTargetAdapter const& chainAdapter() const noexcept { return m_chainAdapter; }

private:
    OpStackChainCallTargetAdapter m_chainAdapter;
    EvmTxContextView m_view;
};

}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth/apply/EthReferenceExecute.h"
#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/eth/policy/EthVmHostPolicy.h"

namespace bcos::evm
{

/// Owns Eth reference-path EvmHostHooks; exposes kernel EvmTxContextView view.
struct EthExecutionBundle
{
    EthExecutionBundle(TxPipelineContext& ctx, EthReferenceRequest const& input) : m_extension()
    {
        m_view.vm = input.vm;
        m_view.blockHashes = input.blockHashes;
        m_view.extension = &m_extension;
        m_view.chainPort = nullptr;
        m_view.wire(ctx);
    }

    EvmTxContextView const& view() const noexcept { return m_view; }

private:
    state::EthVmHostPolicy m_extension;
    EvmTxContextView m_view;
};

}  // namespace bcos::evm

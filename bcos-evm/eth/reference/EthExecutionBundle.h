#pragma once

#include "bcos-evm/eth/pipeline/ExecutionSession.h"
#include "bcos-evm/eth/policy/EthVmHostPolicy.h"
#include "bcos-evm/eth/reference/EthReferenceBridge.h"

namespace bcos::evm
{

/// Owns Eth reference-path VmHostPolicy; exposes kernel ExecutionSession view (ADR-027).
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

    ExecutionSession const& view() const noexcept { return m_view; }

private:
    state::EthVmHostPolicy m_extension;
    ExecutionSession m_view;
};

}  // namespace bcos::evm

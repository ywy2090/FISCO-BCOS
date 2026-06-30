#pragma once

#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoExecute.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include <cassert>
#include <optional>

namespace bcos::evm
{

/// Owns FISCO EvmHostHooks + optional chain call-target adapter.
struct FiscoExecutionBundle
{
    FiscoExecutionBundle(TxPipelineContext& ctx, FiscoExecutionRequest& input)
      : m_extension(input.revisionConfig.enable_balance_transfer, makeDeps(ctx, input))
    {
        m_view.vm = input.vm;
        m_view.blockHashes = input.blockHashes;
        m_view.extension = &m_extension;
        m_view.fixStorageStatus = input.revisionConfig.fix_storage_status;
        m_view.fixNonceInit = input.revisionConfig.fix_nonce_init;

        if (input.chainDispatchPort != nullptr)
        {
            m_chainAdapter.emplace(ctx.state, *input.chainDispatchPort);
            m_view.chainPort = std::addressof(*m_chainAdapter);
#ifndef NDEBUG
            assert(m_view.chainPort != nullptr);
#endif
        }

        m_view.wire(ctx);
    }

    EvmTxContextView const& view() const noexcept { return m_view; }
    FiscoVmHostPolicy& extension() noexcept { return m_extension; }

private:
    static FiscoVmHostPolicy::FiscoVmHostPolicyDeps makeDeps(
        TxPipelineContext& ctx, FiscoExecutionRequest& input)
    {
        FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
        deps.state = &ctx.state;
        deps.blockNumber = input.blockInfo.number;
        deps.revisionFlags.fix_auth_check = input.revisionConfig.fix_auth_check;
        deps.revisionFlags.use_raw_address = input.revisionConfig.use_raw_address;
        deps.revisionFlags.fix_nonce_init = input.revisionConfig.fix_nonce_init;
        deps.revisionFlags.web3Tx = input.web3Tx;
        deps.hashImpl = input.hashImpl;
        deps.seq = input.nestedSeq;
        deps.origin = input.origin;
        deps.persistContractCreateNonce = std::move(input.persistContractCreateNonce);
        deps.recipientPathResolver = std::move(input.recipientPathResolver);
        deps.authPort = input.authPort;
        return deps;
    }

    FiscoVmHostPolicy m_extension;
    std::optional<FiscoChainCallTargetAdapter> m_chainAdapter;
    EvmTxContextView m_view;
};

}  // namespace bcos::evm

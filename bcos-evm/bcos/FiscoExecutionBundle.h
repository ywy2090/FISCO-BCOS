#pragma once

#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include <cassert>
#include <optional>

namespace bcos::evm
{

/// Owns FISCO EvmHostHooks + optional chain call-target adapter.
struct FiscoExecutionBundle
{
    FiscoExecutionBundle(StateTransitionContext& ctx, FiscoExecutionRequest& input)
      : m_extension(input.revisionConfig.enable_balance_transfer, makeDeps(ctx, input))
    {
        ChainExtendedPrecompileDispatch* chainPort = nullptr;
        if (input.chainDispatchPort != nullptr)
        {
            m_chainAdapter.emplace(ctx.state, *input.chainDispatchPort);
            chainPort = std::addressof(*m_chainAdapter);
#ifndef NDEBUG
            assert(chainPort != nullptr);
#endif
        }
        ctx.wireExecutionEnvironment(input.vm, &m_extension, chainPort);
    }

    FiscoVmHostPolicy& extension() noexcept { return m_extension; }

private:
    static FiscoVmHostPolicy::FiscoVmHostPolicyDeps makeDeps(
        StateTransitionContext& ctx, FiscoExecutionRequest& input)
    {
        FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
        deps.state = &ctx.state;
        deps.blockNumber = input.blockInfo.number;
        deps.revisionFlags.fix_auth_check = input.revisionConfig.fix_auth_check;
        deps.revisionFlags.use_raw_address = input.revisionConfig.use_raw_address;
        deps.revisionFlags.fix_storage_status = input.revisionConfig.fix_storage_status;
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
};

}  // namespace bcos::evm

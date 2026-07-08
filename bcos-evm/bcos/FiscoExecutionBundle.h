#pragma once

#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoEvmHostHooks.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include <cassert>
#include <optional>

namespace bcos::evm
{

/// Owns FISCO EvmHostHooks + optional chain call-target adapter.
struct FiscoExecutionBundle
{
    FiscoExecutionBundle(StateTransitionContext& ctx, FiscoMessageRequest& input)
      : m_extension(input.revisionConfig.enable_balance_transfer, makeDeps(ctx, input))
    {
        ChainCallTargetPort* callTargetPort = nullptr;
        if (input.callTargetPort != nullptr)
        {
            m_chainAdapter.emplace(ctx.state, *input.callTargetPort);
            callTargetPort = std::addressof(*m_chainAdapter);
#ifndef NDEBUG
            assert(callTargetPort != nullptr);
#endif
        }
        ctx.wireExecutionEnvironment(input.vm, &m_extension, callTargetPort);
    }

    FiscoEvmHostHooks& extension() noexcept { return m_extension; }

private:
    static FiscoEvmHostHooks::FiscoEvmHostHooksDeps makeDeps(
        StateTransitionContext& ctx, FiscoMessageRequest& input)
    {
        FiscoEvmHostHooks::FiscoEvmHostHooksDeps deps;
        deps.state = &ctx.state;
        deps.blockNumber = input.blockInfo.number;
        // Nested-CREATE address derivation must see the same contextID and
        // feature_evm_address the top-level derivation uses (FiscoStateTransitionHooks);
        // without these, nested frames derived addresses with contextID=0 and the feature
        // forced off, colliding across txs and diverging from top-level derivation.
        deps.contextID = input.contextID;
        deps.featureEvmAddress = input.revisionConfig.feature_evm_address;
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

    FiscoEvmHostHooks m_extension;
    std::optional<FiscoChainCallTargetAdapter> m_chainAdapter;
};

}  // namespace bcos::evm

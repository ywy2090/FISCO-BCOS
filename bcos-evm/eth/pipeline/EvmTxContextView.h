#pragma once

#include "bcos-evm/eth/InnerExecute.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/eth/ports/ChainCallTargetDispatcher.h"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include <cassert>
#include <stdexcept>

namespace bcos::evm
{

/// Kernel view of per-tx execution environment. Chain Bundles own adapters.
struct EvmTxContextView
{
    ChainCallTargetDispatcher* chainPort{nullptr};
    state::EvmHostHooks* extension{nullptr};
    evmc::VM* vm{nullptr};
    state::BlockHashes blockHashes{};

    void wire(StateTransitionContext& ctx) const
    {
        if (vm == nullptr)
        {
            throw std::invalid_argument("EvmTxContextView::wire requires vm");
        }

        ctx.extension = extension;
        ctx.chainPort = chainPort;
        ctx.inputs.vm = vm;
        ctx.txContextView = this;

#ifndef NDEBUG
        assert(ctx.extension == extension);
        assert(ctx.chainPort == chainPort);
        assert(ctx.inputs.vm == vm);
#endif
    }

    InnerExecuteInput toInnerExecuteInput(StateTransitionContext const& ctx) const
    {
        InnerExecuteInput input;
        input.state = const_cast<state::State*>(&ctx.state);
        input.vm = vm;
        input.message = ctx.message;
        input.gasPrice = ctx.gasPrice;
        input.blockInfo = ctx.inputs.blockInfo;
        input.blockHashes = blockHashes;
        input.revisionConfig = ctx.revisionConfig;
        input.txProps = ctx.txProps;
        input.accessList = ctx.inputs.accessList;
        input.authorizationListPresent = ctx.inputs.authorizationListPresent;
        input.authorizations = ctx.inputs.authorizations;
        input.web3TypedTxKind = ctx.inputs.web3TypedTxKind;
        input.extension = extension;
        input.chainPort = chainPort;
        return input;
    }
};

}  // namespace bcos::evm

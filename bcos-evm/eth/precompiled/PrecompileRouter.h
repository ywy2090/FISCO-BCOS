/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @brief Precompile dispatch router.
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.hpp>

namespace bcos::evm
{
struct ChainCallTargetDispatcher;
}

namespace bcos::evm::precompiled
{

enum class PrecompileDispatchOutcome
{
    NotApplicable,
    Dispatched,
    EmptyAccountSuccess,
    PolicyRejected,
};

struct PrecompileEnvelopeInput
{
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    execution::CallTargetDescriptor const& target;
    evmc_message const& message;
    bool skipValueTransfer;
    ChainCallTargetDispatcher* chainPort;
};

struct PrecompileRouterInput
{
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    state::EvmHostHooks* extension;
    evmc_message const& message;
    evmc_address target;
    bool skipValueTransfer;
    execution::FrameScope scope{execution::FrameScope::TopLevel};
    ChainCallTargetDispatcher* chainPort{nullptr};
};

struct PrecompileRouterOutput
{
    PrecompileDispatchOutcome outcome{PrecompileDispatchOutcome::NotApplicable};
    evmc::Result result{};
    int64_t gasRefund{0};
};

PrecompileRouterOutput executePrecompileEnvelope(PrecompileEnvelopeInput const& input);
PrecompileRouterOutput executeEmptyAccountEnvelope(PrecompileEnvelopeInput const& input);

[[deprecated("Use resolveCallTarget + executePrecompileEnvelope")]] PrecompileRouterOutput
dispatchPrecompile(PrecompileRouterInput const& input);

}  // namespace bcos::evm::precompiled

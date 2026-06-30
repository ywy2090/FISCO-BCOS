/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @brief Precompile call envelope: value transfer + state journal + dispatch.
 *
 * Production path (EvmCallFrame → resolveCallTarget):
 *   executePrecompileEnvelope
 *     → checkpoint / optional value transfer
 *     → BuiltinPrecompile  → EthPrecompiles::tryDispatchInCall
 *     → ChainPrecompile    → ChainExtendedPrecompileDispatch (FISCO / OpStack)
 *     → commit or revert state
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.hpp>

namespace bcos::evm
{
struct ChainExtendedPrecompileDispatch;
}

namespace bcos::evm::precompiled
{

/// How the envelope resolved; orthogonal to evmc status inside @p result.
enum class PrecompileDispatchOutcome
{
    NotApplicable,
    Dispatched,
    EmptyAccountSuccess,
    PolicyRejected,
};

/// Inputs assembled by EvmCallFrame after CallTargetResolver classification.
struct PrecompileEnvelopeInput
{
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    execution::CallTargetDescriptor const& target;
    evmc_message const& message;
    bool skipValueTransfer;
    /// nullptr on pure Eth reference execution (builtin precompiles only).
    ChainExtendedPrecompileDispatch* chainPort;
};

struct PrecompileRouterOutput
{
    PrecompileDispatchOutcome outcome{PrecompileDispatchOutcome::NotApplicable};
    evmc::Result result{};
    int64_t gasRefund{0};
};

/// Full precompile call: value transfer, dispatch, state finalize.
PrecompileRouterOutput executePrecompileEnvelope(PrecompileEnvelopeInput const& input);
/// CALL to empty account: succeed after value transfer, no bytecode execution.
PrecompileRouterOutput executeEmptyAccountEnvelope(PrecompileEnvelopeInput const& input);

}  // namespace bcos::evm::precompiled

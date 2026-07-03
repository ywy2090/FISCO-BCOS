/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @brief Precompile call envelope: value transfer + state journal + dispatch.
 *
 * Production path (EvmCallFrame → classifyCallTarget):
 *   executePrecompileEnvelope
 *     → checkpoint / optional value transfer
 *     → BuiltinPrecompile  → EthPrecompiles::tryDispatchInCall
 *     → ChainPrecompile    → ChainCallTargetPort (FISCO / OpStack)
 *     → commit or revert state
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/CallTargetTypes.h"
#include <evmc/evmc.hpp>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::precompiled
{

/// Which PrecompileRouter envelope ran; orthogonal to @p result (success/failure/gas).
enum class PrecompileEnvelopeRoute
{
    None,          ///< Envelope not executed (default; policy reject / EvmContract fall-through).
    Precompile,    ///< executePrecompileEnvelope (builtin or chain precompile).
    EmptyAccount,  ///< executeEmptyAccountEnvelope (empty code, non-precompile address).
};

/// Inputs assembled by EvmCallFrame after CallTargetResolver classification.
struct PrecompileEnvelopeInput
{
    state::State& state;
    bcos::evm::RevisionConfig const& revision;
    execution::ClassifiedCallTarget const& target;
    evmc_message const& message;
    bool skipValueTransfer;
    /// nullptr on pure Eth reference execution (builtin precompiles only).
    ChainCallTargetPort* callTargetPort;
};

struct PrecompileRouterOutput
{
    PrecompileEnvelopeRoute route{PrecompileEnvelopeRoute::None};
    evmc::Result result{};
    int64_t gasRefund{0};
};

/// Full precompile call: value transfer, dispatch, state finalize.
PrecompileRouterOutput executePrecompileEnvelope(PrecompileEnvelopeInput const& input);
/// CALL to empty account: succeed after value transfer, no bytecode execution.
PrecompileRouterOutput executeEmptyAccountEnvelope(PrecompileEnvelopeInput const& input);

}  // namespace bcos::evm::precompiled

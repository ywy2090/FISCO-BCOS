#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/CallTargetTypes.h"
#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/kernel/FrameScope.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

/// Classify routed call target: precompile / empty / EVM / policy reject (geth: run() dispatch).
CallTargetDescriptor classifyCallTarget(state::State& state,
    bcos::evm::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainCallTargetPort* callTargetPort, state::EvmHostHooks* extension);

void enumerateTxEntryWarmTargets(bcos::evm::RevisionConfig const& cfg,
    ChainCallTargetPort const* callTargetPort,
    std::function<void(evmc_address const&)> const& consume);

}  // namespace bcos::evm::execution

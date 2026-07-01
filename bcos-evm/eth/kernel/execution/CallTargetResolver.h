#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/CallTargetKind.h"
#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/core/FrameScope.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{
struct ChainExtendedPrecompileDispatch;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

CallTargetDescriptor resolveCallTarget(state::State& state,
    bcos::evm_standard::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainExtendedPrecompileDispatch* chainPort, state::EvmHostHooks* extension);

void enumerateTxEntryWarmTargets(bcos::evm_standard::RevisionConfig const& cfg,
    ChainExtendedPrecompileDispatch const* chainPort,
    std::function<void(evmc_address const&)> const& consume);

}  // namespace bcos::evm::execution

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file CallTargetResolver.cpp
 */

#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/core/ChainCallTargetPort.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
namespace
{
/// Account code is an EIP-7702 delegation designator (not runtime bytecode).
bool is7702DelegationDesignator(bcos::evm::RevisionConfig const& revision, bcos::bytes const& code)
{
    return revision.eip7702 &&
           parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}).has_value();
}

/// Empty-code account at an active builtin precompile address (DELEGATECALL policy probe).
bool isActiveEmptyPrecompileTarget(state::State const& state,
    bcos::evm::RevisionConfig const& revision, evmc_address const& target,
    evmc_message const& message)
{
    if (state::isZeroAddress(target))
    {
        return false;
    }
    auto const code = state.get_code(target);
    if (!code.empty())
    {
        return false;
    }
    return precompiled::isActivePrecompile(revision, target);
}
}  // namespace

ClassifiedCallTarget classifyCallTarget(state::State& state,
    bcos::evm::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainCallTargetPort* callTargetPort, state::EvmHostHooks* extension)
{
    // CREATE frames always run initcode via EVM; classification is for CALL-family only.
    if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::EvmContract,
            .accessWarm = AccessWarmSchedule::AtFirstAccess,
            .routed = msg};
    }

    auto const resolved = routeFrameMessage(state, revision, msg, scope);
    auto const& executionAddress = resolved.executionAddress;
    auto const& routed = resolved.routed;

    auto const code = state.get_code(executionAddress);
    bool const emptyCode = code.empty();

    // 7702 authority stores a designator; execution follows delegate code in loadFrameBytecode.
    if (!emptyCode && is7702DelegationDesignator(revision, code))
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::EvmContract,
            .dispatchAddress = executionAddress,
            .accessWarm = AccessWarmSchedule::AtFirstAccess,
            .routed = routed};
    }

    // Chain policy: reject DELEGATECALL into precompile addresses when extension disallows it.
    if (msg.kind == EVMC_DELEGATECALL && extension != nullptr &&
        !extension->allowDelegateCallToPrecompile() &&
        isActiveEmptyPrecompileTarget(state, revision, executionAddress, routed))
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::BuiltinPrecompile,
            .admission = CallTargetAdmission::DenyDelegateCallPrecompile,
            .dispatchAddress = executionAddress,
            .accessWarm = AccessWarmSchedule::AtFirstAccess,
            .routed = routed};
    }

    // Chain hook: empty target or nested frame (FISCO proxy, OpStack predeploy classification).
    bool const tryChainHook = emptyCode || scope == FrameScope::Nested;
    if (tryChainHook && callTargetPort != nullptr)
    {
        if (auto chainDesc = callTargetPort->classifyTarget(state, executionAddress, routed, scope))
        {
            chainDesc->routed = routed;
            return *chainDesc;
        }
    }

    // Standard eth precompile table (single source: PrecompileActive).
    if (emptyCode && precompiled::isActivePrecompile(revision, executionAddress))
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::BuiltinPrecompile,
            .dispatchAddress = executionAddress,
            .accessWarm = AccessWarmSchedule::AtTxPrepare,
            .routed = routed};
    }

    // Call into non-existent contract: empty success envelope, no VM.
    if (emptyCode)
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::EmptyAccount,
            .dispatchAddress = executionAddress,
            .accessWarm = AccessWarmSchedule::AtFirstAccess,
            .routed = routed};
    }

    return ClassifiedCallTarget{.route = CallTargetRoute::EvmContract,
        .dispatchAddress = executionAddress,
        .accessWarm = AccessWarmSchedule::AtFirstAccess,
        .routed = routed};
}

void enumerateTxEntryWarmTargets(bcos::evm::RevisionConfig const& cfg,
    ChainCallTargetPort const* callTargetPort,
    std::function<void(evmc_address const&)> const& consume)
{
    // Builtin precompiles: classifyCallTarget assigns AccessWarmSchedule::AtTxPrepare
    // (PrecompileActive single source).
    precompiled::forEachActivePrecompile(cfg, [&](evmc_address const& a) { consume(a); });
    // Chain static targets: adapter forEachStaticWarmTarget emits only isEnumeratedAtTxPrepare
    // entries.
    if (callTargetPort != nullptr)
    {
        callTargetPort->forEachStaticWarmTarget(consume);
    }
}

}  // namespace bcos::evm::execution

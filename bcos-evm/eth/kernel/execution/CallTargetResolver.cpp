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
#include "bcos-utilities/Common.h"
#include "eth/core/CallTargetTypes.h"
#include "eth/core/EvmHostHooks.h"
#include "eth/core/RevisionConfig.h"
#include "eth/state/StateKeyHash.hpp"
#include <optional>

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

/// STATICCALL is EVMC_CALL with EVMC_STATIC (no separate evmc_call_kind).
inline bool isStaticCallFamily(evmc_message const& msg) noexcept
{
    return msg.kind == EVMC_CALL && (msg.flags & EVMC_STATIC) != 0;
}

/// DELEGATECALL/CALLCODE/STATICCALL through 7702 delegation to empty delegate code.
inline bool is7702NonCallEmptyDelegateOpcode(evmc_message const& msg) noexcept
{
    return msg.kind == EVMC_DELEGATECALL || msg.kind == EVMC_CALLCODE || isStaticCallFamily(msg);
}
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
        // geth: DELEGATECALL/CALLCODE resolve delegation but never RunPrecompiledContract on the
        // delegate address — empty delegate bytecode (incl. precompile addrs) is an empty-account
        // success envelope, including gas=0 child calls (EEST set_code_to_precompile).
        if (auto const delegate =
                parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
        {
            auto const delegateCode = state.get_code(*delegate);
            if (delegateCode.empty() && is7702NonCallEmptyDelegateOpcode(msg))
            {
                return ClassifiedCallTarget{.route = CallTargetRoute::EmptyAccount,
                    .dispatchAddress = executionAddress,
                    .accessWarm = AccessWarmSchedule::AtFirstAccess,
                    .routed = routed};
            }
        }
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

    // evmone Prague+ resolves EIP-7702 before host call: DELEGATECALL/CALLCODE/STATICCALL to a
    // delegated precompile arrives with code_address at the delegate and EVMC_DELEGATED set — run
    // empty code, not RunPrecompiledContract (EEST set_code_to_precompile variants).
    if (emptyCode && precompiled::isActivePrecompile(revision, executionAddress) &&
        is7702NonCallEmptyDelegateOpcode(msg) && (msg.flags & EVMC_DELEGATED) != 0)
    {
        return ClassifiedCallTarget{.route = CallTargetRoute::EmptyAccount,
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

#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/core/ChainCallTargetPort.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/CallKind.h"
#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
namespace
{
bool is7702DelegationDesignator(bcos::evm::RevisionConfig const& revision, bcos::bytes const& code)
{
    return revision.eip7702 &&
           parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}).has_value();
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

CallTargetDescriptor classifyCallTarget(state::State& state,
    bcos::evm::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainCallTargetPort* callTargetPort, state::EvmHostHooks* extension)
{
    if (isCreateKind(msg.kind))
    {
        return CallTargetDescriptor{
            .kind = CallTargetKind::EvmContract, .warmPolicy = WarmPolicy::Never, .routed = msg};
    }

    auto const resolved = routeFrameMessage(state, revision, msg, scope);
    auto const& executionAddress = resolved.executionAddress;
    auto const& routed = resolved.routed;

    auto const code = state.get_code(executionAddress);
    bool const emptyCode = code.empty();

    if (!emptyCode && is7702DelegationDesignator(revision, code))
    {
        return CallTargetDescriptor{.kind = CallTargetKind::EvmContract,
            .dispatchAddress = executionAddress,
            .warmPolicy = WarmPolicy::Never,
            .routed = routed};
    }

    if (msg.kind == EVMC_DELEGATECALL && extension != nullptr &&
        !extension->allowDelegateCallToPrecompile() &&
        isActiveEmptyPrecompileTarget(state, revision, executionAddress, routed))
    {
        return CallTargetDescriptor{.kind = CallTargetKind::PolicyRejected,
            .dispatchAddress = executionAddress,
            .warmPolicy = WarmPolicy::Never,
            .routed = routed};
    }

    bool const tryChainHook = emptyCode || scope == FrameScope::Nested;
    if (tryChainHook && callTargetPort != nullptr)
    {
        if (auto chainDesc = callTargetPort->classifyTarget(state, executionAddress, routed, scope))
        {
            chainDesc->routed = routed;
            return *chainDesc;
        }
    }

    if (emptyCode && precompiled::isActivePrecompile(revision, executionAddress))
    {
        return CallTargetDescriptor{.kind = CallTargetKind::BuiltinPrecompile,
            .dispatchAddress = executionAddress,
            .warmPolicy = WarmPolicy::TxEntryAlways,
            .routed = routed};
    }

    if (emptyCode)
    {
        return CallTargetDescriptor{.kind = CallTargetKind::EmptyAccount,
            .dispatchAddress = executionAddress,
            .warmPolicy = WarmPolicy::Never,
            .routed = routed};
    }

    return CallTargetDescriptor{.kind = CallTargetKind::EvmContract,
        .dispatchAddress = executionAddress,
        .warmPolicy = WarmPolicy::Never,
        .routed = routed};
}

void enumerateTxEntryWarmTargets(bcos::evm::RevisionConfig const& cfg,
    ChainCallTargetPort const* callTargetPort,
    std::function<void(evmc_address const&)> const& consume)
{
    // Builtin precompiles: classifyCallTarget assigns WarmPolicy::TxEntryAlways (PrecompileActive
    // single source).
    precompiled::forEachActivePrecompile(cfg, [&](evmc_address const& a) { consume(a); });
    // Chain static targets: adapter forEachStaticWarmTarget emits only isTxEntryWarm entries (PR5).
    if (callTargetPort != nullptr)
    {
        callTargetPort->forEachStaticWarmTarget(consume);
    }
}

}  // namespace bcos::evm::execution

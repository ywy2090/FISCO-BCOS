/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile dispatch router (Phase 1 skeleton).
 */

#include "PrecompileRouter.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"

namespace bcos::evm::precompiled
{
namespace
{
inline bool isDelegated7702Message(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0;
}

evmc::Result makeInsufficientBalanceResult() noexcept
{
    evmc_result result{};
    result.status_code = EVMC_INSUFFICIENT_BALANCE;
    result.gas_left = 0;
    return evmc::Result(result);
}

void finalizeEnvelope(state::State& state, PrecompileRouterOutput& output)
{
    if (output.result.status_code == EVMC_SUCCESS)
    {
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        state.commit();
    }
    else
    {
        state.revert();
    }
}

bool is7702DelegationDesignator(
    bcos::evm_standard::RevisionConfig const& revision, bcos::bytes const& code)
{
    return revision.eip7702 &&
           parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}).has_value();
}

evmc::Result makePrecompileFailureResult(int64_t gasLeft) noexcept
{
    evmc_result result{};
    result.status_code = EVMC_PRECOMPILE_FAILURE;
    result.gas_left = gasLeft;
    return evmc::Result(result);
}

bool isActiveEmptyPrecompileTarget(state::State const& state,
    bcos::evm_standard::RevisionConfig const& revision, evmc_address const& target,
    evmc_message const& message)
{
    if (state::isZeroAddress(target))
    {
        return false;
    }
    if (isDelegated7702Message(message) && message.kind != EVMC_CALL)
    {
        return false;
    }
    auto const code = state.get_code(target);
    if (!code.empty())
    {
        return false;
    }
    return isActivePrecompile(revision, target);
}
}  // namespace

PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input)
{
    PrecompileRouterOutput output;

    if (input.message.kind == EVMC_DELEGATECALL && input.extension != nullptr &&
        !input.extension->allowDelegateCallToPrecompile() &&
        isActiveEmptyPrecompileTarget(input.state, input.revision, input.target, input.message))
    {
        output.outcome = PrecompileDispatchOutcome::PolicyRejected;
        output.result = makePrecompileFailureResult(input.message.gas);
        return output;
    }

    auto const code = input.state.get_code(input.target);
    bool const emptyCode = code.empty();

    // EIP-7702 delegation designator: execute via resolveExecutionCode (empty delegate code).
    if (!emptyCode && is7702DelegationDesignator(input.revision, code))
    {
        return output;
    }

    input.state.checkpoint();

    if (emptyCode && !state::isZeroBytes32(input.message.value) && !input.skipValueTransfer)
    {
        auto const value = state::fromEvmC(input.message.value);
        if (!canTransfer(input.state, input.message.sender, value))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = makeInsufficientBalanceResult();
            input.state.revert();
            return output;
        }
        transfer(input.state, input.message.sender, input.target, value);
    }

    if (input.extension != nullptr)
    {
        bool const tryChainHook = emptyCode || input.scope == execution::FrameScope::Nested;
        if (tryChainHook)
        {
            if (auto result =
                    input.extension->tryChainPrecompile(input.revision.revision, input.message))
            {
                output.outcome = PrecompileDispatchOutcome::Dispatched;
                output.result = evmc::Result(std::move(*result));
                finalizeEnvelope(input.state, output);
                return output;
            }
        }
    }

    if (emptyCode && isActivePrecompile(input.revision, input.target))
    {
        if (auto result = state::EthPrecompiles::tryDispatchInCall(
                input.target, input.message, input.revision.revision, input.revision))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = std::move(*result);
            finalizeEnvelope(input.state, output);
            return output;
        }
    }

    if (emptyCode)
    {
        evmc_result result{};
        result.status_code = EVMC_SUCCESS;
        result.gas_left = input.message.gas;
        output.outcome = PrecompileDispatchOutcome::EmptyAccountSuccess;
        output.result = evmc::Result(result);
        finalizeEnvelope(input.state, output);
        return output;
    }

    input.state.revert();
    return output;
}

}  // namespace bcos::evm::precompiled

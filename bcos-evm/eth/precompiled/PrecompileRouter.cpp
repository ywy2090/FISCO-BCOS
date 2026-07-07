/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @brief Precompile dispatch router.
 */

#include "PrecompileRouter.h"
#include "bcos-evm/eth/core/ChainCallTargetPort.h"
#include "bcos-evm/eth/precompiled/EthPrecompiles.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "eth/core/CallTargetTypes.h"
#include "eth/core/RevisionConfig.h"
#include "eth/state/StateKeyHash.hpp"
#include <evmc/evmc.h>
#include <functional>
#include <optional>
#include <utility>

namespace bcos::evm::precompiled
{
namespace
{
evmc::Result makeInsufficientBalanceResult(int64_t gasLeft) noexcept
{
    evmc_result result{};
    result.status_code = EVMC_INSUFFICIENT_BALANCE;
    result.gas_left = gasLeft;
    return evmc::Result(result);
}

evmc::Result makePrecompileFailureResult(int64_t gasLeft) noexcept
{
    evmc_result result{};
    result.status_code = EVMC_PRECOMPILE_FAILURE;
    result.gas_left = gasLeft;
    return evmc::Result(result);
}

// Success commits the checkpoint (value transfer + any state touched by dispatch);
// failure reverts the whole envelope.
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

std::optional<evmc::Result> tryEnvelopeValueTransfer(state::State& state,
    evmc_message const& message, evmc_address const& target, bool skipValueTransfer)
{
    if (state::isZeroBytes32(message.value) || skipValueTransfer ||
        message.kind == EVMC_DELEGATECALL || message.kind == EVMC_CALLCODE)
    {
        return std::nullopt;
    }
    auto const value = state::fromEvmC(message.value);
    if (!state.transfer_balance(message.sender, target, value))
    {
        return makeInsufficientBalanceResult(message.gas);
    }
    return std::nullopt;
}

// Shared skeleton: checkpoint → value transfer → dispatchFn → finalize.
PrecompileRouterOutput envelopeAfterValueTransfer(
    PrecompileEnvelopeInput const& input, std::function<evmc::Result()> dispatchFn)
{
    PrecompileRouterOutput output;
    input.state.checkpoint();

    if (auto insufficient = tryEnvelopeValueTransfer(
            input.state, input.message, input.target.dispatchAddress, input.skipValueTransfer))
    {
        output.route = PrecompileEnvelopeRoute::Precompile;
        output.result = std::move(*insufficient);
        input.state.revert();
        return output;
    }

    output.route = PrecompileEnvelopeRoute::Precompile;
    output.result = dispatchFn();
    finalizeEnvelope(input.state, output);
    return output;
}
}  // namespace

PrecompileRouterOutput executePrecompileEnvelope(PrecompileEnvelopeInput const& input)
{
    return envelopeAfterValueTransfer(input, [&input]() -> evmc::Result {
        if (input.target.route == execution::CallTargetRoute::BuiltinPrecompile)
        {
            if (auto result = EthPrecompiles::tryDispatchInCall(input.target.dispatchAddress,
                    input.message, input.revision.revision, input.revision))
            {
                return std::move(*result);
            }
            return makePrecompileFailureResult(input.message.gas);
        }

        if (input.target.route == execution::CallTargetRoute::ChainPrecompile &&
            input.callTargetPort != nullptr)
        {
            if (auto result =
                    input.callTargetPort->dispatch(input.revision.revision, input.message))
            {
                return evmc::Result(std::move(*result));
            }
        }

        return makePrecompileFailureResult(input.message.gas);
    });
}

PrecompileRouterOutput executeEmptyAccountEnvelope(PrecompileEnvelopeInput const& input)
{
    auto output = envelopeAfterValueTransfer(input, [&input]() -> evmc::Result {
        evmc_result result{};
        result.status_code = EVMC_SUCCESS;
        result.gas_left = input.message.gas;
        return evmc::Result(result);
    });
    output.route = PrecompileEnvelopeRoute::EmptyAccount;
    return output;
}

}  // namespace bcos::evm::precompiled

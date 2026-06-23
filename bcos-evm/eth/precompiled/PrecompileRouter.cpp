/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile dispatch router (Phase 1 skeleton).
 */

#include "PrecompileRouter.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"

namespace bcos::evm::precompiled
{
namespace
{
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
}  // namespace

PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input)
{
    PrecompileRouterOutput output;
    auto const code = input.state.get_code(input.target);
    bool const emptyCode = code.empty();

    if (!state::isZeroBytes32(input.message.value) && !input.skipValueTransfer)
    {
        auto const value = state::fromEvmC(input.message.value);
        if (!canTransfer(input.state, input.message.sender, value))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = makeInsufficientBalanceResult();
            return output;
        }
        transfer(input.state, input.message.sender, input.target, value);
    }

    input.state.checkpoint();

    if (input.extension != nullptr)
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

    if (emptyCode && isActivePrecompile(input.revision.revision, input.revision, input.target))
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

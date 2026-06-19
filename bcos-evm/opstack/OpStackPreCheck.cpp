#include "bcos-evm/opstack/OpStackPreCheck.h"

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include <evmc/evmc.h>
#include <algorithm>

namespace bcos::evm
{
namespace
{
inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

std::optional<EVMCResult> makePreCheckError(
    protocol::TransactionStatus status, evmc_status_code evmcStatus = EVMC_FAILURE)
{
    evmc_result result{};
    result.status_code = evmcStatus;
    result.gas_left = 0;
    return EVMCResult(result, status);
}
}  // namespace

bool isDepositTx(OpStackExecuteViaHostInput const& input) noexcept
{
    return input.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE || input.depositTx.has_value();
}

std::optional<EVMCResult> opStackPreCheck(
    OpStackExecuteViaHostInput const& input, state::State& state)
{
    auto const deposit = isDepositTx(input);
    auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, input.message.gas));

    if (deposit)
    {
        if (input.depositTx.has_value() && input.depositTx->isSystemTransaction)
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }
        if (input.gasPoolSubGasHook && !input.gasPoolSubGasHook(gasLimit))
        {
            return makePreCheckError(protocol::TransactionStatus::OutOfGasLimit, EVMC_OUT_OF_GAS);
        }
        return std::nullopt;
    }

    if (!input.skipNonceChecks)
    {
        auto const stateNonce = state.get_nonce(input.message.sender);
        if (stateNonce != input.nonce)
        {
            return makePreCheckError(protocol::TransactionStatus::NonceCheckFail);
        }
    }

    if (!input.skipTransactionChecks)
    {
        if (input.gasFeeCap < input.gasTipCap || input.gasFeeCap < input.blockInfo.baseFee)
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }

        if (!input.blobVersionedHashes.empty())
        {
            if (!input.revisionConfig.eip4844)
            {
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            }
            if (input.blobGasFeeCap < input.blockInfo.blobBaseFee)
            {
                return makePreCheckError(protocol::TransactionStatus::InsufficientFunds);
            }
        }

        if (!input.authorizations.empty() && isCreateKind(input.message.kind))
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }
    }

    return std::nullopt;
}
}  // namespace bcos::evm

#include "bcos-evm/opstack/OpStackTxPrecheck.h"

#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackBlobTxIntent.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
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

bool isDepositTx(OpStackExecutionRequest const& input) noexcept
{
    return input.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE || input.depositTx.has_value();
}

std::optional<EVMCResult> opStackTxPrecheck(
    OpStackExecutionRequest const& input, state::State& state)
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
        auto const senderCode = state.get_code(input.message.sender);
        if (!senderCode.empty() &&
            !parseDelegationTarget(bcos::bytesConstRef{senderCode.data(), senderCode.size()})
                 .has_value())
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }

        if (input.gasFeeCap < input.gasTipCap || input.gasFeeCap < input.blockInfo.baseFee)
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }

        if (hasBlobTxIntent(input))
        {
            if (!input.revisionConfig.eip4844)
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            if (isCreateKind(input.message.kind))
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            if (input.blobVersionedHashes.empty())
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            for (auto const& hash : input.blobVersionedHashes)
            {
                if (!isValidVersionedHash(hash))
                    return makePreCheckError(protocol::TransactionStatus::Malformed);
            }
            if (input.blobGasFeeCap < input.blockInfo.blobBaseFee)
                return makePreCheckError(protocol::TransactionStatus::InsufficientFunds);
        }

        if (!input.authorizations.empty() && isCreateKind(input.message.kind))
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }
        if (input.authorizationListPresent && input.authorizations.empty())
        {
            return makePreCheckError(protocol::TransactionStatus::Malformed);
        }
    }

    return std::nullopt;
}
}  // namespace bcos::evm

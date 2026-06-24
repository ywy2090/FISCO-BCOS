#include "bcos-evm/eth/EthTxPrecheck.h"

#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/EthReferenceBridge.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>

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

std::optional<EVMCResult> ethTxPrecheck(EthReferenceRequest const& input, state::State& state)
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

    if (input.authorizationListPresent && input.authorizations.empty())
    {
        return makePreCheckError(protocol::TransactionStatus::Malformed);
    }

    if (!input.authorizations.empty() && isCreateKind(input.message.kind))
    {
        return makePreCheckError(protocol::TransactionStatus::Malformed);
    }

    if (input.web3TypedTxKind == 0x04 && isCreateKind(input.message.kind))
    {
        return makePreCheckError(protocol::TransactionStatus::Malformed);
    }

    return std::nullopt;
}
}  // namespace bcos::evm

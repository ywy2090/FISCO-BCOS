#include "bcos-evm/eth/apply/EthTxPrecheck.h"

#include "bcos-evm/eth/Web3TypedTxKind.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/apply/EthEvmResult.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <limits>

namespace bcos::evm
{
namespace
{
inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}
}  // namespace

std::optional<EVMCResult> ethTxPrecheck(EthMessageRequest const& input, state::State& state)
{
    // EIP-2681: account nonce cannot exceed uint64 max; reject txs that cannot be incremented.
    if (input.txNonce == std::numeric_limits<uint64_t>::max())
    {
        return makeEvmcResult(protocol::TransactionStatus::NonceCheckFail);
    }

    auto const senderCode = state.get_code(input.message.sender);
    if (!senderCode.empty() &&
        !parseDelegationTarget(bcos::bytesConstRef{senderCode.data(), senderCode.size()})
             .has_value())
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    if (gas::isEip1559FeeMarketActive(input.revisionConfig))
    {
        if (input.gasFeeCap < input.gasTipCap || input.gasFeeCap < input.blockInfo.baseFee)
        {
            return makeEvmcResult(protocol::TransactionStatus::Malformed);
        }
    }

    if (input.authorizationListPresent && input.authorizations.empty())
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    if (!input.authorizations.empty() && isCreateKind(input.message.kind))
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    if (input.web3TypedTxKind == 0x04 && isCreateKind(input.message.kind))
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    if (!isTypedTxKindSupportedByRevision(input.web3TypedTxKind, input.revisionConfig))
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    if (isInitCodeSizeExceeded(input.revisionConfig.revision, input.message.kind,
            static_cast<size_t>(input.message.input_size)))
    {
        return makeEvmcResult(protocol::TransactionStatus::Malformed);
    }

    return std::nullopt;
}
}  // namespace bcos::evm

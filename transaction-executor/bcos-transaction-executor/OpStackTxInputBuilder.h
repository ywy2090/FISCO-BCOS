#pragma once

#include "Web3Eip7702Decoder.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-evm/bcos/FiscoBlockInfo.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/RollupCost.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include <boost/algorithm/hex.hpp>
#include <algorithm>
#include <atomic>
#include <limits>

namespace bcos::evm
{
namespace opstack_tx
{
class BlockGasPool
{
public:
    explicit BlockGasPool(int64_t gasLimit) : m_remaining(std::max<int64_t>(0, gasLimit)) {}

    [[nodiscard]] bool tryConsume(uint64_t gas) noexcept
    {
        auto requested = static_cast<int64_t>(
            std::min<uint64_t>(gas, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        auto remaining = m_remaining.load(std::memory_order_relaxed);
        while (true)
        {
            if (remaining < requested)
            {
                return false;
            }
            if (m_remaining.compare_exchange_weak(remaining, remaining - requested,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                return true;
            }
        }
    }

private:
    std::atomic<int64_t> m_remaining;
};

inline bcos::u256 parseU256Field(std::string_view field)
{
    if (field.empty())
    {
        return 0;
    }
    try
    {
        return bcos::u256(field);
    }
    catch (...)
    {
        return 0;
    }
}

inline void fillGasCaps(protocol::Transaction const& tx, OpStackExecuteViaHostInput& input)
{
    input.gasTipCap = parseU256Field(tx.maxPriorityFeePerGas());
    input.gasFeeCap = parseU256Field(tx.maxFeePerGas());
    if (input.gasFeeCap == 0)
    {
        input.gasFeeCap = protocol::effectiveGasPrice(tx);
    }
    if (input.gasTipCap == 0)
    {
        input.gasTipCap = input.gasFeeCap;
    }
}

inline state::BlockInfo buildOpStackBlockInfo(protocol::BlockHeader const& blockHeader,
    ledger::LedgerConfig const& ledgerConfig, bcos::u256 baseFeePerGas = {},
    bcos::u256 blobBaseFee = {})
{
    auto blockInfo = state::buildFiscoBlockInfo(blockHeader, ledgerConfig);
    blockInfo.baseFee = baseFeePerGas;
    blockInfo.blobBaseFee = blobBaseFee;
    return blockInfo;
}

inline bcos::u256 resolveOpStackBaseFee(ledger::LedgerConfig const& ledgerConfig)
{
    // TODO(opstack-t11): switch to block-header baseFee when engine exposes it.
    auto const& [gasPrice, _] = ledgerConfig.gasPrice();
    return parseU256Field(gasPrice);
}

inline bcos::u256 resolveOpStackBlobBaseFee(state::StateView const& stateView)
{
    auto const slot =
        stateView.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT));
    return state::fromEvmC(slot);
}

inline std::optional<RollupCostData> buildRollupCostData(protocol::Transaction const& tx)
{
    auto const extra = tx.extraTransactionBytes();
    if (!extra.empty())
    {
        return newRollupCostData(extra);
    }
    return newRollupCostData(tx.input());
}

inline std::optional<OpStackDepositTx> decodeOpStackDepositTx(bcos::bytesConstRef extra)
{
    if (extra.empty() || static_cast<uint8_t>(extra[0]) != bcos::executor::DEPOSIT_TX_TYPE)
    {
        return std::nullopt;
    }

    bcos::bytes copy(extra.begin(), extra.end());
    bcos::bytesRef payload(copy.data() + 1, copy.size() - 1);

    OpStackDepositTx deposit;
    auto [headerError, header] = bcos::codec::rlp::decodeHeader(payload);
    if (headerError != nullptr || !header.isList)
    {
        return std::nullopt;
    }
    auto items = payload.getCroppedData(0, header.payloadLength);
    payload = payload.getCroppedData(header.payloadLength);
    (void)payload;

    bcos::bytes sourceHashRaw;
    bcos::bytes fromRaw;
    bcos::bytes toRaw;
    bcos::bytes mintRaw;
    bcos::bytes valueRaw;
    if (auto error = bcos::codec::rlp::decodeItems(items, sourceHashRaw, fromRaw, toRaw, mintRaw,
            valueRaw, deposit.gas, deposit.isSystemTransaction, deposit.data);
        error != nullptr || !items.empty())
    {
        return std::nullopt;
    }
    if (sourceHashRaw.size() != bcos::h256::SIZE || fromRaw.size() != sizeof(evmc_address))
    {
        return std::nullopt;
    }

    std::copy(sourceHashRaw.begin(), sourceHashRaw.end(), deposit.sourceHash.data());
    std::copy(fromRaw.begin(), fromRaw.end(), deposit.from.bytes);

    web3_tx::decodeU256Bytes(valueRaw, deposit.value);
    if (toRaw.empty())
    {
        deposit.to.reset();
    }
    else
    {
        if (toRaw.size() != sizeof(evmc_address))
        {
            return std::nullopt;
        }
        evmc_address to{};
        std::copy(toRaw.begin(), toRaw.end(), to.bytes);
        deposit.to = to;
    }
    if (!mintRaw.empty())
    {
        bcos::u256 mint = 0;
        web3_tx::decodeU256Bytes(mintRaw, mint);
        deposit.mint = mint;
    }

    return deposit;
}

inline void fillWeb3Fields(protocol::Transaction const& tx, OpStackExecuteViaHostInput& input)
{
    auto const resolved = executor::resolveWeb3AccessList(tx);
    input.web3TypedTxKind = resolved.web3TypedTxKind;
    if (resolved.accessList)
    {
        input.accessList = resolved.accessList.get();
    }
    if (input.web3TypedTxKind == 0x04)
    {
        if (auto decodedAuthorizations =
                web3_tx::decodeEip7702Authorizations(tx.extraTransactionBytes());
            decodedAuthorizations.has_value())
        {
            input.authorizationListPresent = true;
            input.authorizations = std::move(*decodedAuthorizations);
        }
    }
    if (input.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE)
    {
        if (auto decodedDeposit = decodeOpStackDepositTx(tx.extraTransactionBytes());
            decodedDeposit.has_value())
        {
            input.depositTx = std::move(*decodedDeposit);
            input.message.sender = input.depositTx->from;
        }
        else
        {
            OpStackDepositTx deposit;
            deposit.from = state::parseHexAddress(tx.sender());
            deposit.gas = static_cast<uint64_t>(std::max<int64_t>(0, tx.gasLimit()));
            deposit.value = parseU256Field(tx.value());
            deposit.data.assign(tx.input().begin(), tx.input().end());
            input.depositTx = std::move(deposit);
        }
    }
}
}  // namespace opstack_tx
}  // namespace bcos::evm

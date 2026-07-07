#pragma once

#include "Web3Eip7702Decoder.h"
#include "Web3SignedTxEncoder.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/kernel/execution/CreateDeployment.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/types/OpStackBlockHeaderExtension.h"
#include "bcos-evm/storage/LedgerBlockInfo.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
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

    void returnGas(uint64_t gasRemaining, uint64_t gasUsed) noexcept
    {
        auto returned = static_cast<int64_t>(std::min<uint64_t>(
            gasRemaining, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        m_remaining.fetch_add(returned, std::memory_order_acq_rel);
        m_cumulativeUsed.fetch_add(gasUsed, std::memory_order_relaxed);
    }

    [[nodiscard]] int64_t remaining() const noexcept
    {
        return m_remaining.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t cumulativeUsed() const noexcept
    {
        return m_cumulativeUsed.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> m_remaining;
    std::atomic<uint64_t> m_cumulativeUsed{0};
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

inline void fillGasCaps(protocol::Transaction const& tx, OpStackMessageRequest& input)
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

inline state::BlockInfo buildOpStackBlockInfo(
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig)
{
    auto blockInfo = state::buildBlockInfoFromHeader(blockHeader, ledgerConfig);
    auto const fees = requireOpStackHeaderFees(blockHeader);
    blockInfo.baseFee = fees.baseFee;
    blockInfo.blobBaseFee = calcOpStackBlobBaseFee(fees.excessBlobGas);
    return blockInfo;
}

inline bcos::u256 resolveOpStackBaseFee(protocol::BlockHeader const& blockHeader)
{
    return resolveOpStackBaseFeeFromHeader(blockHeader);
}

inline bcos::u256 resolveOpStackBlobBaseFee(protocol::BlockHeader const& blockHeader)
{
    return resolveOpStackBlobBaseFeeFromHeader(blockHeader);
}

inline std::optional<RollupCostData> buildRollupCostData(protocol::Transaction const& tx)
{
    auto const extra = tx.extraTransactionBytes();
    if (!extra.empty())
    {
        if (static_cast<uint8_t>(extra[0]) ==
            toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit))
        {
            // op-geth transaction.go RollupCostData(): DepositTxType → empty struct.
            return RollupCostData{};
        }
        if (tx.type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        {
            if (auto signedBytes = encodeWeb3SignedMarshalBinary(tx); !signedBytes.empty())
            {
                return newRollupCostData(bcos::ref(signedBytes));
            }
        }
        return newRollupCostData(extra);
    }
    return newRollupCostData(tx.input());
}

inline std::optional<OpStackDepositTx> decodeOpStackDepositTx(bcos::bytesConstRef extra)
{
    if (extra.empty() ||
        static_cast<uint8_t>(extra[0]) != toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit))
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

inline void fillWeb3Fields(protocol::Transaction const& tx, OpStackMessageRequest& input)
{
    auto const resolved = executor::resolveWeb3AccessList(tx);
    input.web3TypedTxKind = resolved.web3TypedTxKind;
    if (resolved.accessList)
    {
        input.accessList = resolved.accessList.get();
    }
    if (input.web3TypedTxKind ==
        bcos::evm::toWeb3TypedTxKindValue(bcos::evm::Web3TypedTxKind::EIP7702))
    {
        if (auto decodedAuthorizations =
                web3_tx::decodeEip7702Authorizations(tx.extraTransactionBytes());
            decodedAuthorizations.has_value())
        {
            input.authorizationListPresent = true;
            input.authorizations = std::move(*decodedAuthorizations);
        }
    }
    auto const web3Kind = input.web3TypedTxKind != 0 ?
                              input.web3TypedTxKind :
                              (tx.extraTransactionBytes().empty() ?
                                      uint8_t{0} :
                                      static_cast<uint8_t>(tx.extraTransactionBytes()[0]));
    if (web3Kind == bcos::evm::toWeb3TypedTxKindValue(bcos::evm::Web3TypedTxKind::EIP4844))
    {
        input.web3TypedTxKind = web3Kind;
        if (auto decodedBlobFields = web3_tx::decodeEip4844BlobFields(tx.extraTransactionBytes());
            decodedBlobFields.has_value())
        {
            input.blobGasFeeCap = decodedBlobFields->maxFeePerBlobGas;
            input.blobVersionedHashes = std::move(decodedBlobFields->blobVersionedHashes);
        }
    }
    if (input.web3TypedTxKind == toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit))
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

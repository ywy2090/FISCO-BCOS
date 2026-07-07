#pragma once

#include "Web3Eip7702Decoder.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/storage/LedgerBlockInfo.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-utilities/Common.h"

namespace bcos::evm::eth_tx
{
inline state::BlockInfo buildEthBlockInfo(
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig)
{
    auto blockInfo = state::buildBlockInfoFromHeader(
        blockHeader, ledgerConfig, [](int64_t timestamp) { return timestamp / 1000; });
    blockInfo.baseFee = u256(std::get<0>(ledgerConfig.gasPrice()));
    return blockInfo;
}

inline void fillTransactionGasFields(protocol::Transaction const& tx, auto& data)
{
    data.m_blockInfo = buildEthBlockInfo(data.m_blockHeader.get(), data.m_ledgerConfig.get());

    data.m_gasTipCap = 0;
    data.m_gasFeeCap = 0;
    data.m_hasExplicitFeeCaps = false;
    if (auto tip = tx.maxPriorityFeePerGas(); !tip.empty())
        data.m_gasTipCap = u256(tip);
    if (auto fee = tx.maxFeePerGas(); !fee.empty())
    {
        data.m_gasFeeCap = u256(fee);
        data.m_hasExplicitFeeCaps = true;
    }
    data.m_gasPriceLegacy = u256(tx.gasPrice());

    auto const resolved = executor::resolveWeb3AccessList(tx);
    data.m_web3TypedTxKind = resolved.web3TypedTxKind;
    if (data.m_web3TypedTxKind == 0 && !tx.extraTransactionBytes().empty())
        data.m_web3TypedTxKind = static_cast<uint8_t>(tx.extraTransactionBytes()[0]);
    if (data.m_web3TypedTxKind == 0)
    {
        data.m_web3TypedTxKind = inferWeb3TypedTxKindFromFields(false, false, false,
            data.m_hasExplicitFeeCaps, resolved.accessList && !resolved.accessList->empty());
    }
}

inline void fillWeb3Fields(protocol::Transaction const& tx, EthMessageRequest& input)
{
    auto const resolved = executor::resolveWeb3AccessList(tx);
    input.web3TypedTxKind = resolved.web3TypedTxKind;
    if (resolved.accessList)
    {
        input.accessList = resolved.accessList.get();
    }

    auto const web3Kind = input.web3TypedTxKind != 0 ?
                              input.web3TypedTxKind :
                              (tx.extraTransactionBytes().empty() ?
                                      uint8_t{0} :
                                      static_cast<uint8_t>(tx.extraTransactionBytes()[0]));
    if (web3Kind == toWeb3TypedTxKindValue(Web3TypedTxKind::EIP7702))
    {
        input.web3TypedTxKind = web3Kind;
        if (auto decodedAuthorizations =
                web3_tx::decodeEip7702Authorizations(tx.extraTransactionBytes());
            decodedAuthorizations.has_value())
        {
            input.authorizationListPresent = true;
            input.authorizations = std::move(*decodedAuthorizations);
        }
    }
}
}  // namespace bcos::evm::eth_tx

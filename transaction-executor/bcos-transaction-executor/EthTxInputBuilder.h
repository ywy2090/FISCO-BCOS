#pragma once

#include "Web3Eip7702Decoder.h"
#include "bcos-evm/bcos/FiscoBlockInfo.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"

namespace bcos::evm::eth_tx
{
inline state::BlockInfo buildEthBlockInfo(
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig)
{
    return state::buildFiscoBlockInfo(
        blockHeader, ledgerConfig, [](int64_t timestamp) { return timestamp / 1000; });
}

inline void fillWeb3Fields(protocol::Transaction const& tx, ExecuteViaEthInput& input)
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
    if (web3Kind == 0x04)
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

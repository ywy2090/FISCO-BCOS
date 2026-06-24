#pragma once

#include "Web3Eip7702Decoder.h"
#include "bcos-evm/bcos/FiscoExecutionBridge.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/protocol/Transaction.h"

namespace bcos::evm::fisco_tx
{
inline void fillWeb3Fields(protocol::Transaction const& tx, FiscoExecutionRequest& input)
{
    auto const resolved = executor::resolveWeb3AccessList(tx);
    input.web3TypedTxKind = resolved.web3TypedTxKind;
    if (resolved.accessList)
    {
        input.accessList = resolved.accessList;
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
}  // namespace bcos::evm::fisco_tx

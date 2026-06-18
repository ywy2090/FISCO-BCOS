#pragma once

#include "../RollbackableStorage.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include <functional>

namespace bcos::evm::state
{
inline BlockInfo buildFiscoBlockInfo(
    const bcos::protocol::BlockHeader& blockHeader, const bcos::ledger::LedgerConfig& ledgerConfig,
    const std::function<int64_t(int64_t)>& timestampConverter = [](int64_t t) { return t; })
{
    BlockInfo blockInfo;
    blockInfo.number = blockHeader.number();
    blockInfo.timestamp = timestampConverter(blockHeader.timestamp());
    blockInfo.gasLimit = static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()));
    if (auto const& chainId = ledgerConfig.chainId(); chainId.has_value())
    {
        blockInfo.chainId = fromEvmC(*chainId);
    }
    return blockInfo;
}

template <class Storage>
BlockHashes buildFiscoBlockHashes(Rollbackable<Storage>& storage, int64_t currentBlockNumber)
{
    return [&storage, currentBlockNumber](int64_t number) -> evmc_bytes32 {
        if (number < 0 || number >= currentBlockNumber)
        {
            return {};
        }
        auto hash = task::syncWait(ledger::getBlockHash(storage, number, ledger::fromStorage));
        if (!hash.has_value())
        {
            return {};
        }
        return toEvmC(*hash);
    };
}
}  // namespace bcos::evm::state

#pragma once

#include "bcos-evm/storage/LedgerBlockInfo.h"

namespace bcos::evm::state
{
[[deprecated(
    "use buildBlockInfoFromHeader from bcos-evm/storage/LedgerBlockInfo.h")]] inline BlockInfo
buildFiscoBlockInfo(
    const bcos::protocol::BlockHeader& blockHeader, const bcos::ledger::LedgerConfig& ledgerConfig,
    const std::function<int64_t(int64_t)>& timestampConverter = [](int64_t t) { return t; })
{
    return buildBlockInfoFromHeader(blockHeader, ledgerConfig, timestampConverter);
}

template <class Storage>
[[deprecated(
    "use buildBlockHashesFromStorage from bcos-evm/storage/LedgerBlockInfo.h")]] BlockHashes
buildFiscoBlockHashes(Storage& storage, int64_t currentBlockNumber)
{
    return buildBlockHashesFromStorage(storage, currentBlockNumber);
}
}  // namespace bcos::evm::state

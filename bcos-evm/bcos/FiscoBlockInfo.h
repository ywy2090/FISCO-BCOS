#pragma once

#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include <algorithm>
#include <functional>

namespace bcos::evm::state
{
inline evmc_address resolveCoinbaseFromBlockHeader(const bcos::protocol::BlockHeader& blockHeader)
{
    evmc_address coinbase{};
    auto const extraData = blockHeader.extraData();
    if (extraData.size() >= sizeof(coinbase.bytes))
    {
        std::copy_n(extraData.data(), sizeof(coinbase.bytes), coinbase.bytes);
    }
    return coinbase;
}

inline BlockInfo buildFiscoBlockInfo(
    const bcos::protocol::BlockHeader& blockHeader, const bcos::ledger::LedgerConfig& ledgerConfig,
    const std::function<int64_t(int64_t)>& timestampConverter = [](int64_t t) { return t; })
{
    BlockInfo blockInfo;
    blockInfo.number = blockHeader.number();
    blockInfo.timestamp = timestampConverter(blockHeader.timestamp());
    blockInfo.gasLimit = static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()));
    blockInfo.coinbase = resolveCoinbaseFromBlockHeader(blockHeader);
    if (auto const& chainId = ledgerConfig.chainId(); chainId.has_value())
    {
        blockInfo.chainId = fromEvmC(*chainId);
    }
    return blockInfo;
}

template <class Storage>
BlockHashes buildFiscoBlockHashes(Storage& storage, int64_t currentBlockNumber)
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
        return state::toEvmC(*hash);
    };
}
}  // namespace bcos::evm::state

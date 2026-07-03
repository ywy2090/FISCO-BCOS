#pragma once
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-framework/ledger/Features.h"
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm
{

inline evmc_revision evmcRevisionFromBlockNumber(int64_t blockNum)
{
    if (blockNum >= 25000000)
        return EVMC_OSAKA;
    if (blockNum >= 22000000)
        return EVMC_PRAGUE;
    if (blockNum >= 19426587)
        return EVMC_CANCUN;
    if (blockNum >= 17034870)
        return EVMC_SHANGHAI;
    if (blockNum >= 15537394)
        return EVMC_PARIS;
    return EVMC_LONDON;
}

struct EthChainPolicy
{
    RevisionConfig computeRevisionConfig(const protocol::BlockHeader& header) const
    {
        return revisionConfigFromRevision(evmcRevisionFromBlockNumber(header.number()));
    }

    static evmc_message deriveMessage(bool /*web3Tx*/, const evmc_message& msg,
        int64_t /*blockNum*/, int64_t /*ctxId*/, int64_t /*seq*/, const u256& /*nonce*/,
        const crypto::Hash& /*hashImpl*/)
    {
        return msg;  // Standard: no address override
    }

    int64_t convertTimestamp(int64_t blockTimestampMs) const { return blockTimestampMs / 1000; }

    bool selfdestruct(const evmc_address&, const evmc_address&) const
    {
        return false;  // EIP-3529: no refund
    }

    bool allowDelegateCallToPrecompile() const { return true; }

    const ledger::Features& features() const
    {
        static const ledger::Features empty;
        return empty;
    }
};

}  // namespace bcos::evm

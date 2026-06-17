#pragma once
#include "RevisionConfig.h"
#include "bcos-framework/ledger/Features.h"
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm_standard
{

struct StandardEthPolicy
{
    RevisionConfig computeRevisionConfig(const protocol::BlockHeader& header) const
    {
        RevisionConfig cfg;
        cfg.revision = evmcRevisionFromBlockNumber(header.number());
        cfg.eip2929 = cfg.revision >= EVMC_BERLIN;
        cfg.eip1153 = cfg.revision >= EVMC_CANCUN;
        cfg.eip4844 = cfg.revision >= EVMC_CANCUN;
        cfg.eip5656 = cfg.revision >= EVMC_CANCUN;
        cfg.eip6780 = cfg.revision >= EVMC_CANCUN;
        cfg.eip2537 = cfg.revision >= EVMC_PRAGUE;
        cfg.eip7623 = cfg.revision >= EVMC_PRAGUE;
        cfg.eip7212 = cfg.revision >= EVMC_OSAKA;
        cfg.eip7823 = cfg.revision >= EVMC_OSAKA;
        cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;
        cfg.use_raw_address = false;
        cfg.use_web3_timestamp = true;
        cfg.enable_balance_transfer = true;
        cfg.enable_auth_check = false;
        return cfg;
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

inline evmc_revision evmcRevisionFromBlockNumber(int64_t blockNum)
{
    // Standard Ethereum fork schedule — used for testing the Policy seam only.
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

}  // namespace bcos::evm_standard

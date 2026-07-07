#pragma once
#include "bcos-evm/eth/policy/EthMainnetRevision.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

/// TE-boundary policy for the Eth reference execution path.
struct EthChainPolicy
{
    RevisionConfig computeRevisionConfig(protocol::BlockHeader const& header) const
    {
        return makeEthRevisionConfigFromBlock(header);
    }

    static evmc_message deriveMessage(bool /*web3Tx*/, evmc_message const& msg,
        int64_t /*blockNum*/, int64_t /*ctxId*/, int64_t /*seq*/, u256 const& /*nonce*/,
        crypto::Hash const& /*hashImpl*/)
    {
        return msg;  // Standard: no address override
    }

    int64_t convertTimestamp(int64_t blockTimestampMs) const { return blockTimestampMs / 1000; }
};

}  // namespace bcos::evm

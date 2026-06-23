#pragma once

#include "bcos-evm/bcos/FiscoRevisionConfig.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "transaction-executor/bcos-transaction-executor/RollbackableStorage.h"
#include <evmc/evmc.h>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{
class PrecompiledManager;
}

namespace bcos::transaction_executor
{

template <class RollbackableStorage>
struct ExecutorSessionContext
{
    RollbackableStorage& storage;
    protocol::BlockHeader const& blockHeader;
    ledger::LedgerConfig const& ledgerConfig;
    evmc_address origin;
    int64_t contextID;
    int64_t seq;
    chain_policy::FiscoRevisionConfig const& revisionConfig;
    crypto::Hash const& hashImpl;
    evm::PrecompiledManager& precompiledManager;
};

}  // namespace bcos::transaction_executor

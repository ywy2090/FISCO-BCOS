#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-framework/protocol/LogEntry.h"
#include <evmc/evmc.h>
#include <vector>

namespace bcos::evm
{

struct FiscoExecutionContext
{
    evmc_message message{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    std::vector<protocol::LogEntry> logs;
    bcos::evm::gas::TxGasSettlementContext gasSettlementSnapshot{};
};

}  // namespace bcos::evm

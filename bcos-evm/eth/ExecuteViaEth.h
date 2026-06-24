#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/EthExecutionContext.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-task/Task.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <functional>
#include <optional>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

/// Pure-ethereum orchestration: executeMessage + EthHostExtension, no FISCO auth/precompile hooks.
struct ExecuteViaEthInput
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};

    evmc_message message{};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    bcos::u256 gasPrice{0};
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    uint8_t web3TypedTxKind{0};
    bool hasExplicitFeeCaps{false};
    const Eip2930AccessList* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    std::optional<bcos::h256> txHash;
};

struct ExecuteViaEthOutput
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    EthExecutionContext executionContext;
    bool topLevelIncludedTxVmError{false};
};

task::Task<ExecuteViaEthOutput> executeViaEth(ExecuteViaEthInput input);

}  // namespace bcos::evm

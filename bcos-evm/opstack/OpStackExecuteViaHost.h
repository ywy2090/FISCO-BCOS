#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackTxExecutor.h"
#include "bcos-evm/opstack/RollupCost.h"
#include <bcos-task/Task.h>
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm
{
struct OpStackExecuteViaHostInput
{
    struct SetCodeAuthorization
    {
        std::optional<bcos::u256> chainId;
        evmc_address address{};
        uint64_t nonce{0};
    };

    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
    evmc_message message{};
    uint64_t nonce{0};
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    bcos::u256 blobGasFeeCap{0};
    std::vector<bcos::h256> blobVersionedHashes;
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    state::TransactionProperties txProps{};
    const Eip2930AccessList* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
    std::optional<OpStackDepositTx> depositTx;
    std::vector<SetCodeAuthorization> authorizations;
    bool call{false};
    bool skipNonceChecks{false};
    bool skipTransactionChecks{false};
    bool noBaseFee{false};
    uint64_t floorDataGas{0};
    std::optional<RollupCostData> rollupCostData;
    std::function<bool(uint64_t)> gasPoolSubGasHook;
    OpStackTxExecutor opTxExecutor{};
};

struct OpStackExecuteViaHostOutput
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
    int64_t gasUsed{0};
};

task::Task<OpStackExecuteViaHostOutput> opStackExecuteViaHost(OpStackExecuteViaHostInput input);
}  // namespace bcos::evm

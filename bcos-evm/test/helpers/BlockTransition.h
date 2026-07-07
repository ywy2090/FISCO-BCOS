#pragma once

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/BlockRequests.h"
#include "helpers/BlockSystemCalls.h"
#include "helpers/BlockValidation.h"
#include "helpers/BloomFilter.hpp"
#include <bcos-task/Wait.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

inline bool isConsensusRejectedExitKind(StateTransitionExitKind exitKind) noexcept
{
    switch (exitKind)
    {
    case StateTransitionExitKind::RulesRejected:
    case StateTransitionExitKind::GasAffordRejected:
    case StateTransitionExitKind::IntrinsicRejected:
        return true;
    default:
        return false;
    }
}

/// Block-level tx rejection (spec §8.2 Level 2): consensus entry rejects plus buyGas abort
/// before stateTransitionExecute (ApplyEthMessage early return leaves exitKind=None).
inline bool isBlockTxRejected(ExecutionResult const& execResult) noexcept
{
    if (isConsensusRejectedExitKind(execResult.exitKind))
        return true;
    return execResult.exitKind == StateTransitionExitKind::None &&
           execResult.status == EVMC_INSUFFICIENT_BALANCE &&
           execResult.receiptStatus == protocol::TransactionStatus::NotEnoughCash;
}

struct TransactionReceipt
{
    /// Post-normalize settlement status (ADR-015 included vmerr → EVMC_SUCCESS).
    evmc_status_code settlementStatus{EVMC_SUCCESS};
    /// Receipt / RPC failure bit (Gap 40 — preserved across included vmerr).
    protocol::TransactionStatus receiptStatus{protocol::TransactionStatus::None};
    bool topLevelIncludedTxVmError{false};
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    state::LogEntry log;
    int64_t gasUsed = 0;
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gasRefund = 0;              // EIP-7778
    bcos::bytes bloom;                  // 256-byte logs bloom for this tx
    std::vector<state::LogEntry> logs;  // all logs (supersedes single `log`; `log` kept for compat)
    uint8_t txType = 0;                 // EIP-2718 typed receipt prefix
};

struct BlockApplyResult
{
    TestStateView postState;
    std::vector<TransactionReceipt> receipts;
    int64_t gasUsed = 0;
    bcos::bytes bloom;                  // aggregate 256-byte block logs bloom
    std::vector<bcos::bytes> requests;  // EIP-7685 requests (Prague+); see §8.3.1
    std::optional<std::string> requestsError;
    std::vector<size_t> rejected;  // indices of txs rejected during block apply
    uint64_t blobGasLeft = 0;      // maxBlobGasPerBlock - consumed
};

/// Execution-time block context: beacon root + blob base fee from parent excess (EIP-4844).
inline state::BlockInfo blockInfoForExecution(state::BlockInfo const& base, TestBlock const& tb,
    TestBlockHeader const* parent, evmc_revision rev, bcos::u256 chainId = 0)
{
    state::BlockInfo bi = base;
    bi.chainId = chainId;
    bi.parentBeaconBlockRoot = tb.expectedBlockHeader.parentBeaconBlockRoot;
    if (rev >= EVMC_CANCUN)
    {
        if (tb.inputExcessBlobGas.has_value())
            bi.blobBaseFee = gas::calcBlobBaseFee(*tb.inputExcessBlobGas, rev);
        else if (parent != nullptr && parent->excessBlobGas.has_value())
            bi.blobBaseFee = gas::calcBlobBaseFee(*parent->excessBlobGas, rev);
    }
    return bi;
}

/// Apply a sequence of transactions to the pre-state within a single block.
/// Each tx is executed via EthMessageAdapter::execute(); state diffs accumulate.
/// Coinbase reward is set to 0 (standard test convention).
inline BlockApplyResult applyEthBlock(TestStateView& preState,
    std::span<GstTransactionTemplate const> transactions, state::BlockInfo const& blockInfo,
    ForkProfile const& profile, evmc::VM& vm, bcos::crypto::Hash& hashImpl,
    state::BlockHashes blockHashes = {}, std::span<const Withdrawal> withdrawals = {})
{
    BlockApplyResult result;
    result.gasUsed = 0;

    // Accumulated state diff across all transactions
    state::StateDiff accumulatedDiff;

    // Full block pre-state; updated after each tx for sequential execution.
    std::vector<std::pair<evmc_address, state::Account>> preStatePairs;
    preStatePairs.reserve(preState.accounts().size());
    for (auto const& [addr, acc] : preState.accounts())
        preStatePairs.emplace_back(addr, acc);

    // Cancun+: block-start system calls (EIP-4788) before user transactions.
    if (profile.revision.revision >= EVMC_CANCUN)
    {
        auto const sysDiff = applyCancunBlockSystemCalls(
            preState, blockInfo, profile.revision, vm, hashImpl, blockHashes);
        mergeStateDiffIntoPairs(preStatePairs, sysDiff);
    }

    // Prague+: block-start system calls (EIP-2935) before user transactions.
    if (profile.revision.revision >= EVMC_PRAGUE)
    {
        auto const sysDiff = applyPragueBlockSystemCalls(
            preState, blockInfo, profile.revision, vm, hashImpl, blockHashes);
        mergeStateDiffIntoPairs(preStatePairs, sysDiff);
    }

    uint64_t blobGasConsumed = 0;

    for (size_t txIndex = 0; txIndex < transactions.size(); ++txIndex)
    {
        auto const& tmpl = transactions[txIndex];
        StateTestCase tc;
        tc.env = blockInfo;
        tc.transaction = tmpl;
        tc.tx = materializeTransaction(tmpl);

        // Pre-state: current block state (includes contract code/storage).
        for (auto const& [addr, acc] : preStatePairs)
            tc.preState.emplace_back(addr, acc);

        StateSubtest st;
        st.fork = profile.upstreamForkName;
        st.dataIndex = 0;
        st.gasIndex = 0;
        st.valueIndex = 0;

        EthMessageAdapter adapter(profile, hashImpl, vm);
        if (blockHashes)
            adapter.setBlockHashes(blockHashes);
        auto execResult = task::syncWait(adapter.execute(tc, st));

        bool const consensusRejected = isBlockTxRejected(execResult);
        if (consensusRejected)
            result.rejected.push_back(txIndex);
        else
        {
            result.gasUsed += execResult.gasUsed;
            blobGasConsumed +=
                static_cast<uint64_t>(tmpl.blobVersionedHashes.size()) * GAS_PER_BLOB;

            // Accumulate diff
            for (auto const& [addr, acc] : execResult.stateDiff.accounts)
                mergeStateDiffAccount(accumulatedDiff.accounts[addr], acc);

            // Propagate state diff into preStatePairs so the next transaction
            // sees this transaction's balance/nonce/storage mutations.
            for (auto const& [addr, acc] : execResult.stateDiff.accounts)
            {
                bool found = false;
                for (auto& [pAddr, pAcc] : preStatePairs)
                {
                    if (state::AddressEqual{}(pAddr, addr))
                    {
                        mergeStateDiffAccount(pAcc, acc);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    preStatePairs.emplace_back(addr, acc);
            }

            if (profile.revision.revision >= EVMC_SPURIOUS_DRAGON)
            {
                preStatePairs.erase(std::remove_if(preStatePairs.begin(), preStatePairs.end(),
                                        [](auto const& entry) {
                                            auto const& acc = entry.second;
                                            return acc.nonce == 0 && acc.balance == 0 &&
                                                   acc.code.empty();
                                        }),
                    preStatePairs.end());
            }
        }

        // Collect receipt (settlement vs receipt status split mirrors ADR-015 / Gap 40).
        TransactionReceipt receipt;
        receipt.settlementStatus = execResult.status;
        receipt.status = execResult.status;
        receipt.receiptStatus = execResult.receiptStatus;
        receipt.topLevelIncludedTxVmError = execResult.topLevelIncludedTxVmError;
        receipt.exitKind = execResult.exitKind;
        receipt.gasUsed = execResult.gasUsed;
        receipt.logs = execResult.logs;
        if (!execResult.logs.empty())
            receipt.log = execResult.logs.front();
        receipt.bloom = state::computeLogsBloom(execResult.logs);
        receipt.txType = inferWeb3TypedTxKindFromFields(tmpl.authorizationListKeyPresent,
            !tmpl.authorizationList.empty(), !tmpl.blobVersionedHashes.empty(),
            tmpl.maxFeePerBlobGasKeyPresent,
            tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0, !tmpl.accessLists.empty());
        result.receipts.push_back(std::move(receipt));
    }

    result.bloom.assign(state::LOGS_BLOOM_BYTES, 0);
    for (auto const& rc : result.receipts)
        state::bloomOr(result.bloom, rc.bloom);

    if (profile.revision.revision >= EVMC_CANCUN)
    {
        BlobParams const blobParams = blobParamsFor({}, profile.upstreamForkName);
        result.blobGasLeft = maxBlobGasPerBlock(blobParams) - blobGasConsumed;
    }

    // Prague+: collect EIP-7685 requests (deposit logs + block-end system calls) before finalize.
    if (profile.revision.revision >= EVMC_PRAGUE)
    {
        TestStateView postTxState;
        for (auto const& [addr, acc] : preStatePairs)
            postTxState.insertAccount(addr, acc);

        auto reqCollect = collectPragueBlockRequests(
            postTxState, blockInfo, profile.revision, vm, hashImpl, blockHashes, result.receipts);
        if (reqCollect.error)
            result.requestsError = *reqCollect.error;
        else
            result.requests = std::move(reqCollect.requests);

        // collectPragueBlockRequests mutates postTxState in place (evmone block_state).
        // Do not merge reqCollect.stateDiff into preStatePairs: accumulated diff drops
        // zero-valued storage clears (EIP-7002 count/head/tail reset after block-end).
        preStatePairs.clear();
        preStatePairs.reserve(postTxState.accounts().size());
        for (auto const& [addr, acc] : postTxState.accounts())
            preStatePairs.emplace_back(addr, acc);
    }

    // preStatePairs already includes tx + block-end request mutations; do not re-apply
    // accumulatedDiff (evmone: single block_state mutated in place).
    bool const eip158 = profile.revision.revision >= EVMC_SPURIOUS_DRAGON;
    auto postView =
        buildPostStateView(preStatePairs, accumulatedDiff, false, blockInfo.coinbase, eip158);

    if (auto const reward = miningReward(profile.revision.revision))
    {
        bool found = false;
        for (auto& [addr, acc] : postView.accounts)
        {
            if (state::AddressEqual{}(addr, blockInfo.coinbase))
            {
                acc.balance += *reward;
                found = true;
                break;
            }
        }
        if (!found)
        {
            state::Account acc;
            acc.balance = *reward;
            postView.accounts.emplace_back(blockInfo.coinbase, std::move(acc));
        }
    }

    bcos::u256 const gweiToWei{1000000000};
    for (auto const& w : withdrawals)
    {
        bool found = false;
        for (auto& [addr, acc] : postView.accounts)
        {
            if (state::AddressEqual{}(addr, w.address))
            {
                acc.balance += bcos::u256(w.amount) * gweiToWei;
                found = true;
                break;
            }
        }
        if (!found)
        {
            state::Account acc;
            acc.balance = bcos::u256(w.amount) * gweiToWei;
            postView.accounts.emplace_back(w.address, std::move(acc));
        }
    }

    // Populate TestStateView from GstPostStateView
    for (auto const& [addr, acc] : postView.accounts)
        result.postState.insertAccount(addr, acc);

    return result;
}

/// Finalize block-level state (reserved for future block-level hooks).
/// PoW mining reward is credited in applyEthBlock via miningReward() (pre-Paris only).
inline void finalizeBlockState(
    TestStateView& /*state*/, state::BlockInfo const& /*block*/, evmc_revision /*rev*/)
{}

}  // namespace bcos::evm::reference_tests

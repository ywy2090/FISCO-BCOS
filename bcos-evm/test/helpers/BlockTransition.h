#pragma once

#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/BlockSystemCalls.h"
#include <bcos-task/Wait.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

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

/// Apply a sequence of transactions to the pre-state within a single block.
/// Each tx is executed via EthMessageAdapter::execute(); state diffs accumulate.
/// Coinbase reward is set to 0 (standard test convention).
inline BlockApplyResult applyEthBlock(TestStateView& preState,
    std::span<GstTransactionTemplate const> transactions, state::BlockInfo const& blockInfo,
    ForkProfile const& profile, evmc::VM& vm, bcos::crypto::Hash& hashImpl,
    state::BlockHashes blockHashes = {})
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
        auto const sysDiff =
            applyCancunBlockSystemCalls(preState, blockInfo, profile.revision, vm, hashImpl);
        mergeStateDiffIntoPairs(preStatePairs, sysDiff);
    }

    // Prague+: block-start system calls (EIP-2935) before user transactions.
    if (profile.revision.revision >= EVMC_PRAGUE)
    {
        auto const sysDiff =
            applyPragueBlockSystemCalls(preState, blockInfo, profile.revision, vm, hashImpl);
        mergeStateDiffIntoPairs(preStatePairs, sysDiff);
    }

    for (auto const& tmpl : transactions)
    {
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

        result.gasUsed += execResult.gasUsed;

        // Accumulate diff
        for (auto const& [addr, acc] : execResult.stateDiff.accounts)
        {
            auto& merged = accumulatedDiff.accounts[addr];
            merged.nonce = acc.nonce;
            merged.balance = acc.balance;
            if (!acc.code.empty())
                merged.code = acc.code;
            merged.codeHash = acc.codeHash;
            for (auto const& [slot, value] : acc.storage)
            {
                if (state::isZeroBytes32(value))
                    merged.storage.erase(slot);
                else
                    merged.storage[slot] = value;
            }
        }

        // Propagate state diff into preStatePairs so the next transaction
        // sees this transaction's balance/nonce/storage mutations.
        for (auto const& [addr, acc] : execResult.stateDiff.accounts)
        {
            bool found = false;
            for (auto& [pAddr, pAcc] : preStatePairs)
            {
                if (state::AddressEqual{}(pAddr, addr))
                {
                    pAcc.nonce = acc.nonce;
                    pAcc.balance = acc.balance;
                    if (!acc.code.empty())
                        pAcc.code = acc.code;
                    pAcc.codeHash = acc.codeHash;
                    for (auto const& [slot, value] : acc.storage)
                    {
                        if (state::isZeroBytes32(value))
                            pAcc.storage.erase(slot);
                        else
                            pAcc.storage[slot] = value;
                    }
                    found = true;
                    break;
                }
            }
            if (!found)
                preStatePairs.emplace_back(addr, acc);
        }

        // Collect receipt (settlement vs receipt status split mirrors ADR-015 / Gap 40).
        TransactionReceipt receipt;
        receipt.settlementStatus = execResult.status;
        receipt.receiptStatus = execResult.receiptStatus;
        receipt.topLevelIncludedTxVmError = execResult.topLevelIncludedTxVmError;
        receipt.exitKind = execResult.exitKind;
        receipt.gasUsed = execResult.gasUsed;
        if (!execResult.logs.empty())
            receipt.log = execResult.logs.front();
        result.receipts.push_back(std::move(receipt));
    }

    // Build post-state from pre-state + accumulated diff
    auto postView = buildPostStateView(
        preStatePairs, accumulatedDiff, true, blockInfo.coinbase, profile.revision.eip1559);

    // Populate TestStateView from GstPostStateView
    for (auto const& [addr, acc] : postView.accounts)
        result.postState.insertAccount(addr, acc);

    return result;
}

/// Finalize block-level state (coinbase reward, etc.)
/// For Ethereum tests, coinbase reward is set to 0 (no block reward in test fixtures).
inline void finalizeBlockState(
    TestStateView& /*state*/, state::BlockInfo const& /*block*/, evmc_revision /*rev*/)
{}

}  // namespace bcos::evm::reference_tests

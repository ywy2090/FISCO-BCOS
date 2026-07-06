#pragma once

#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "helpers/BlockSystemCalls.h"
#include <bcos-task/Wait.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bcos::evm::reference_tests
{

struct TransactionReceipt
{
    state::LogEntry log;
    int64_t gasUsed = 0;
};

struct BlockApplyResult
{
    TestStateView postState;
    std::vector<TransactionReceipt> receipts;
    int64_t gasUsed = 0;
};

/// Apply a sequence of transactions to the pre-state within a single block.
/// Each tx is executed via EthMessageAdapter::execute(); state diffs accumulate.
/// Coinbase reward is set to 0 (standard test convention).
inline BlockApplyResult applyEthBlock(TestStateView& preState,
    std::span<const state::Transaction> transactions, state::BlockInfo const& blockInfo,
    ForkProfile const& profile, evmc::VM& vm, bcos::crypto::Hash& hashImpl)
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

    for (auto const& tx : transactions)
    {
        // Build a StateTestCase for this single transaction
        StateTestCase tc;
        tc.env = blockInfo;
        tc.tx = tx;
        tc.transaction.gasLimit.push_back(static_cast<uint64_t>(tx.gasLimit));
        tc.transaction.data.push_back(tx.data);
        tc.transaction.value.push_back(tx.value);
        tc.transaction.gasPrice = tx.gasPrice;
        tc.transaction.nonce = tx.nonce;
        tc.transaction.sender = tx.from;

        if (tx.to.has_value())
        {
            tc.transaction.to =
                "0x" + bcos::toHex(bcos::bytes(tx.to->bytes, tx.to->bytes + sizeof(tx.to->bytes)));
        }

        // Pre-state: current block state (includes contract code/storage).
        for (auto const& [addr, acc] : preStatePairs)
            tc.preState.emplace_back(addr, acc);

        StateSubtest st;
        st.fork = profile.upstreamForkName;
        st.dataIndex = 0;
        st.gasIndex = 0;
        st.valueIndex = 0;

        EthMessageAdapter adapter(profile, hashImpl, vm);
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

        // Collect receipt
        TransactionReceipt receipt;
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

#pragma once

#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include <bcos-task/Wait.h>
#include <cstring>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

/// geth: 0xfffffffffffffffffffffffffffffffffffffffe
inline constexpr evmc_address kSystemSenderAddress{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};

/// EIP-4788 beacon roots contract (Cancun+).
inline constexpr evmc_address kBeaconRootsAddress{0x00, 0x0f, 0x3d, 0xf6, 0xd7, 0x32, 0x80, 0x7e,
    0xf1, 0x31, 0x9f, 0xb7, 0xb8, 0xbb, 0x85, 0x22, 0xd0, 0xbe, 0xac, 0x02};

/// EIP-2935 history storage contract (Prague+).
inline constexpr evmc_address kHistoryStorageAddress{0x00, 0x00, 0xf9, 0x08, 0x27, 0xf1, 0xc5, 0x3a,
    0x10, 0xcb, 0x7a, 0x02, 0x33, 0x5b, 0x17, 0x53, 0x20, 0x00, 0x29, 0x35};

inline void mergeStateDiffAccount(
    state::Account& merged, state::Account const& patch, bool eraseZeroStorage = true)
{
    if (patch.nonceDirty)
        merged.nonce = patch.nonce;
    if (patch.balanceDirty)
        merged.balance = patch.balance;
    if (!patch.code.empty() || patch.codeDirty)
    {
        merged.code = patch.code;
        merged.codeHash = patch.codeHash;
        merged.codeDirty = patch.codeDirty;
    }
    for (auto const& [slot, value] : patch.storage)
    {
        if (eraseZeroStorage && state::isZeroBytes32(value))
            merged.storage.erase(slot);
        else
            merged.storage[slot] = value;
    }
}

inline void mergeStateDiffIntoPairs(
    std::vector<std::pair<evmc_address, state::Account>>& accounts, state::StateDiff const& diff)
{
    for (auto const& [addr, acc] : diff.accounts)
    {
        bool found = false;
        for (auto& [pAddr, pAcc] : accounts)
        {
            if (state::AddressEqual{}(pAddr, addr))
            {
                mergeStateDiffAccount(pAcc, acc);
                found = true;
                break;
            }
        }
        if (!found)
            accounts.emplace_back(addr, acc);
    }
}

inline void mergeStateDiffIntoView(TestStateView& view, state::StateDiff const& diff)
{
    for (auto const& [addr, acc] : diff.accounts)
    {
        auto existing = view.get_account(addr);
        state::Account merged = existing.has_value() ? *existing : state::Account{};
        mergeStateDiffAccount(merged, acc);
        view.insertAccount(addr, std::move(merged));
    }
}

/// Fixed gas budget for block-start/end system calls (evmone execute_system_call).
inline constexpr int64_t kBlockSystemCallGas = 30'000'000;

/// Cancun block-start system calls (EIP-4788 beacon block root). Returns state diff to merge.
inline state::StateDiff applyCancunBlockSystemCalls(TestStateView& state,
    state::BlockInfo const& blockInfo, bcos::evm::RevisionConfig const& revision, evmc::VM& vm,
    bcos::crypto::Hash& hashImpl, state::BlockHashes const& blockHashes = {})
{
    state::StateDiff empty{};
    if (revision.revision < EVMC_CANCUN)
        return empty;

    if (!state.get_account(kBeaconRootsAddress).has_value())
        return empty;

    bcos::bytes const calldata(blockInfo.parentBeaconBlockRoot.bytes,
        blockInfo.parentBeaconBlockRoot.bytes + sizeof(blockInfo.parentBeaconBlockRoot.bytes));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = kBlockSystemCallGas;
    msg.sender = kSystemSenderAddress;
    msg.recipient = kBeaconRootsAddress;
    msg.code_address = kBeaconRootsAddress;
    msg.input_data = calldata.data();
    msg.input_size = calldata.size();

    EthMessageRequest input{};
    input.stateView = &state;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = msg;
    input.blockInfo = blockInfo;
    input.revisionConfig = revision;
    input.isCall = true;
    if (blockHashes)
        input.blockHashes = blockHashes;
    else
        input.blockHashes = [](int64_t) { return evmc_bytes32{}; };

    auto output = task::syncWait(applyEthMessage(std::move(input)));
    auto diff = output.stateDiff;
    // System sender must not persist in block state (geth discards after system call).
    diff.accounts.erase(kSystemSenderAddress);
    return diff;
}

/// Prague block-start system calls (EIP-2935 parent block hash into history storage).
inline state::StateDiff applyPragueBlockSystemCalls(TestStateView& state,
    state::BlockInfo const& blockInfo, bcos::evm::RevisionConfig const& revision, evmc::VM& vm,
    bcos::crypto::Hash& hashImpl, state::BlockHashes const& blockHashes = {})
{
    state::StateDiff empty{};
    if (revision.revision < EVMC_PRAGUE)
        return empty;

    if (blockInfo.number <= 0)
        return empty;

    if (!state.get_account(kHistoryStorageAddress).has_value())
        return empty;

    evmc_bytes32 parentHash = blockInfo.parentHash;
    if (blockHashes)
    {
        auto const fromLookup = blockHashes(blockInfo.number - 1);
        if (!state::isZeroBytes32(fromLookup))
            parentHash = fromLookup;
    }

    bcos::bytes const calldata(parentHash.bytes, parentHash.bytes + sizeof(parentHash.bytes));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = kBlockSystemCallGas;
    msg.sender = kSystemSenderAddress;
    msg.recipient = kHistoryStorageAddress;
    msg.code_address = kHistoryStorageAddress;
    msg.input_data = calldata.data();
    msg.input_size = calldata.size();

    EthMessageRequest input{};
    input.stateView = &state;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = msg;
    input.blockInfo = blockInfo;
    input.revisionConfig = revision;
    input.isCall = true;
    if (blockHashes)
        input.blockHashes = blockHashes;
    else
        input.blockHashes = [](int64_t) { return evmc_bytes32{}; };

    auto output = task::syncWait(applyEthMessage(std::move(input)));
    auto diff = output.stateDiff;
    diff.accounts.erase(kSystemSenderAddress);
    return diff;
}

}  // namespace bcos::evm::reference_tests

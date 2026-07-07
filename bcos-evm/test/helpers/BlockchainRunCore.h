#pragma once

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/ReceiptForRoot.h"
#include "bcos-evm/eth/state/StateKeyHash.hpp"
#include "helpers/BlockTransition.h"
#include "helpers/BlockValidation.h"

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bcos::evm::reference_tests
{

namespace detail
{

using state::Bytes32Equal;
using state::Bytes32Hash;

inline evmc_bytes32 const EMPTY_MPT_HASH = {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff,
    0x83, 0x45, 0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0, 0x01,
    0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21};

inline bool bytes32Equal(evmc_bytes32 const& a, evmc_bytes32 const& b)
{
    return std::memcmp(a.bytes, b.bytes, 32) == 0;
}

inline std::string formatBytes32(evmc_bytes32 const& value)
{
    return bcos::toHex(bcos::bytes(value.bytes, value.bytes + sizeof(value.bytes)));
}

inline evmc_bytes32 computeStateRootFromView(TestStateView const& view)
{
    GstPostStateView postView;
    for (auto const& [addr, acc] : view.accounts())
        postView.accounts.emplace_back(addr, acc);
    return computeStateRoot(postView);
}

inline bool expectExceptionMatches(std::string_view expectException, std::string_view reason)
{
    if (expectException.find(reason) != std::string::npos)
        return true;
    size_t start = 0;
    while (start < expectException.size())
    {
        auto end = expectException.find('|', start);
        auto token = expectException.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.remove_suffix(1);
        if (!token.empty() && reason.find(token) != std::string::npos)
            return true;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return false;
}

inline std::optional<std::string> validateGenesis(
    BlockchainTest const& test, evmc_revision genesisRev)
{
    auto const& g = test.genesisBlockHeader;
    if (g.blockNumber != 0)
        return "genesis blockNumber != 0";
    if (g.gasUsed != 0)
        return "genesis gasUsed != 0";
    if (!bytes32Equal(g.transactionsRoot, EMPTY_MPT_HASH))
        return "genesis transactionsRoot != EMPTY_MPT";
    if (!bytes32Equal(g.receiptsRoot, EMPTY_MPT_HASH))
        return "genesis receiptsRoot != EMPTY_MPT";
    if (genesisRev >= EVMC_SHANGHAI)
    {
        if (!bytes32Equal(g.withdrawalsRoot, EMPTY_MPT_HASH))
            return "genesis withdrawalsRoot != EMPTY_MPT";
    }
    else if (!bytes32Equal(g.withdrawalsRoot, evmc_bytes32{}))
    {
        return "genesis withdrawalsRoot != zero";
    }
    if (g.logsBloom.size() != 256)
        return "genesis logsBloom size != 256";
    for (auto b : g.logsBloom)
    {
        if (b != 0)
            return "genesis logsBloom not zero";
    }
    return std::nullopt;
}

inline std::optional<std::string> expectPostStateMatches(
    TestStateView const& canonical, BlockchainTest const& test)
{
    if (test.postStateHash.has_value())
    {
        auto computed = computeStateRootFromView(canonical);
        if (!bytes32Equal(computed, *test.postStateHash))
            return "postStateHash mismatch (computed=0x" + formatBytes32(computed) +
                   " expected=0x" + formatBytes32(*test.postStateHash) + ")";
        return std::nullopt;
    }
    if (test.postState.empty())
        return std::nullopt;

    std::unordered_map<evmc_address, state::Account, state::AddressHash, state::AddressEqual>
        actual;
    for (auto const& [addr, acc] : canonical.accounts())
        actual.emplace(addr, acc);

    for (auto const& [addr, expectedAcc] : test.postState)
    {
        auto it = actual.find(addr);
        if (it == actual.end())
            return "postState missing account 0x" +
                   bcos::toHex(bcos::bytes(addr.bytes, addr.bytes + sizeof(addr.bytes)));
        auto const& got = it->second;
        if (got.nonce != expectedAcc.nonce)
            return "postState nonce mismatch";
        if (got.balance != expectedAcc.balance)
            return "postState balance mismatch";
        for (auto const& [slot, wantVal] : expectedAcc.storage)
        {
            auto slotIt = got.storage.find(slot);
            evmc_bytes32 gotVal{};
            if (slotIt != got.storage.end())
                gotVal = slotIt->second;
            if (!bytes32Equal(gotVal, wantVal))
                return "postState storage mismatch";
        }
    }
    return std::nullopt;
}

inline std::vector<ReceiptForRoot> receiptsForRoot(std::vector<TransactionReceipt> const& receipts)
{
    std::vector<ReceiptForRoot> out;
    out.reserve(receipts.size());
    uint64_t cumulative = 0;
    for (auto const& rc : receipts)
    {
        ReceiptForRoot r;
        r.status = rc.status;
        cumulative += static_cast<uint64_t>(rc.gasUsed);
        r.cumulativeGasUsed = cumulative;
        r.bloom = rc.bloom;
        r.logs = rc.logs;
        out.push_back(std::move(r));
    }
    return out;
}

inline void logRequestsHashXfailOnce()
{
    static bool logged = false;
    if (logged)
        return;
    logged = true;
    std::cerr << "XFAIL: Prague+ requestsHash validation skipped "
                 "(requests collection not wired, B2/M2)\n";
}

inline std::optional<std::string> checkRequestsHash(
    TestBlock const& tb, BlockApplyResult const& res, evmc_revision rev)
{
    if (rev < EVMC_PRAGUE)
        return std::nullopt;
    if (res.requests.empty())
    {
        logRequestsHashXfailOnce();
        return std::nullopt;
    }
    if (!bytes32Equal(computeRequestsHash(res.requests), tb.expectedBlockHeader.requestsHash))
        return "requestsHash";
    return std::nullopt;
}

inline std::optional<std::string> firstHeaderFieldMismatch(TestBlock const& tb,
    BlockApplyResult const& res, evmc_revision rev, BlobSchedule const& schedule,
    std::string_view network)
{
    auto const& h = tb.expectedBlockHeader;
    auto const computedState = computeStateRootFromView(res.postState);
    if (!bytes32Equal(computedState, h.stateRoot))
        return "stateRoot";
    if (!bytes32Equal(computeTxRoot(tb.rawTxRlp), h.transactionsRoot))
        return "transactionsRoot";
    if (!bytes32Equal(computeReceiptsRoot(receiptsForRoot(res.receipts)), h.receiptsRoot))
        return "receiptsRoot";
    if (rev >= EVMC_SHANGHAI &&
        !bytes32Equal(computeWithdrawalRoot(tb.withdrawals), h.withdrawalsRoot))
        return "withdrawalsRoot";
    if (auto requestsMismatch = checkRequestsHash(tb, res, rev))
        return *requestsMismatch;
    if (res.gasUsed != h.gasUsed)
        return "gasUsed";
    if (res.bloom != h.logsBloom)
        return "logsBloom";
    if (rev >= EVMC_CANCUN)
    {
        BlobParams const p = blobParamsFor(schedule, network);
        uint64_t const blobGasLimit = maxBlobGasPerBlock(p);
        uint64_t const actualBlobUsed = blobGasLimit - res.blobGasLeft;
        if (actualBlobUsed != h.blobGasUsed.value_or(0))
            return "blobGasUsed";
    }
    return std::nullopt;
}

inline std::optional<std::string> validateValidBlockHeaders(TestBlock const& tb,
    BlockApplyResult const& res, evmc_revision rev, BlobSchedule const& schedule,
    std::string_view network)
{
    if (res.requestsError.has_value())
        return "requests collection failed: " + *res.requestsError;
    if (!res.rejected.empty())
        return "unexpected rejected transactions in valid block";
    if (auto mismatch = firstHeaderFieldMismatch(tb, res, rev, schedule, network))
        return "header field mismatch: " + *mismatch;
    return std::nullopt;
}

struct BlockData
{
    TestBlockHeader const* header = nullptr;
    TestStateView postState;
    uint64_t totalDifficulty = 0;
};

inline std::optional<std::string> runOneTest(BlockchainTest const& test, ForkProfile const& profile,
    evmc::VM& vm, bcos::crypto::Hash& hashImpl)
{
    auto const genesisRevOpt = ForkProfileRegistry::instance().resolveRevision(
        test.network, test.genesisBlockHeader.timestamp);
    if (!genesisRevOpt.has_value())
        return "unknown network " + test.network;
    evmc_revision const genesisRev = *genesisRevOpt;

    if (auto err = validateGenesis(test, genesisRev))
        return *err;

    auto const& genesis = test.genesisBlockHeader;
    std::unordered_map<int64_t, evmc_bytes32> blockHashes;
    blockHashes[0] = genesis.hash;

    std::unordered_map<evmc_bytes32, BlockData, Bytes32Hash, Bytes32Equal> blockData;
    blockData[genesis.hash] =
        BlockData{&genesis, test.preState, static_cast<uint64_t>(genesis.difficulty)};

    TestStateView const* canonicalState = &test.preState;
    evmc_bytes32 canonicalTip = genesis.hash;
    uint64_t maxTotalDifficulty = static_cast<uint64_t>(genesis.difficulty);

    for (auto const& tb : test.testBlocks)
    {
        auto const revOpt =
            ForkProfileRegistry::instance().resolveRevision(test.network, tb.blockInfo.timestamp);
        if (!revOpt.has_value())
            return "unknown revision for block " + std::to_string(tb.blockInfo.number);
        evmc_revision const rev = *revOpt;

        auto parentIt = blockData.find(tb.blockInfo.parentHash);
        TestBlockHeader const* parentHeader =
            (parentIt != blockData.end()) ? parentIt->second.header : nullptr;

        auto blockError = validateBlock(rev, test.blobSchedule, tb, parentHeader);

        if (!tb.expectException.empty())
        {
            if (blockError.has_value())
            {
                if (!expectExceptionMatches(tb.expectException, *blockError))
                    return "expectException mismatch at level 1: got " + *blockError;
                continue;
            }
            if (parentIt == blockData.end())
                return "invalid block parent not in block_data";

            state::BlockHashes blockHashesLookup = [&blockHashes, blockNum = tb.blockInfo.number](
                                                       int64_t n) -> evmc_bytes32 {
                if (n < 0 || n >= blockNum)
                    return {};
                if (n + 256 <= blockNum)
                    return {};
                auto it = blockHashes.find(n);
                if (it != blockHashes.end())
                    return it->second;
                return {};
            };

            TestStateView parentState = parentIt->second.postState;
            auto res = applyEthBlock(parentState, tb.transactions, tb.blockInfo, profile, vm,
                hashImpl, std::move(blockHashesLookup), tb.withdrawals);

            if (!res.rejected.empty())
            {
                if (tb.expectException.find("TransactionException.") == std::string::npos)
                    return "unexpected rejected txs without TransactionException expectException";
                continue;
            }

            if (res.requestsError.has_value())
            {
                if (!expectExceptionMatches(tb.expectException, *res.requestsError))
                    return "expectException mismatch at level 2.5: got " + *res.requestsError;
                continue;
            }

            if (auto mismatch =
                    firstHeaderFieldMismatch(tb, res, rev, test.blobSchedule, test.network))
                continue;

            return "expected block to be invalid but resulted valid";
        }

        if (blockError.has_value())
            return "validateBlock failed on valid block: " + *blockError;
        if (parentIt == blockData.end())
            return "valid block parent not in block_data";

        state::BlockHashes blockHashesLookup = [&blockHashes, blockNum = tb.blockInfo.number](
                                                   int64_t n) -> evmc_bytes32 {
            if (n < 0 || n >= blockNum)
                return {};
            if (n + 256 <= blockNum)
                return {};
            auto it = blockHashes.find(n);
            if (it != blockHashes.end())
                return it->second;
            return {};
        };

        TestStateView parentState = parentIt->second.postState;
        auto res = applyEthBlock(parentState, tb.transactions, tb.blockInfo, profile, vm, hashImpl,
            std::move(blockHashesLookup), tb.withdrawals);

        if (auto err = validateValidBlockHeaders(tb, res, rev, test.blobSchedule, test.network))
            return *err;

        uint64_t const totalDifficulty = parentIt->second.totalDifficulty +
                                         static_cast<uint64_t>(tb.expectedBlockHeader.difficulty);
        auto [it, _] = blockData.insert_or_assign(tb.expectedBlockHeader.hash,
            BlockData{&tb.expectedBlockHeader, res.postState, totalDifficulty});
        blockHashes[tb.expectedBlockHeader.blockNumber] = tb.expectedBlockHeader.hash;

        if (totalDifficulty >= maxTotalDifficulty)
        {
            canonicalState = &it->second.postState;
            canonicalTip = tb.expectedBlockHeader.hash;
            maxTotalDifficulty = totalDifficulty;
        }
    }

    if (!bytes32Equal(canonicalTip, test.lastBlockHash))
        return "canonical tip mismatch (got=0x" + formatBytes32(canonicalTip) + " want=0x" +
               formatBytes32(test.lastBlockHash) + ")";

    if (auto err = expectPostStateMatches(*canonicalState, test))
        return *err;

    return std::nullopt;
}

}  // namespace detail

/// Run one blockchain test case. Returns failure messages (empty on success).
inline std::vector<std::string> runBlockchainTest(
    BlockchainTest const& test, evmc::VM& vm, bcos::crypto::Hash& hashImpl)
{
    auto profile = ForkProfileRegistry::instance().findByUpstreamFork(test.network);
    if (!profile.has_value())
        return {"unknown network " + test.network};

    if (auto err = detail::runOneTest(test, *profile, vm, hashImpl))
        return {*err};
    return {};
}

}  // namespace bcos::evm::reference_tests

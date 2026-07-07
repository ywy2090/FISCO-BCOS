#define BOOST_TEST_MODULE BlockValidationTest
#include "helpers/BlockValidation.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "helpers/BlockRequests.h"
#include "helpers/BlockSystemCalls.h"
#include "helpers/BlockTransition.h"
#include "helpers/BlockchainRunCore.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/included/unit_test.hpp>
#include <cstring>
#include <filesystem>
#include <map>
#include <unordered_map>

namespace bcos::evm::reference_tests
{
BOOST_AUTO_TEST_CASE(base_fee_constant_when_gas_used_equals_target)
{
    // parentGasLimit=20000000 -> target=10000000; used==target -> unchanged
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 10000000, 1000000000), 1000000000ull);
}

BOOST_AUTO_TEST_CASE(base_fee_rises_when_over_target)
{
    // used=15,000,000 target=10,000,000 base=1e9 -> +1e9*5e6/1e7/8 = +62,500,000
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 15000000, 1000000000), 1062500000ull);
}

BOOST_AUTO_TEST_CASE(rejects_missing_parent)
{
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 1;
    auto err = validateBlock(EVMC_LONDON, {}, "London", tb, /*parent*/ nullptr);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_PARENT));
}

BOOST_AUTO_TEST_CASE(rejects_non_sequential_number)
{
    TestBlockHeader parent;
    parent.blockNumber = 5;
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 7;  // must be 6
    tb.expectedBlockHeader.gasLimit = 20000000;
    tb.expectedBlockHeader.timestamp = 100;
    parent.gasLimit = 20000000;
    parent.timestamp = 50;
    auto err = validateBlock(EVMC_PARIS, {}, "Paris", tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_NUMBER));
}

BOOST_AUTO_TEST_CASE(rejects_ommers_on_paris_plus)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 1;
    tb.expectedBlockHeader.gasLimit = 20000000;
    tb.expectedBlockHeader.timestamp = 1;
    tb.expectedBlockHeader.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    tb.hasOmmers = true;
    auto err = validateBlock(EVMC_PARIS, {}, "Paris", tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INCORRECT_BLOCK_FORMAT));
}

BOOST_AUTO_TEST_CASE(excess_blob_gas_zero_below_target)
{
    // Cancun target=3 -> TARGET_BLOB_GAS = 3*131072 = 393216.
    // parentExcess=0, parentUsed=131072 (1 blob) -> sum < target -> 0
    BlobSchedule sched;  // empty -> blobParamsFor returns Cancun defaults
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, "Cancun", 131072, 0, 0), 0ull);
}

BOOST_AUTO_TEST_CASE(excess_blob_gas_accumulates_above_target)
{
    // parentUsed = 6 blobs = 786432, parentExcess=0, target gas=393216
    // -> 786432 + 0 - 393216 = 393216
    BlobSchedule sched;
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, "Cancun", 786432, 0, 0), 393216ull);
}

BOOST_AUTO_TEST_CASE(blob_gas_price_min_at_zero_excess)
{
    BlobParams p;                                        // Cancun defaults
    BOOST_CHECK_EQUAL(computeBlobGasPrice(p, 0), 1ull);  // MIN_BLOB_BASE_FEE
}

BOOST_AUTO_TEST_CASE(rejects_wrong_excess_blob_gas)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    parent.blobGasUsed = 786432;
    parent.excessBlobGas = 0;  // -> expected excess 393216
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1;
    h.gasLimit = 20000000;
    h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0;
    h.excessBlobGas = 999999;  // wrong
    tb.inputBlobGasUsed = 0;
    tb.inputExcessBlobGas = 999999;
    auto err = validateBlock(EVMC_CANCUN, {}, "Cancun", tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INCORRECT_EXCESS_BLOB_GAS));
}

BOOST_AUTO_TEST_CASE(rejects_oversized_rlp_block_on_osaka)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    parent.blobGasUsed = 0;
    parent.excessBlobGas = 0;
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1;
    h.gasLimit = 20000000;
    h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0;
    h.excessBlobGas = 0;
    tb.inputBlobGasUsed = 0;
    tb.inputExcessBlobGas = 0;
    tb.rlpSize = 9 * 1024 * 1024;  // > 8MB
    auto err = validateBlock(EVMC_OSAKA, {}, "Osaka", tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::RLP_BLOCK_LIMIT_EXCEEDED));
}

BOOST_AUTO_TEST_CASE(block_apply_result_has_extended_fields)
{
    BlockApplyResult r;
    r.bloom.resize(256, 0);
    r.rejected.push_back(0);
    r.requests.emplace_back();
    r.requestsError = std::nullopt;
    r.blobGasLeft = 0;
    TransactionReceipt rc;
    rc.bloom.resize(256, 0);
    rc.logs.clear();
    BOOST_CHECK_EQUAL(r.bloom.size(), 256u);
}

BOOST_AUTO_TEST_CASE(prague_block_end_system_call_revert_fails_requests)
{
    auto profile = ForkProfileRegistry::instance().findByUpstreamFork("Prague");
    BOOST_REQUIRE(profile.has_value());

    TestStateView state;
    state::Account revertAcc;
    revertAcc.code = bcos::fromHex("60006000fd");
    state.insertAccount(kWithdrawalRequestAddress, revertAcc);
    state::Account okAcc;
    okAcc.code = bcos::fromHex("00");
    state.insertAccount(kConsolidationRequestAddress, okAcc);

    state::BlockInfo blockInfo{};
    blockInfo.gasLimit = 30'000'000;
    blockInfo.number = 1;
    evmc::VM vm{evmc_create_evmone()};
    bcos::crypto::Keccak256 hashImpl;

    auto result = collectPragueBlockRequests(
        state, blockInfo, profile->revision, vm, hashImpl, {}, std::vector<TransactionReceipt>{});
    BOOST_REQUIRE(result.error.has_value());
    BOOST_CHECK_EQUAL(*result.error, std::string(kSystemContractCallFailed));
}

BOOST_AUTO_TEST_CASE(prague_withdrawal_eoa_tx_only_storage)
{
#ifdef SPECS_TESTS_EEST_ROOT
    namespace fs = std::filesystem;
    namespace pt = boost::property_tree;
    auto const path = fs::path(SPECS_TESTS_EEST_ROOT) /
                      "fixtures/blockchain_tests/prague/eip7002_el_triggerable_withdrawals/"
                      "test_eip_7002.json";
    pt::ptree root;
    pt::read_json(path.string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& test = tests.front();
    auto const genesisRoot = detail::computeStateRootFromView(test.preState);
    BOOST_CHECK_MESSAGE(detail::bytes32Equal(genesisRoot, test.genesisBlockHeader.stateRoot),
        "genesis stateRoot self-test mismatch");
    auto profile = ForkProfileRegistry::instance().findByUpstreamFork(test.network);
    BOOST_REQUIRE(profile.has_value());
    evmc::VM vm{evmc_create_evmone()};
    bcos::crypto::Keccak256 hashImpl;

    auto const& tb = test.testBlocks.front();
    TestStateView state = test.preState;
    auto const execBlockInfo =
        blockInfoForExecution(tb.blockInfo, tb, &test.genesisBlockHeader, EVMC_PRAGUE);

    state::BlockHashes blockHashesLookup = [&test](int64_t n) -> evmc_bytes32 {
        if (n == 0)
            return test.genesisBlockHeader.hash;
        return {};
    };

    mergeStateDiffIntoView(state, applyCancunBlockSystemCalls(state, execBlockInfo,
                                      profile->revision, vm, hashImpl, blockHashesLookup));
    mergeStateDiffIntoView(state, applyPragueBlockSystemCalls(state, execBlockInfo,
                                      profile->revision, vm, hashImpl, blockHashesLookup));

    evmc_address const beaconAddr = kBeaconRootsAddress;
    auto const beaconAfterSys = state.get_account(beaconAddr);
    BOOST_REQUIRE(beaconAfterSys.has_value());
    evmc_bytes32 slot1Key{};
    slot1Key.bytes[31] = 1;
    auto const slot1It = beaconAfterSys->storage.find(slot1Key);
    evmc_bytes32 slot1Val{};
    if (slot1It != beaconAfterSys->storage.end())
        slot1Val = slot1It->second;
    evmc_bytes32 wantSlot1{};
    wantSlot1.bytes[31] = 1;
    BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(slot1Val, wantSlot1),
        "beacon slot1 after sys calls got=0x"
            << bcos::toHex(bcos::bytes(slot1Val.bytes, slot1Val.bytes + 32)));

    StateTestCase tc;
    tc.env = execBlockInfo;
    tc.transaction = tb.transactions.front();
    tc.tx = materializeTransaction(tc.transaction);
    for (auto const& [addr, acc] : state.accounts())
        tc.preState.emplace_back(addr, acc);

    StateSubtest st{
        .fork = profile->upstreamForkName, .dataIndex = 0, .gasIndex = 0, .valueIndex = 0};
    EthMessageAdapter adapter(*profile, hashImpl, vm);
    adapter.setBlockHashes(blockHashesLookup);
    auto execResult = task::syncWait(adapter.execute(tc, st));
    BOOST_REQUIRE_EQUAL(execResult.status, EVMC_SUCCESS);
    BOOST_REQUIRE(execResult.stateRoot.has_value());

    auto const postView =
        buildPostStateView(tc.preState, execResult.stateDiff, true, execBlockInfo.coinbase, true);
    evmc_address const withdrawalAddr = kWithdrawalRequestAddress;
    state::Account const* gotAcc = nullptr;
    for (auto const& [addr, acc] : postView.accounts)
    {
        if (state::AddressEqual{}(addr, withdrawalAddr))
            gotAcc = &acc;
    }
    BOOST_REQUIRE(gotAcc != nullptr);

    std::map<std::string, evmc_bytes32> expectedSlots;
    for (auto const& [addr, acc] : test.postState)
    {
        if (!state::AddressEqual{}(addr, withdrawalAddr))
            continue;
        for (auto const& [slot, val] : acc.storage)
            expectedSlots[bcos::toHex(bcos::bytes(slot.bytes, slot.bytes + 32))] = val;
    }

    for (auto const& [slot, val] : gotAcc->storage)
    {
        auto const slotHex = bcos::toHex(bcos::bytes(slot.bytes, slot.bytes + 32));
        auto const valHex = bcos::toHex(bcos::bytes(val.bytes, val.bytes + 32));
        auto it = expectedSlots.find(slotHex);
        if (it != expectedSlots.end())
            BOOST_CHECK_MESSAGE(
                state::Bytes32Equal{}(val, it->second), "slot 0x" << slotHex << " got=" << valHex);
    }
    for (auto const& [slotHex, want] : expectedSlots)
    {
        evmc_bytes32 slotKey{};
        auto const slotBytes = bcos::fromHex(slotHex);
        std::memcpy(slotKey.bytes, slotBytes.data(), std::min(slotBytes.size(), size_t{32}));
        auto it = gotAcc->storage.find(slotKey);
        BOOST_CHECK_MESSAGE(it != gotAcc->storage.end(), "missing expected slot 0x" << slotHex);
    }

    // Block-end request collection should not corrupt withdrawal storage.
    TestStateView postTxState;
    for (auto const& [addr, acc] : postView.accounts)
        postTxState.insertAccount(addr, acc);
    TransactionReceipt receipt;
    receipt.logs = execResult.logs;
    auto reqCollect = collectPragueBlockRequests(postTxState, execBlockInfo, profile->revision, vm,
        hashImpl, blockHashesLookup, std::vector<TransactionReceipt>{receipt});
    BOOST_REQUIRE(!reqCollect.error.has_value());

    std::vector<std::pair<evmc_address, state::Account>> pairs;
    for (auto const& [addr, acc] : postView.accounts)
        pairs.emplace_back(addr, acc);
    mergeStateDiffIntoPairs(pairs, reqCollect.stateDiff);

    state::Account const* afterEnd = nullptr;
    for (auto const& [addr, acc] : pairs)
    {
        if (state::AddressEqual{}(addr, withdrawalAddr))
            afterEnd = &acc;
    }
    BOOST_REQUIRE(afterEnd != nullptr);
    for (auto const& [slotHex, want] : expectedSlots)
    {
        evmc_bytes32 slotKey{};
        auto const slotBytes = bcos::fromHex(slotHex);
        std::memcpy(slotKey.bytes, slotBytes.data(), std::min(slotBytes.size(), size_t{32}));
        auto it = afterEnd->storage.find(slotKey);
        BOOST_REQUIRE(it != afterEnd->storage.end());
        BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(it->second, want),
            "after block-end slot 0x" << slotHex << " corrupted");
    }

    // applyEthBlock-style preStatePairs merge must match buildPostStateView(applyDiff=true).
    std::vector<std::pair<evmc_address, state::Account>> preStatePairs;
    for (auto const& [addr, acc] : state.accounts())
        preStatePairs.emplace_back(addr, acc);
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
    preStatePairs.erase(std::remove_if(preStatePairs.begin(), preStatePairs.end(),
                            [](auto const& entry) {
                                auto const& acc = entry.second;
                                return acc.nonce == 0 && acc.balance == 0 && acc.code.empty();
                            }),
        preStatePairs.end());

    state::Account const* pairsAcc = nullptr;
    for (auto const& [addr, acc] : preStatePairs)
    {
        if (state::AddressEqual{}(addr, withdrawalAddr))
            pairsAcc = &acc;
    }
    BOOST_REQUIRE(pairsAcc != nullptr);
    for (auto const& [slotHex, want] : expectedSlots)
    {
        evmc_bytes32 slotKey{};
        auto const slotBytes = bcos::fromHex(slotHex);
        std::memcpy(slotKey.bytes, slotBytes.data(), std::min(slotBytes.size(), size_t{32}));
        auto it = pairsAcc->storage.find(slotKey);
        BOOST_REQUIRE(it != pairsAcc->storage.end());
        BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(it->second, want),
            "preStatePairs slot 0x" << slotHex << " mismatch");
    }

    state::StateDiff emptyDiff;
    auto rootFromView = [&](GstPostStateView const& v) { return computeStateRoot(v); };
    auto const blockPostView =
        buildPostStateView(preStatePairs, emptyDiff, false, execBlockInfo.coinbase, true);
    BOOST_CHECK_MESSAGE(
        detail::bytes32Equal(rootFromView(postView), tb.expectedBlockHeader.stateRoot),
        "tx-only postView stateRoot mismatch");
    evmc_address const coinbaseAddr = execBlockInfo.coinbase;
    evmc_address const senderAddr = tc.tx.from;
    for (auto const& [addr, acc] : postView.accounts)
    {
        if (state::AddressEqual{}(addr, coinbaseAddr))
        {
            for (auto const& [expAddr, expAcc] : test.postState)
            {
                if (state::AddressEqual{}(addr, expAddr))
                    BOOST_CHECK_MESSAGE(acc.balance == expAcc.balance,
                        "coinbase balance got=0x" << acc.balance.str(0, std::ios::hex) << " want=0x"
                                                  << expAcc.balance.str(0, std::ios::hex));
            }
        }
        if (state::AddressEqual{}(addr, senderAddr))
        {
            for (auto const& [expAddr, expAcc] : test.postState)
            {
                if (state::AddressEqual{}(addr, expAddr))
                {
                    BOOST_CHECK_MESSAGE(acc.nonce == expAcc.nonce, "sender nonce mismatch");
                    BOOST_CHECK_MESSAGE(acc.balance == expAcc.balance,
                        "sender balance got=0x" << acc.balance.str(0, std::ios::hex) << " want=0x"
                                                << expAcc.balance.str(0, std::ios::hex));
                }
            }
        }
    }
    BOOST_CHECK_MESSAGE(
        detail::bytes32Equal(rootFromView(blockPostView), tb.expectedBlockHeader.stateRoot),
        "blockPostView (no block-end) stateRoot mismatch");

    std::vector<std::pair<evmc_address, state::Account>> withBlockEndPairs;
    for (auto const& [addr, acc] : postView.accounts)
        withBlockEndPairs.emplace_back(addr, acc);
    mergeStateDiffIntoPairs(withBlockEndPairs, reqCollect.stateDiff);
    auto const withBlockEndView =
        buildPostStateView(withBlockEndPairs, emptyDiff, false, execBlockInfo.coinbase, true);
    BOOST_CHECK_MESSAGE(
        detail::bytes32Equal(rootFromView(withBlockEndView), tb.expectedBlockHeader.stateRoot),
        "with block-end merge stateRoot mismatch");

    state::Account const* blockPostAcc = nullptr;
    for (auto const& [addr, acc] : blockPostView.accounts)
    {
        if (state::AddressEqual{}(addr, withdrawalAddr))
            blockPostAcc = &acc;
    }
    BOOST_REQUIRE(blockPostAcc != nullptr);
    for (auto const& [slotHex, want] : expectedSlots)
    {
        evmc_bytes32 slotKey{};
        auto const slotBytes = bcos::fromHex(slotHex);
        std::memcpy(slotKey.bytes, slotBytes.data(), std::min(slotBytes.size(), size_t{32}));
        auto it = blockPostAcc->storage.find(slotKey);
        BOOST_REQUIRE(it != blockPostAcc->storage.end());
        BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(it->second, want),
            "buildPostStateView(false) slot 0x" << slotHex << " mismatch");
    }

    TestStateView chain = test.preState;
    auto const full = applyEthBlock(chain, tb.transactions, execBlockInfo, *profile, vm, hashImpl,
        blockHashesLookup, tb.withdrawals);
    auto const fullAcc = full.postState.get_account(withdrawalAddr);
    BOOST_REQUIRE(fullAcc.has_value());
    for (auto const& [slotHex, want] : expectedSlots)
    {
        evmc_bytes32 slotKey{};
        auto const slotBytes = bcos::fromHex(slotHex);
        std::memcpy(slotKey.bytes, slotBytes.data(), std::min(slotBytes.size(), size_t{32}));
        auto it = fullAcc->storage.find(slotKey);
        BOOST_REQUIRE(it != fullAcc->storage.end());
        BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(it->second, want),
            "applyEthBlock slot 0x" << slotHex << " mismatch");
    }

    evmc_address const historyAddr = kHistoryStorageAddress;
    auto const historyGot = full.postState.get_account(historyAddr);
    BOOST_REQUIRE(historyGot.has_value());
    evmc_bytes32 slot0{};
    state::Account const* histExp = nullptr;
    for (auto const& [addr, acc] : test.postState)
    {
        if (state::AddressEqual{}(addr, historyAddr))
            histExp = &acc;
    }
    BOOST_REQUIRE(histExp != nullptr);
    auto expSlot0It = histExp->storage.find(slot0);
    BOOST_REQUIRE(expSlot0It != histExp->storage.end());
    auto gotSlot0It = historyGot->storage.find(slot0);
    evmc_bytes32 got0{};
    if (gotSlot0It != historyGot->storage.end())
        got0 = gotSlot0It->second;
    BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(got0, expSlot0It->second), "history slot 0 mismatch");
    evmc_bytes32 slot1{};
    slot1.bytes[31] = 1;
    auto histSlot1It = historyGot->storage.find(slot1);
    BOOST_CHECK_MESSAGE(
        histSlot1It == historyGot->storage.end(), "history slot 1 present after block 1");

    evmc_address const beaconAddrCheck = kBeaconRootsAddress;
    auto const beaconGot = full.postState.get_account(beaconAddrCheck);
    BOOST_REQUIRE(beaconGot.has_value());
    evmc_bytes32 beaconSlot1{};
    beaconSlot1.bytes[31] = 1;
    auto b1It = beaconGot->storage.find(beaconSlot1);
    evmc_bytes32 b1Got{};
    if (b1It != beaconGot->storage.end())
        b1Got = b1It->second;
    BOOST_CHECK_MESSAGE(state::Bytes32Equal{}(b1Got, wantSlot1), "beacon slot1 after block 1");
    evmc_bytes32 beaconSlot2{};
    beaconSlot2.bytes[31] = 2;
    auto b2It = beaconGot->storage.find(beaconSlot2);
    BOOST_CHECK_MESSAGE(b2It == beaconGot->storage.end() || state::isZeroBytes32(b2It->second),
        "beacon slot2 should be absent/zero after block 1");

    BOOST_CHECK_MESSAGE(fullAcc->balance == 1, "withdrawal balance want 1 wei");

    size_t const accountCount = [&] {
        size_t n = 0;
        for (auto const& _ : full.postState.accounts())
            ++n;
        return n;
    }();
    BOOST_CHECK_MESSAGE(accountCount == 7, "unexpected postState account count");

    for (auto const& [addr, preAcc] : test.preState.accounts())
    {
        if (state::AddressEqual{}(addr, senderAddr) ||
            state::AddressEqual{}(addr, withdrawalAddr) ||
            state::AddressEqual{}(addr, historyAddr) ||
            state::AddressEqual{}(addr, beaconAddrCheck))
            continue;
        auto got = full.postState.get_account(addr);
        BOOST_REQUIRE_MESSAGE(got.has_value(), "missing unchanged pre account");
        BOOST_CHECK_MESSAGE(got->nonce == preAcc.nonce && got->balance == preAcc.balance &&
                                got->code == preAcc.code,
            "pre account mutated unexpectedly");
        for (auto const& [slot, wantVal] : preAcc.storage)
        {
            auto it = got->storage.find(slot);
            evmc_bytes32 gotVal{};
            if (it != got->storage.end())
                gotVal = it->second;
            BOOST_CHECK_MESSAGE(
                state::Bytes32Equal{}(gotVal, wantVal), "pre account storage drift");
        }
    }

    auto const computedRoot = detail::computeStateRootFromView(full.postState);
    BOOST_CHECK_MESSAGE(detail::bytes32Equal(computedRoot, tb.expectedBlockHeader.stateRoot),
        "applyEthBlock stateRoot mismatch got="
            << bcos::toHex(bcos::bytes(computedRoot.bytes, computedRoot.bytes + 32)) << " want="
            << bcos::toHex(bcos::bytes(tb.expectedBlockHeader.stateRoot.bytes,
                   tb.expectedBlockHeader.stateRoot.bytes + 32)));
#endif
}

BOOST_AUTO_TEST_CASE(prague_withdrawal_eoa_header_fields)
{
#ifdef SPECS_TESTS_EEST_ROOT
    namespace fs = std::filesystem;
    namespace pt = boost::property_tree;
    auto const path = fs::path(SPECS_TESTS_EEST_ROOT) /
                      "fixtures/blockchain_tests/prague/eip7002_el_triggerable_withdrawals/"
                      "test_eip_7002.json";
    pt::ptree root;
    pt::read_json(path.string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& test = tests.front();
    auto profile = ForkProfileRegistry::instance().findByUpstreamFork(test.network);
    BOOST_REQUIRE(profile.has_value());
    evmc::VM vm{evmc_create_evmone()};
    bcos::crypto::Keccak256 hashImpl;

    std::unordered_map<int64_t, evmc_bytes32> blockHashes;
    blockHashes[0] = test.genesisBlockHeader.hash;
    TestStateView chainState = test.preState;
    TestBlockHeader const* parentHeader = &test.genesisBlockHeader;

    for (auto const& tb : test.testBlocks)
    {
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

        auto const execBlockInfo =
            blockInfoForExecution(tb.blockInfo, tb, parentHeader, EVMC_PRAGUE);
        auto res = applyEthBlock(chainState, tb.transactions, execBlockInfo, *profile, vm, hashImpl,
            std::move(blockHashesLookup), tb.withdrawals);

        auto const& h = tb.expectedBlockHeader;
        BOOST_CHECK_MESSAGE(
            detail::bytes32Equal(detail::computeStateRootFromView(res.postState), h.stateRoot),
            "block " << h.blockNumber << " stateRoot mismatch");
        BOOST_CHECK(detail::bytes32Equal(computeRequestsHash(res.requests), h.requestsHash));
        BOOST_CHECK(detail::bytes32Equal(
            computeReceiptsRoot(detail::receiptsForRoot(res.receipts)), h.receiptsRoot));
        BOOST_CHECK_EQUAL(res.gasUsed, h.gasUsed);

        chainState = res.postState;
        blockHashes[h.blockNumber] = h.hash;
        parentHeader = &tb.expectedBlockHeader;
    }
#endif
}

BOOST_AUTO_TEST_CASE(eip7685_multi_type_requests_header_fields)
{
#ifdef SPECS_TESTS_EEST_ROOT
    namespace fs = std::filesystem;
    namespace pt = boost::property_tree;
    auto const path = fs::path(SPECS_TESTS_EEST_ROOT) /
                      "fixtures/blockchain_tests/prague/eip7685_general_purpose_el_requests/"
                      "test_valid_multi_type_requests.json";
    pt::ptree root;
    pt::read_json(path.string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());

    std::string const wantSuffix =
        "consolidation_from_contract+deposit_from_contract+withdrawal_from_contract";
    BlockchainTest const* picked = nullptr;
    for (auto const& t : tests)
    {
        if (t.network == "Prague" && t.name.find(wantSuffix) != std::string::npos)
        {
            picked = &t;
            break;
        }
    }
    BOOST_REQUIRE(picked != nullptr);

    auto profile = ForkProfileRegistry::instance().findByUpstreamFork(picked->network);
    BOOST_REQUIRE(profile.has_value());
    evmc::VM vm{evmc_create_evmone()};
    bcos::crypto::Keccak256 hashImpl;

    auto const& tb = picked->testBlocks.front();
    std::unordered_map<int64_t, evmc_bytes32> blockHashes;
    blockHashes[0] = picked->genesisBlockHeader.hash;
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

    TestStateView chain = picked->preState;
    auto const execBlockInfo = blockInfoForExecution(
        tb.blockInfo, tb, &picked->genesisBlockHeader, profile->revision.revision);

    auto res = applyEthBlock(chain, tb.transactions, execBlockInfo, *profile, vm, hashImpl,
        blockHashesLookup, tb.withdrawals);

    auto const& h = tb.expectedBlockHeader;
    BOOST_CHECK_MESSAGE(res.rejected.empty(), "rejected tx count=" << res.rejected.size());
    BOOST_CHECK_MESSAGE(!res.requestsError.has_value(),
        "requests error: " << (res.requestsError ? *res.requestsError : ""));
    BOOST_CHECK_EQUAL(res.requests.size(), 3u);
    BOOST_CHECK_MESSAGE(
        res.gasUsed == h.gasUsed, "gasUsed got=" << res.gasUsed << " want=" << h.gasUsed);
    BOOST_CHECK_MESSAGE(detail::bytes32Equal(computeRequestsHash(res.requests), h.requestsHash),
        "requestsHash mismatch");
    BOOST_CHECK_MESSAGE(
        detail::bytes32Equal(
            computeReceiptsRoot(detail::receiptsForRoot(res.receipts)), h.receiptsRoot),
        "receiptsRoot mismatch");

    if (auto postErr = detail::expectPostStateMatches(res.postState, *picked))
        BOOST_FAIL("postState field mismatch: " + *postErr);

    TestStateView fixturePost;
    for (auto const& [addr, acc] : picked->postState)
        fixturePost.insertAccount(addr, acc);
    auto const fixtureRoot = detail::computeStateRootFromView(fixturePost);
    BOOST_CHECK_MESSAGE(detail::bytes32Equal(fixtureRoot, h.stateRoot),
        "fixture postState does not hash to header stateRoot got=0x"
            << detail::formatBytes32(fixtureRoot) << " want=0x"
            << detail::formatBytes32(h.stateRoot));

    for (auto const& [expAddr, expAcc] : picked->postState)
    {
        auto got = res.postState.get_account(expAddr);
        BOOST_REQUIRE(got.has_value());
        if (got->code != expAcc.code)
            BOOST_FAIL("code mismatch on 0x" +
                       bcos::toHex(bcos::bytes(expAddr.bytes, expAddr.bytes + 20)));
        for (auto const& [slot, val] : got->storage)
        {
            auto it = expAcc.storage.find(slot);
            evmc_bytes32 want{};
            if (it != expAcc.storage.end())
                want = it->second;
            if (!state::Bytes32Equal{}(val, want))
            {
                BOOST_FAIL("storage slot mismatch on 0x" +
                           bcos::toHex(bcos::bytes(expAddr.bytes, expAddr.bytes + 20)) +
                           " slot=0x" + bcos::toHex(bcos::bytes(slot.bytes, slot.bytes + 32)) +
                           " got=0x" + bcos::toHex(bcos::bytes(val.bytes, val.bytes + 32)) +
                           " want=0x" + bcos::toHex(bcos::bytes(want.bytes, want.bytes + 32)));
            }
        }
        for (auto const& [slot, want] : expAcc.storage)
        {
            auto it = got->storage.find(slot);
            if (it == got->storage.end())
                BOOST_FAIL("missing expected storage slot");
        }
    }

    std::vector<std::pair<evmc_address, state::Account>> pairs;
    for (auto const& [addr, acc] : res.postState.accounts())
        pairs.emplace_back(addr, acc);
    state::StateDiff emptyDiff;
    auto const gstView = buildPostStateView(pairs, emptyDiff, false, execBlockInfo.coinbase, true);
    auto const rootFromGst = computeStateRoot(gstView);
    auto const rootFromView = detail::computeStateRootFromView(res.postState);
    BOOST_CHECK_MESSAGE(
        detail::bytes32Equal(rootFromGst, rootFromView), "gst vs view stateRoot path mismatch");

    auto const computedRoot = rootFromView;
    size_t accountCount = 0;
    for (auto const& _ : res.postState.accounts())
        ++accountCount;
    BOOST_CHECK_MESSAGE(accountCount > 0, "empty postState");
    BOOST_CHECK_MESSAGE(detail::bytes32Equal(computedRoot, h.stateRoot),
        "stateRoot mismatch got=0x" << detail::formatBytes32(computedRoot) << " want=0x"
                                    << detail::formatBytes32(h.stateRoot)
                                    << " accounts=" << accountCount);
#endif
}
}  // namespace bcos::evm::reference_tests

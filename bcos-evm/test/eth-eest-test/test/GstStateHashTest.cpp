#define BOOST_TEST_MODULE GstStateHashTest
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(empty_logs_hash_matches_geth)
{
    evmc_bytes32 const expected = {0x1d, 0xcc, 0x4d, 0xe8, 0xde, 0xc7, 0x5d, 0x7a, 0xab, 0x85, 0xb5,
        0x67, 0xb6, 0xcc, 0xd4, 0x1a, 0xd3, 0x12, 0x45, 0x1b, 0x94, 0x8a, 0x74, 0x13, 0xf0, 0xa1,
        0x42, 0xfd, 0x40, 0xd4, 0x93, 0x47};
    auto const actual = computeLogsHash({});
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(self_balance_post_state_root_from_fixture)
{
    GstPostStateView postState;
    state::Account contract;
    contract.balance = 500;
    contract.code = bcos::fromHex("4760015500");
    evmc_bytes32 slot{};
    slot.bytes[31] = 1;
    evmc_bytes32 value{};
    value.bytes[30] = 0x01;
    value.bytes[31] = 0xf4;
    contract.storage.emplace(slot, value);

    state::Account sender;
    sender.balance = bcos::fromBigQuantity("0x3635c9adc5de996c18");
    sender.nonce = 1;

    postState.accounts.emplace_back(
        state::parseHexAddress("0x1000000000000000000000000000000000000000"), contract);
    postState.accounts.emplace_back(
        state::parseHexAddress("0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b"), sender);

    evmc_bytes32 const expected = {0x2e, 0x75, 0x86, 0xff, 0x21, 0xd9, 0xf2, 0x06, 0xe4, 0x2a, 0x10,
        0x6e, 0x14, 0x14, 0x95, 0xb7, 0x49, 0x9f, 0x0a, 0x4b, 0x55, 0x50, 0x16, 0xd0, 0x35, 0xa9,
        0x64, 0x15, 0xb3, 0xf7, 0x6b, 0x14};
    auto const actual = computeStateRoot(postState);
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(pre_eip158_rejected_tx_omits_untouched_coinbase)
{
    std::vector<std::pair<evmc_address, state::Account>> preState;
    state::StateDiff diff;
    auto const coinbase = state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");

    auto const postState = buildPostStateView(preState, diff, false, coinbase, false);
    BOOST_CHECK_EQUAL(postState.accounts.size(), 0u);

    evmc_bytes32 const expected = {0xe1, 0xba, 0x68, 0xaa, 0xb2, 0x77, 0xd1, 0xbc, 0x2d, 0xbd, 0xd5,
        0x57, 0x3f, 0xf1, 0x52, 0x4e, 0xcf, 0x1e, 0x13, 0x2f, 0x6a, 0x52, 0xa7, 0xc6, 0x54, 0x1c,
        0xeb, 0x82, 0x5b, 0x7d, 0x08, 0x37};
    auto const actual = computeStateRoot(postState);
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(pre_eip158_coinbase_touch_included_in_post_state)
{
    std::vector<std::pair<evmc_address, state::Account>> preState;
    state::StateDiff diff;
    auto const sender = state::parseHexAddress("0xcc6872e307074e29d88504ef9cc7cc22c0310bba");
    auto const contract = state::parseHexAddress("0x30e2c29a12092a510c50bcdba5d7b2de7c8bc840");
    auto const coinbase = state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");

    state::Account senderAccount;
    senderAccount.nonce = 1;
    senderAccount.balance = bcos::fromBigQuantity("0x3635c9adc5dea00000");
    diff.accounts.emplace(sender, senderAccount);

    state::Account contractAccount;
    contractAccount.nonce = 1;
    contractAccount.code = bcos::fromHex("600160005500");
    evmc_bytes32 slot{};
    evmc_bytes32 value{};
    value.bytes[31] = 0x01;
    contractAccount.storage.emplace(slot, value);
    diff.accounts.emplace(contract, contractAccount);

    auto const postState = buildPostStateView(preState, diff, true, coinbase, false);
    BOOST_REQUIRE_EQUAL(postState.accounts.size(), 3u);
    BOOST_REQUIRE_EQUAL(postState.eip158ClearEmpty, false);

    evmc_bytes32 const expected = {0x86, 0xd5, 0xed, 0x0d, 0xe0, 0x1c, 0xab, 0x26, 0x22, 0xb7, 0xd4,
        0x41, 0xcc, 0x36, 0x62, 0xd5, 0x28, 0x9a, 0x4f, 0x00, 0x8d, 0xe0, 0x58, 0x5f, 0x1d, 0xef,
        0x99, 0xeb, 0xe7, 0xdf, 0xf4, 0x79};
    auto const actual = computeStateRoot(postState);
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);

    auto const eip158PostState = buildPostStateView(preState, diff, true, coinbase, true);
    BOOST_CHECK_EQUAL(eip158PostState.accounts.size(), 2u);
    evmc_bytes32 const eip158Expected = {0xe4, 0x75, 0xc3, 0xef, 0x3a, 0xc0, 0x0e, 0x3f, 0x08, 0x0a,
        0x34, 0x0c, 0xcb, 0x9c, 0xca, 0x46, 0x1f, 0x0d, 0xcf, 0xf5, 0x76, 0xc0, 0x14, 0xf8, 0xc3,
        0x9b, 0x64, 0x36, 0x7c, 0x7d, 0x33, 0x96};
    auto const eip158Actual = computeStateRoot(eip158PostState);
    BOOST_CHECK_EQUAL(std::memcmp(eip158Actual.bytes, eip158Expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(build_post_state_view_removes_deleted_prestate_accounts)
{
    auto const deleted = state::parseHexAddress("0x1ff7e948c4172cea98805e48478badaec68bcb43");
    auto const sender = state::parseHexAddress("0x8c0107aef7f9da541bb4f7b2d33c21f9a8151d63");
    auto const coinbase = state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");

    std::vector<std::pair<evmc_address, state::Account>> preState;
    state::Account deletedPre;
    deletedPre.balance = 100000;
    preState.emplace_back(deleted, deletedPre);

    state::StateDiff diff;
    diff.deletedAccounts.insert(deleted);
    state::Account senderPatch;
    senderPatch.nonce = 1;
    senderPatch.balanceDirty = true;
    senderPatch.nonceDirty = true;
    diff.accounts.emplace(sender, senderPatch);

    auto const postState = buildPostStateView(preState, diff, true, coinbase, true);
    for (auto const& [address, account] : postState.accounts)
    {
        (void)account;
        BOOST_CHECK(!state::AddressEqual{}(address, deleted));
    }
}

BOOST_AUTO_TEST_CASE(add11_post_state_root_from_fixture)
{
    GstPostStateView postState;

    state::Account contract;
    contract.balance = bcos::fromBigQuantity("0x0de0b6b3a76586a0");
    contract.code = bcos::fromHex("600160010160005500");
    evmc_bytes32 slot{};
    evmc_bytes32 value{};
    value.bytes[31] = 0x02;
    contract.storage.emplace(slot, value);

    state::Account coinbase;
    coinbase.nonce = 1;

    state::Account sender;
    sender.balance = bcos::fromBigQuantity("0x0de0b6b3a75be550");
    sender.nonce = 1;

    postState.accounts.emplace_back(
        state::parseHexAddress("0x095e7baea6a6c7c4c2dfeb977efac326af552d87"), contract);
    postState.accounts.emplace_back(
        state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba"), coinbase);
    postState.accounts.emplace_back(
        state::parseHexAddress("0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b"), sender);

    evmc_bytes32 const expected = {0xe8, 0x01, 0x0c, 0xe5, 0x90, 0xf4, 0x01, 0xc9, 0xd6, 0x1f, 0xef,
        0x8a, 0xb0, 0x5b, 0xea, 0x9b, 0xce, 0xc2, 0x42, 0x81, 0xb7, 0x95, 0xe5, 0x86, 0x88, 0x09,
        0xbc, 0x4e, 0x51, 0x5a, 0xa5, 0x30};
    auto const actual = computeStateRoot(postState);
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(empty_tx_root_is_empty_mpt)
{
    auto const r = computeTxRoot({});
    // EMPTY_MPT_HASH = keccak256(RLP("")) = 0x56e81f...b421
    evmc_bytes32 const empty = {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45,
        0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0, 0x01, 0x62,
        0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21};
    BOOST_CHECK_EQUAL(std::memcmp(r.bytes, empty.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(single_withdrawal_root_is_deterministic)
{
    Withdrawal w;
    w.index = 0;
    w.validatorIndex = 0;
    w.amount = 0x5209;
    auto const r = computeWithdrawalRoot(std::span<const Withdrawal>(&w, 1));
    // Non-empty, and stable across runs.
    BOOST_CHECK(std::memcmp(r.bytes, decltype(r){}.bytes, 32) != 0);
    BOOST_CHECK_EQUAL(
        std::memcmp(r.bytes, computeWithdrawalRoot(std::span<const Withdrawal>(&w, 1)).bytes, 32),
        0);
}

BOOST_AUTO_TEST_CASE(empty_requests_hash_is_sha256_empty)
{
    // EIP-7685: no requests → sha256("")
    evmc_bytes32 const expected = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
        0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95,
        0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    auto const actual = computeRequestsHash({});
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(type_only_request_excluded_from_requests_hash)
{
    evmc_bytes32 const emptyHash = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb,
        0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4,
        0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    bcos::bytes typeOnly{0x01};
    auto const actual = computeRequestsHash(std::span<const bcos::bytes>(&typeOnly, 1));
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, emptyHash.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(single_request_hash_is_deterministic)
{
    bcos::bytes request{0x01, 0x02};
    evmc_bytes32 const expected = {0x76, 0xa5, 0x6a, 0xce, 0xd9, 0x15, 0xd2, 0x51, 0x3d, 0xcd, 0x84,
        0xc2, 0xc3, 0x78, 0xb2, 0xe8, 0xaa, 0x5c, 0xd6, 0x32, 0xb5, 0xb7, 0x1c, 0xa2, 0xf2, 0xac,
        0x5b, 0x0e, 0x3a, 0x64, 0x9b, 0xdb};
    auto const actual = computeRequestsHash(std::span<const bcos::bytes>(&request, 1));
    BOOST_CHECK_EQUAL(std::memcmp(actual.bytes, expected.bytes, 32), 0);
}

}  // namespace bcos::evm::reference_tests

#define BOOST_TEST_MODULE GstStateHashTest
#include "bcos-evm/evm-reference-tests/GstStateHash.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <boost/test/included/unit_test.hpp>

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

}  // namespace bcos::evm::reference_tests

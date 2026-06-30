/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EthHost BLOCKHASH via BlockHashes lambda and BLOCKHASH opcode.
 */

#define BOOST_TEST_MODULE BlockHashHostTest

#include "bcos-evm/eth/host/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addressFromByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::bytes hexBytes(std::string_view hex)
{
    return bcos::fromHex(hex);
}

evmc_bytes32 storageKeyZero()
{
    return evmc_bytes32{};
}

// PUSH1 99 BLOCKHASH PUSH1 0 SSTORE STOP
constexpr std::string_view kBlockHashBytecode = "60634060005500";
}  // namespace

BOOST_AUTO_TEST_SUITE(BlockHashHostTest)

BOOST_AUTO_TEST_CASE(blockhash_opcode_returns_lambda_hash)
{
    constexpr int64_t kCurrentBlock = 100;
    constexpr int64_t kQueryBlock = 99;

    InMemoryStateView view;

    auto const contract = addressFromByte(0x01);
    auto const sender = addressFromByte(0xaa);

    Account contractAccount;
    contractAccount.code = hexBytes(kBlockHashBytecode);
    view.insert_account(contract, contractAccount);

    State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = sender;
    txContext.block_number = kCurrentBlock;
    txContext.block_gas_limit = 30'000'000;

    int blockHashCalls = 0;
    int64_t queriedBlock = -1;
    BlockHashes blockHashes = [&](int64_t number) {
        ++blockHashCalls;
        queriedBlock = number;
        evmc_bytes32 hash{};
        if (number == kQueryBlock)
        {
            hash.bytes[31] = 0xab;
            hash.bytes[30] = 0xcd;
        }
        return hash;
    };

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .eip2929 = true};
    EthHost host(state, txContext, cfg, vm, blockHashes);

    evmc_bytes32 expected{};
    expected.bytes[31] = 0xab;
    expected.bytes[30] = 0xcd;
    BOOST_CHECK(std::memcmp(host.get_block_hash(kQueryBlock).bytes, expected.bytes, 32) == 0);
    blockHashCalls = 0;

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = sender;
    msg.recipient = contract;
    msg.code_address = contract;
    msg.value = {};

    auto const result = vm.execute(
        host, EVMC_CANCUN, msg, contractAccount.code.data(), contractAccount.code.size());

    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(blockHashCalls, 1);
    BOOST_CHECK_EQUAL(queriedBlock, kQueryBlock);

    auto const stored = state.get_storage(contract, storageKeyZero());
    BOOST_CHECK(std::memcmp(stored.bytes, expected.bytes, 32) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

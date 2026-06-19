/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TDD RED: nested CALL via EthHost::call() stub (Task 3).
 */

#define BOOST_TEST_MODULE NestedCallHostTest

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <array>
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

BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

bcos::bytes hexBytes(std::string_view hex)
{
    return bcos::fromHex(hex);
}

bool outputContainsByte(bcos::bytes const& output, uint8_t byte)
{
    return std::any_of(
        output.begin(), output.end(), [byte](uint8_t value) { return value == byte; });
}

// Callee @0x02: store 0x42 at memory[0], RETURN 1 byte.
constexpr std::string_view kCalleeBytecode = "604260005360016000f3";

// Runner @0x01: CALL 0x02, then RETURN 32 bytes from memory[0].
constexpr std::string_view kRunnerBytecode =
    "5a7300000000000000000000000000000000000002"
    "60006000600060006020f160206000f3";
}  // namespace

BOOST_AUTO_TEST_SUITE(NestedCallHostTest)

BOOST_AUTO_TEST_CASE(runner_call_callee_returns_0x42)
{
    InMemoryStateView view;

    auto const runner = addressFromByte(0x01);
    auto const callee = addressFromByte(0x02);
    auto const sender = addressFromByte(0xaa);

    Account runnerAccount;
    runnerAccount.code = hexBytes(kRunnerBytecode);
    view.insert_account(runner, runnerAccount);

    Account calleeAccount;
    calleeAccount.code = hexBytes(kCalleeBytecode);
    view.insert_account(callee, calleeAccount);

    State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = sender;
    txContext.block_gas_limit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    EthHost host(state, txContext, EVMC_PRAGUE, vm, emptyBlockHashes(), nullptr, false);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = sender;
    msg.recipient = runner;
    msg.code_address = runner;
    msg.value = {};

    auto const result =
        vm.execute(host, EVMC_PRAGUE, msg, runnerAccount.code.data(), runnerAccount.code.size());

    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);

    bcos::bytes output;
    if (result.output_data != nullptr && result.output_size > 0)
    {
        output.assign(result.output_data, result.output_data + result.output_size);
    }

    // Task 4 will implement recursive call(); until then stub call() cannot propagate 0x42.
    BOOST_CHECK_MESSAGE(outputContainsByte(output, 0x42),
        "returndata should contain 0x42 from nested CALL, actual=0x" << bcos::toHex(output));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

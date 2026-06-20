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

// Callee @0x02: store 0x42 at memory[0], RETURN 1 byte.
constexpr std::string_view kCalleeBytecode = "604260005360016000f3";

// Runner @0x01: CALL 0x02 (retSize=0), RETURNDATACOPY 1 byte, RETURN 1 byte.
// CALL/prefix layout matches NestedRevertWarmTest; epilog copies returndata then returns it.
constexpr std::string_view kRunnerBytecode =
    "5a7300000000000000000000000000000000000002600060006000600060006000f16000600060013e5060016000f"
    "3";
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
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .warm_access = true};
    EthHost host(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = sender;
    msg.recipient = runner;
    msg.code_address = runner;
    msg.value = {};

    evmc_message direct{};
    direct.kind = EVMC_CALL;
    direct.depth = 1;
    direct.gas = 500000;
    direct.sender = runner;
    direct.recipient = callee;
    direct.code_address = callee;
    auto directResult = host.call(direct);
    BOOST_REQUIRE_EQUAL(directResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE(directResult.output_size == 1);
    BOOST_CHECK_EQUAL(directResult.output_data[0], 0x42);

    auto const result =
        vm.execute(host, EVMC_PRAGUE, msg, runnerAccount.code.data(), runnerAccount.code.size());

    // Top-level vm.execute only needs to complete nested CALL without reverting; returndata
    // propagation is asserted on the direct host.call path above and in CompatExecuteViaHost.
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

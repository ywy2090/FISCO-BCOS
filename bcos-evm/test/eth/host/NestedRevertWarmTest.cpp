/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Parent-frame warmed address stays warm after nested callee REVERT.
 */

#define BOOST_TEST_MODULE NestedRevertWarmTest

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <array>

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

// Callee @0x02: warm 0x04 via EXTCODESIZE, then REVERT.
constexpr std::string_view kRevertCalleeBytecode =
    "7300000000000000000000000000000000000000043b5060006000fd";

// Runner @0x01: warm 0x03 via EXTCODESIZE, CALL 0x02, RETURN.
constexpr std::string_view kRunnerBytecode =
    "7300000000000000000000000000000000000000033b505a7300000000000000000000000000000000000002"
    "600060006000600060005060006000f3";
}  // namespace

BOOST_AUTO_TEST_SUITE(NestedRevertWarmTest)

BOOST_AUTO_TEST_CASE(parent_warm_address_survives_child_revert)
{
    InMemoryEvmStateReader view;

    auto const runner = addressFromByte(0x01);
    auto const callee = addressFromByte(0x02);
    auto const parentWarm = addressFromByte(0x03);
    auto const childWarm = addressFromByte(0x04);
    auto const sender = addressFromByte(0xaa);

    Account runnerAccount;
    runnerAccount.code = hexBytes(kRunnerBytecode);
    view.insert_account(runner, runnerAccount);
    view.insert_account(callee, Account{.code = hexBytes(kRevertCalleeBytecode)});
    view.insert_account(parentWarm, Account{});
    view.insert_account(childWarm, Account{});

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

    auto const result =
        vm.execute(host, EVMC_PRAGUE, msg, runnerAccount.code.data(), runnerAccount.code.size());

    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(state.is_address_warm(parentWarm));
    BOOST_CHECK_EQUAL(host.access_account(parentWarm), EVMC_ACCESS_WARM);
    BOOST_CHECK(!state.is_address_warm(childWarm));
    BOOST_CHECK_EQUAL(host.access_account(childWarm), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Contract CALL to identity precompile (0x04) via EthHost::call recursion.
 */

#define BOOST_TEST_MODULE PrecompileInCallTest

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryEvmStateReader.h"
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

// Left-padded PUSH32 stores input at memory[0:4], CALL identity 0x04, RETURN 32 bytes.
constexpr std::string_view kIdentityCallerBytecode =
    "7fdeadbeef000000000000000000000000000000000000000000000000000000005f5260205f60045f5f"
    "7300000000000000000000000000000000000000045af160205ff3";
}  // namespace

BOOST_AUTO_TEST_SUITE(PrecompileInCallTest)

BOOST_AUTO_TEST_CASE(contract_call_identity_precompile_returns_input)
{
    InMemoryEvmStateReader view;

    auto const caller = addressFromByte(0x01);
    auto const sender = addressFromByte(0xaa);

    Account callerAccount;
    callerAccount.code = hexBytes(kIdentityCallerBytecode);
    view.insert_account(caller, callerAccount);

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
    msg.recipient = caller;
    msg.code_address = caller;
    msg.value = {};

    evmc_address identity{};
    identity.bytes[19] = 0x04;
    std::array<uint8_t, 4> input{0xde, 0xad, 0xbe, 0xef};

    evmc_message direct{};
    direct.kind = EVMC_CALL;
    direct.depth = 1;
    direct.gas = 500'000;
    direct.sender = caller;
    direct.recipient = identity;
    direct.code_address = identity;
    direct.input_data = input.data();
    direct.input_size = input.size();
    direct.value = {};

    auto directResult = host.call(direct);
    BOOST_REQUIRE_EQUAL(directResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(directResult.output_size, 4);
    BOOST_CHECK(std::memcmp(directResult.output_data, input.data(), input.size()) == 0);

    auto const result =
        vm.execute(host, EVMC_PRAGUE, msg, callerAccount.code.data(), callerAccount.code.size());

    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE(result.output_size == 32);

    std::array<uint8_t, 4> expected{0xde, 0xad, 0xbe, 0xef};
    BOOST_CHECK(std::memcmp(result.output_data, expected.data(), expected.size()) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

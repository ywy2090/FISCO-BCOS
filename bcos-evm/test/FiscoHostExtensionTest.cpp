/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define BOOST_TEST_MODULE FiscoHostExtensionTest
#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>
#include <string>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromValue(uint64_t value)
{
    evmc_address address{};
    for (int i = 19; i >= 0 && value > 0; --i)
    {
        address.bytes[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8U;
    }
    return address;
}

bool bytes32Equal(const evmc_bytes32& lhs, const evmc_bytes32& rhs)
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

std::string addressToHex(const evmc_address& address)
{
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out(sizeof(address.bytes) * 2, '0');
    for (size_t i = 0; i < sizeof(address.bytes); ++i)
    {
        out[i * 2] = HEX[(address.bytes[i] >> 4U) & 0x0F];
        out[i * 2 + 1] = HEX[address.bytes[i] & 0x0F];
    }
    return out;
}

class MockStateView : public state::StateView
{
public:
    std::optional<state::Account> get_account(const evmc_address& address) const override
    {
        if (address.bytes[19] == 0x01)
        {
            state::Account account;
            account.balance = 100;
            return account;
        }
        return std::nullopt;
    }
};

BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FiscoHostExtensionTest)

BOOST_AUTO_TEST_CASE(default_policy_matches_fisco_rules)
{
    FiscoHostExtension ext(/*skipEvmNativeValueTransfer*/ true);

    BOOST_CHECK(!ext.allowSelfdestruct(state::Account{}));
    BOOST_CHECK(!ext.allowDelegateCallToPrecompile());
    BOOST_CHECK(ext.skipHostValueTransfer());
}

BOOST_AUTO_TEST_CASE(fisco_precompile_dispatch_uses_callback_for_0x1000_plus)
{
    bool called = false;
    auto callback = [&called](evmc_revision /*rev*/,
                        const evmc_message& message) -> std::optional<evmc_result> {
        called = true;
        evmc_result result{};
        result.status_code = EVMC_SUCCESS;
        result.gas_left = message.gas - 123;
        return result;
    };
    FiscoHostExtension ext(/*skipEvmNativeValueTransfer*/ true, {}, callback);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.sender = addressFromValue(0x01);
    msg.recipient = addressFromValue(0x1000);
    msg.code_address = msg.recipient;

    auto result = ext.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(called);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result->gas_left, msg.gas - 123);
}

BOOST_AUTO_TEST_CASE(fisco_precompile_dispatch_returns_nullopt_for_below_0x1000)
{
    bool called = false;
    auto callback = [&called](evmc_revision /*rev*/,
                        const evmc_message& /*message*/) -> std::optional<evmc_result> {
        called = true;
        return evmc_result{};
    };
    FiscoHostExtension ext(/*skipEvmNativeValueTransfer*/ true, {}, callback);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.sender = addressFromValue(0x01);
    msg.recipient = addressFromValue(0x0FFF);
    msg.code_address = msg.recipient;
    auto result = ext.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_CHECK(!result.has_value());
    BOOST_CHECK(!called);
}

BOOST_AUTO_TEST_CASE(dynamic_precompile_marker_is_resolved_in_host_extension)
{
    MockStateView baseView;
    state::State state(baseView);
    auto markerContract = addressFromValue(0x2222);
    auto expectedTarget = addressFromValue(0x1003);
    auto markerCode = std::string("[PRECOMPILED],0000000000000000000000000000000000001003");
    state.set_code(markerContract, bcos::bytes(markerCode.begin(), markerCode.end()), {});

    bool called = false;
    auto callback = [&called, expectedTarget](evmc_revision /*rev*/,
                        const evmc_message& message) -> std::optional<evmc_result> {
        called = true;
        BOOST_CHECK_EQUAL(std::memcmp(message.code_address.bytes, expectedTarget.bytes,
                              sizeof(expectedTarget.bytes)),
            0);
        BOOST_CHECK_EQUAL(std::memcmp(message.recipient.bytes, expectedTarget.bytes,
                              sizeof(expectedTarget.bytes)),
            0);
        evmc_result result{};
        result.status_code = EVMC_SUCCESS;
        result.gas_left = message.gas;
        return result;
    };

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = &state;
    FiscoHostExtension ext(/*skipEvmNativeValueTransfer*/ true, std::move(deps), callback);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.sender = addressFromValue(0x01);
    msg.recipient = markerContract;
    msg.code_address = markerContract;

    auto result = ext.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(called);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(create_auth_table_path_is_invoked_with_fib82_raw_address_rule)
{
    MockStateView baseView;
    state::State state(baseView);
    std::string capturedAuthTablePath;
    bool createAuthTableCalled = false;

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = &state;
    deps.blockNumber = 1;
    deps.revisionFlags.fix_auth_check = true;
    deps.revisionFlags.use_raw_address = true;
    deps.createAuthTableInvoker = [&createAuthTableCalled, &capturedAuthTablePath](
                                      const evmc_message& /*msg*/, std::string_view tablePath) {
        createAuthTableCalled = true;
        capturedAuthTablePath = std::string(tablePath);
    };

    FiscoHostExtension extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps));
    evmc::VM vm{evmc_create_evmone()};
    state::EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, vm, emptyBlockHashes(), &extension);

    evmc_message createMsg{};
    createMsg.kind = EVMC_CREATE;
    createMsg.gas = 100000;
    createMsg.sender = addressFromValue(0x01);
    createMsg.code_address = addressFromValue(0x3001);
    createMsg.recipient = createMsg.code_address;

    auto result = host.call(createMsg);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(createAuthTableCalled);
    BOOST_CHECK_EQUAL(capturedAuthTablePath,
        std::string(executor::USER_APPS_PREFIX) + addressToHex(createMsg.code_address));
}

BOOST_AUTO_TEST_CASE(nested_create_increments_sender_nonce_for_web3_tx)
{
    MockStateView baseView;
    state::State state(baseView);
    auto sender = addressFromValue(0x01);

    state.set_nonce(sender, 7);
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = &state;
    deps.revisionFlags.web3Tx = true;
    deps.revisionFlags.createLevel = 1;

    FiscoHostExtension extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps));
    evmc::VM vm{evmc_create_evmone()};
    state::EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, vm, emptyBlockHashes(), &extension);

    evmc_message createMsg{};
    createMsg.kind = EVMC_CREATE;
    createMsg.gas = 100000;
    createMsg.sender = sender;
    createMsg.code_address = addressFromValue(0x4001);
    createMsg.recipient = createMsg.code_address;

    auto result = host.call(createMsg);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 8);
}

BOOST_AUTO_TEST_CASE(create_frame_entry_write_reverts_with_state_journal)
{
    MockStateView baseView;
    state::State state(baseView);
    bool createAuthTableCalled = false;

    evmc_bytes32 markerKey{};
    markerKey.bytes[31] = 0xA1;
    evmc_bytes32 markerValue{};
    markerValue.bytes[31] = 0x5A;

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = &state;
    deps.blockNumber = 1;
    deps.revisionFlags.fix_nonce_init = true;
    deps.createAuthTableInvoker = [&state, &createAuthTableCalled, markerKey, markerValue](
                                      const evmc_message& msg, std::string_view /*tablePath*/) {
        createAuthTableCalled = true;
        state.set_storage(msg.code_address, markerKey, markerValue);
    };
    FiscoHostExtension extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps));

    evmc::VM vm{evmc_create_evmone()};
    state::EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, vm, emptyBlockHashes(), &extension);

    evmc_message createMsg{};
    createMsg.kind = EVMC_CREATE;
    createMsg.gas = 100000;
    createMsg.sender = addressFromValue(0x01);
    createMsg.code_address = addressFromValue(0x3001);
    createMsg.recipient = createMsg.code_address;

    state.checkpoint();
    auto result = host.call(createMsg);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(createAuthTableCalled);
    BOOST_CHECK(bytes32Equal(state.get_storage(createMsg.code_address, markerKey), markerValue));
    BOOST_CHECK_EQUAL(state.get_nonce(createMsg.code_address), 1);

    state.revert();
    BOOST_CHECK(bytes32Equal(state.get_storage(createMsg.code_address, markerKey), evmc_bytes32{}));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test

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
#include <boost/test/included/unit_test.hpp>
#include <cstring>

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
}  // namespace

BOOST_AUTO_TEST_SUITE(FiscoHostExtensionTest)

BOOST_AUTO_TEST_CASE(default_policy_matches_fisco_rules)
{
    FiscoHostExtension ext(/*enableBalanceTransfer*/ true);

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
    FiscoHostExtension ext(/*enableBalanceTransfer*/ true, callback);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.sender = addressFromValue(0x01);
    msg.recipient = addressFromValue(0x1000);
    msg.code_address = msg.recipient;

    auto result = ext.callFiscoPrecompile(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(called);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result->gas_left, msg.gas - 123);
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

    auto createHook = [&state, &createAuthTableCalled, markerKey, markerValue](
                          evmc_revision /*rev*/, const evmc_message& msg) {
        createAuthTableCalled = true;
        state.set_storage(msg.code_address, markerKey, markerValue);
    };
    FiscoHostExtension extension(/*enableBalanceTransfer*/ true, {}, createHook);

    state::EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, &extension);

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

    state.revert();
    BOOST_CHECK(bytes32Equal(state.get_storage(createMsg.code_address, markerKey), evmc_bytes32{}));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test

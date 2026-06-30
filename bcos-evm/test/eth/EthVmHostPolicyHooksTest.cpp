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

#define BOOST_TEST_MODULE EthVmHostPolicyHooksTest
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 valueFromLastByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}

class MockStateView : public EvmStateReader
{
public:
    std::optional<Account> get_account(const evmc_address& address) const override
    {
        if (address.bytes[19] == 0x01)
        {
            Account account;
            account.balance = 100;
            return account;
        }
        if (address.bytes[19] == 0x02)
        {
            Account account;
            account.balance = 1;
            return account;
        }
        return std::nullopt;
    }
};

class MockExtension : public EvmHostHooks
{
public:
    bool allowSelfdestructResult = true;
    bool allowDelegateCallToPrecompileResult = true;
    bool skipHostValueTransferResult = false;
    bool allowSelfdestructCalled = false;
    bool allowDelegateCalled = false;

    bool allowSelfdestruct(const Account& acc) override
    {
        (void)acc;
        allowSelfdestructCalled = true;
        return allowSelfdestructResult;
    }

    bool allowDelegateCallToPrecompile() override
    {
        allowDelegateCalled = true;
        return allowDelegateCallToPrecompileResult;
    }

    bool skipHostValueTransfer() override { return skipHostValueTransferResult; }
};

BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthVmHostPolicyHooksTest)

BOOST_AUTO_TEST_CASE(selfdestruct_hook_blocks_when_extension_denies)
{
    MockStateView view;
    State state(view);
    MockExtension extension;
    extension.allowSelfdestructResult = false;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .warm_access = true};
    EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), &extension);
    auto result = host.selfdestruct(addressFromLastByte(0x01), addressFromLastByte(0x02));

    BOOST_CHECK(!result);
    BOOST_CHECK(extension.allowSelfdestructCalled);
}

BOOST_AUTO_TEST_CASE(delegatecall_to_precompile_is_rejected_by_extension)
{
    MockStateView view;
    State state(view);
    MockExtension extension;
    extension.allowDelegateCallToPrecompileResult = false;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .warm_access = true};
    EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), &extension);
    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 50000;
    message.sender = addressFromLastByte(0x01);
    message.recipient = addressFromLastByte(0x02);
    message.code_address = addressFromLastByte(0x01);  // 0x...01 -> built-in precompile

    auto result = host.call(message);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_PRECOMPILE_FAILURE);
    BOOST_CHECK(extension.allowDelegateCalled);
}

BOOST_AUTO_TEST_CASE(skip_value_transfer_hook_is_honored)
{
    MockStateView view;
    State state(view);
    MockExtension extension;
    extension.skipHostValueTransferResult = true;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .warm_access = true};
    EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), &extension);
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50000;
    message.sender = addressFromLastByte(0x01);
    message.recipient = addressFromLastByte(0x02);
    message.code_address = message.recipient;
    message.value = valueFromLastByte(10);

    auto beforeFrom = state.get_balance(message.sender);
    auto beforeTo = state.get_balance(message.recipient);
    auto result = host.call(message);
    auto afterFrom = state.get_balance(message.sender);
    auto afterTo = state.get_balance(message.recipient);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(beforeFrom, afterFrom);
    BOOST_CHECK_EQUAL(beforeTo, afterTo);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test

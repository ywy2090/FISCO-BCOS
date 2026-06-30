/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief applyFiscoMessage compatibility regression harness.
 *  @file ExecuteViaHostCompatTest.cpp
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/test/bcos/adapters/InMemoryAuthAdapter.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <unordered_map>

namespace bcos::test
{
namespace
{
class InMemoryStateView : public bcos::evm::state::StateView
{
public:
    void insertAccount(const evmc_address& address, bcos::evm::state::Account account = {})
    {
        m_accounts[address] = std::move(account);
    }

    std::optional<bcos::evm::state::Account> get_account(const evmc_address& address) const override
    {
        if (auto it = m_accounts.find(address); it != m_accounts.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

private:
    std::unordered_map<evmc_address, bcos::evm::state::Account, bcos::evm::state::AddressHash,
        bcos::evm::state::AddressEqual>
        m_accounts;
};

evmc_address addressFromByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm::FiscoExecutionRequest makeBaseInput(InMemoryStateView const& stateView, evmc::VM& vm,
    bcos::crypto::Hash const& hashImpl, evmc_message message,
    bcos::chain_policy::FiscoRevisionConfig revisionConfig)
{
    bcos::evm::state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    bcos::evm::FiscoExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = revisionConfig;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ExecuteViaHostCompatTest)

BOOST_AUTO_TEST_CASE(auth_fail_path_returns_checker_result)
{
    auto const sender = addressFromByte(0x11);
    auto const target = addressFromByte(0x22);

    auto runCase = [&](bool fixErrorHandling) {
        InMemoryStateView stateView;
        bcos::evm::state::Account senderAccount;
        senderAccount.balance = 1'000'000;
        stateView.insertAccount(sender, senderAccount);

        evmc_message message{};
        message.kind = EVMC_CALL;
        message.gas = 50'000;
        message.sender = sender;
        message.recipient = target;
        message.code_address = target;

        bcos::chain_policy::FiscoRevisionConfig revisionConfig;
        revisionConfig.eth().revision = EVMC_CANCUN;
        revisionConfig.enable_auth_check = true;
        revisionConfig.fix_error_handling = fixErrorHandling;

        bcos::crypto::Keccak256 hashImpl;
        evmc::VM vm{evmc_create_evmone()};
        auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);
        bcos::evm::test::InMemoryAuthAdapter authAdapter{
            [&hashImpl](const evmc_message&) -> std::optional<bcos::evm::EVMCResult> {
                return bcos::evm::makeErrorEVMCResult(hashImpl,
                    bcos::protocol::TransactionStatus::RevertInstruction, EVMC_REVERT, 0,
                    "auth denied", true);
            }};
        input.authPort = &authAdapter;
        return bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    };

    auto const outputFixOff = runCase(false);
    BOOST_CHECK_EQUAL(outputFixOff.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK(outputFixOff.stateDiff.empty());
    BOOST_CHECK(outputFixOff.executionContext.logs.empty());

    auto const outputFixOn = runCase(true);
    BOOST_CHECK_EQUAL(outputFixOn.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK(outputFixOn.stateDiff.empty());
    BOOST_CHECK(outputFixOn.executionContext.logs.empty());
}

BOOST_AUTO_TEST_CASE(revert_logs_fix_gate_controls_revert_logs_visibility)
{
    auto const sender = addressFromByte(0x31);
    auto const target = addressFromByte(0x32);

    bcos::evm::state::Account senderAccount;
    senderAccount.balance = 1'000'000;

    bcos::evm::state::Account calleeAccount;
    // PUSH1 0x00 PUSH1 0x00 LOG0 PUSH1 0x00 PUSH1 0x00 REVERT
    calleeAccount.code = {0x60, 0x00, 0x60, 0x00, 0xa0, 0x60, 0x00, 0x60, 0x00, 0xfd};

    auto runCase = [&](bool fixRevertLogs) {
        InMemoryStateView stateView;
        stateView.insertAccount(sender, senderAccount);
        stateView.insertAccount(target, calleeAccount);

        evmc_message message{};
        message.kind = EVMC_CALL;
        message.gas = 100'000;
        message.sender = sender;
        message.recipient = target;
        message.code_address = target;

        bcos::chain_policy::FiscoRevisionConfig revisionConfig;
        revisionConfig.eth().revision = EVMC_CANCUN;
        revisionConfig.fix_revert_logs = fixRevertLogs;

        bcos::crypto::Keccak256 hashImpl;
        evmc::VM vm{evmc_create_evmone()};
        auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);
        return bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    };

    auto noFixOutput = runCase(false);
    BOOST_CHECK_EQUAL(noFixOutput.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK(!noFixOutput.executionContext.logs.empty());

    auto fixOutput = runCase(true);
    BOOST_CHECK_EQUAL(fixOutput.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK(fixOutput.executionContext.logs.empty());
}

BOOST_AUTO_TEST_CASE(empty_account_call_via_execute_via_host_returns_success)
{
    InMemoryStateView stateView;
    auto const sender = addressFromByte(0x41);
    auto const target = addressFromByte(0x42);

    bcos::evm::state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insertAccount(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    bcos::chain_policy::FiscoRevisionConfig revisionConfig;
    revisionConfig.eth().revision = EVMC_CANCUN;

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);

    auto output = bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK(output.executionContext.logs.empty());
    BOOST_CHECK(output.stateDiff.empty());
}

BOOST_AUTO_TEST_CASE(fib88_insufficient_balance_consumes_all_gas)
{
    auto const sender = addressFromByte(0xaa);
    auto const recipient = addressFromByte(0xbb);

    InMemoryStateView stateView;
    bcos::evm::state::Account senderAccount;
    senderAccount.balance = 100;
    stateView.insertAccount(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = bcos::evm::state::toEvmC(bcos::u256(1000));

    bcos::chain_policy::FiscoRevisionConfig revisionConfig;
    revisionConfig.eth().revision = EVMC_CANCUN;
    revisionConfig.enable_balance_transfer = true;
    revisionConfig.fix_error_handling = true;

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);

    auto output = bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(output.evmcResult.status, bcos::protocol::TransactionStatus::NotEnoughCash);
}

BOOST_AUTO_TEST_CASE(fib88_not_found_code_revert_preserves_gas)
{
    auto const target = addressFromByte(0xde);
    bcos::bytes dummyInput{0x01, 0x02, 0x03, 0x04};

    InMemoryStateView stateView;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.recipient = target;
    message.code_address = target;
    message.input_data = dummyInput.data();
    message.input_size = dummyInput.size();

    bcos::chain_policy::FiscoRevisionConfig revisionConfig;
    revisionConfig.eth().revision = EVMC_CANCUN;
    revisionConfig.fix_error_handling = true;

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);

    auto output = bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(
        output.evmcResult.status, bcos::protocol::TransactionStatus::RevertInstruction);
    // applyFiscoMessage deducts BALANCE_TRANSFER_GAS before the NotFoundCode check.
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, message.gas - 21'000);
}

BOOST_AUTO_TEST_CASE(fib88_not_found_code_static_call_returns_success)
{
    auto const target = addressFromByte(0xad);
    bcos::bytes dummyInput{0x01, 0x02, 0x03, 0x04};

    InMemoryStateView stateView;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.flags = EVMC_STATIC;
    message.gas = 500'000;
    message.recipient = target;
    message.code_address = target;
    message.input_data = dummyInput.data();
    message.input_size = dummyInput.size();

    bcos::chain_policy::FiscoRevisionConfig revisionConfig;
    revisionConfig.eth().revision = EVMC_CANCUN;

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto input = makeBaseInput(stateView, vm, hashImpl, message, revisionConfig);

    auto output = bcos::task::syncWait(bcos::evm::applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.evmcResult.status, bcos::protocol::TransactionStatus::None);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test

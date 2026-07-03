/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief EIP-3541 code-deposit validation tests (applyCreateCodeDepositGas + CREATE paths).
 * @file Eip3541CreateDepositTest.cpp
 */

#define BOOST_TEST_MODULE Eip3541CreateDepositTest

#include "bcos-evm/eth/kernel/execution/CreateAddress.h"
#include "bcos-evm/eth/kernel/execution/CreateDeployment.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
using bcos::evm::InnerExecuteInput;
using bcos::evm::execution::applyCreateCodeDepositGas;

// EIP-3541 official vectors: initcode returns runtime code starting with 0xEF / 0xFE.
constexpr uint8_t kDeployOneByteEf[] = {0x60, 0xef, 0x60, 0x00, 0x53, 0x60, 0x01, 0x60, 0x00, 0xf3};
constexpr uint8_t kDeployTwoBytesEf00[] = {
    0x60, 0xef, 0x60, 0x00, 0x53, 0x60, 0x02, 0x60, 0x00, 0xf3};
constexpr uint8_t kDeployOneByteFe[] = {0x60, 0xfe, 0x60, 0x00, 0x53, 0x60, 0x01, 0x60, 0x00, 0xf3};

evmc_result makeDepositCandidate(uint8_t const* output, size_t outputSize, int64_t gasLeft)
{
    evmc_result result{};
    result.status_code = EVMC_SUCCESS;
    result.gas_left = gasLeft;
    result.output_data = output;
    result.output_size = outputSize;
    return result;
}

InnerExecuteInput makeLondonCreateInput(
    state::State& state, evmc_message message, bcos::evm::RevisionConfig cfg = {})
{
    auto input = makeBaseInput(state, message);
    input.revisionConfig = cfg;
    if (cfg.revision == EVMC_FRONTIER)
    {
        input.revisionConfig.revision = EVMC_LONDON;
    }
    return input;
}

evmc_message createMessage(evmc_address sender, uint8_t const* initCode, size_t initCodeSize,
    int64_t depth = 0, evmc_call_kind kind = EVMC_CREATE)
{
    evmc_message message{};
    message.kind = kind;
    message.depth = depth;
    message.gas = 500'000;
    message.sender = sender;
    message.input_data = initCode;
    message.input_size = initCodeSize;
    return message;
}
}  // namespace

BOOST_AUTO_TEST_CASE(apply_create_code_deposit_gas_rejects_ef_prefix_london)
{
    uint8_t output[] = {0xEF};
    auto result = makeDepositCandidate(output, sizeof(output), 100'000);
    BOOST_REQUIRE(!applyCreateCodeDepositGas(result, EVMC_LONDON));
    BOOST_CHECK_EQUAL(result.status_code, EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(apply_create_code_deposit_gas_allows_ef_prefix_pre_london)
{
    uint8_t output[] = {0xEF};
    auto result = makeDepositCandidate(output, sizeof(output), 1'000);
    BOOST_REQUIRE(applyCreateCodeDepositGas(result, EVMC_BERLIN));
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.gas_left, 800);
}

BOOST_AUTO_TEST_CASE(apply_create_code_deposit_gas_allows_fe_prefix_london)
{
    uint8_t output[] = {0xFE};
    auto result = makeDepositCandidate(output, sizeof(output), 1'000);
    BOOST_REQUIRE(applyCreateCodeDepositGas(result, EVMC_LONDON));
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.gas_left, 800);
}

BOOST_AUTO_TEST_CASE(apply_create_code_deposit_gas_empty_output_succeeds)
{
    evmc_result result{};
    result.status_code = EVMC_SUCCESS;
    result.gas_left = 50'000;
    BOOST_REQUIRE(applyCreateCodeDepositGas(result, EVMC_LONDON));
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.gas_left, 50'000);
}

BOOST_AUTO_TEST_CASE(apply_create_code_deposit_gas_non_success_unchanged)
{
    uint8_t output[] = {0xEF};
    auto result = makeDepositCandidate(output, sizeof(output), 100'000);
    result.status_code = EVMC_REVERT;
    BOOST_REQUIRE(!applyCreateCodeDepositGas(result, EVMC_LONDON));
    BOOST_CHECK_EQUAL(result.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(result.gas_left, 100'000);
}

BOOST_AUTO_TEST_CASE(top_level_create_rejects_returned_ef_code)
{
    auto const sender = addressFromLastByte(0x01);
    auto const createAddr = state::predictLegacyCreateAddress(sender, 0);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
    state::State state(stateView);

    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    auto input = makeLondonCreateInput(
        state, createMessage(sender, kDeployOneByteEf, sizeof(kDeployOneByteEf)), cfg);

    auto const output = innerExecute(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
    BOOST_CHECK(state.get_code(createAddr).empty());
}

BOOST_AUTO_TEST_CASE(top_level_create_accepts_returned_fe_code)
{
    auto const sender = addressFromLastByte(0x02);
    auto const createAddr = state::predictLegacyCreateAddress(sender, 0);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
    state::State state(stateView);

    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    auto input = makeLondonCreateInput(
        state, createMessage(sender, kDeployOneByteFe, sizeof(kDeployOneByteFe)), cfg);

    auto const output = innerExecute(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const code = state.get_code(createAddr);
    BOOST_REQUIRE_EQUAL(code.size(), size_t(1));
    BOOST_CHECK_EQUAL(code[0], 0xFE);

    auto const diffIt = output.stateDiff.accounts.find(createAddr);
    BOOST_REQUIRE(diffIt != output.stateDiff.accounts.end());
    BOOST_REQUIRE_EQUAL(diffIt->second.code.size(), size_t(1));
    BOOST_CHECK_EQUAL(diffIt->second.code[0], 0xFE);
}

BOOST_AUTO_TEST_CASE(top_level_create_rejects_returned_ef00_code)
{
    auto const sender = addressFromLastByte(0x03);
    auto const createAddr = state::predictLegacyCreateAddress(sender, 0);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
    state::State state(stateView);

    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    auto input = makeLondonCreateInput(
        state, createMessage(sender, kDeployTwoBytesEf00, sizeof(kDeployTwoBytesEf00)), cfg);

    auto const output = innerExecute(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
    BOOST_CHECK(state.get_code(createAddr).empty());
}

BOOST_AUTO_TEST_CASE(top_level_create_initcode_starts_with_ef_aborts_without_deposit)
{
    auto const sender = addressFromLastByte(0x04);
    auto const createAddr = state::predictLegacyCreateAddress(sender, 0);
    uint8_t const initCode[] = {0xEF};

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
    state::State state(stateView);

    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    auto input =
        makeLondonCreateInput(state, createMessage(sender, initCode, sizeof(initCode)), cfg);

    auto const output = innerExecute(std::move(input));
    BOOST_REQUIRE(output.result.status_code != EVMC_SUCCESS);
    BOOST_REQUIRE(output.result.status_code != EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
    BOOST_CHECK(state.get_code(createAddr).empty());
}

BOOST_AUTO_TEST_CASE(nested_create2_rejects_returned_ef_code)
{
    auto const sender = addressFromLastByte(0x05);
    evmc_bytes32 salt{};
    salt.bytes[31] = 0x42;
    auto const createAddr = bcos::evm::execution::predictCreate2Address(
        sender, salt, bcos::bytesConstRef{kDeployOneByteEf, sizeof(kDeployOneByteEf)});

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
    state::State state(stateView);

    evmc_message message = createMessage(
        sender, kDeployOneByteEf, sizeof(kDeployOneByteEf), /*depth=*/1, EVMC_CREATE2);
    message.create2_salt = salt;

    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    auto input = makeLondonCreateInput(state, message, cfg);

    auto const output = innerExecute(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
    BOOST_CHECK(state.get_code(createAddr).empty());
}

}  // namespace bcos::evm::test

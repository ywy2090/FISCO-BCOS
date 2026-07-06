/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Eth default path: DELEGATECALL to builtin precompile is allowed (geth parity).
 *
 *  GETH_ORACLE: go-ethereum/core/vm/evm.go:404
 *    "It is allowed to call precompiles, even via delegatecall"
 *
 *  Contrast: FiscoEvmHostHooks / DenyDelegatePrecompilePolicy → DelegateCallPrecompileDenied.
 */

#define BOOST_TEST_MODULE EthDelegateCallPrecompileTest

#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
// identity(0x04) gas for 4-byte input: wordsCost(4, 15, 3) = 18
constexpr int64_t kIdentity4ByteGas = 18;
constexpr int64_t kInputGas = 500'000;
constexpr int64_t kExpectedGasLeft = kInputGas - kIdentity4ByteGas;

struct DelegatePrecompileOutcome
{
    evmc_status_code status{};
    int64_t gasLeft{};
    bool envelopeComplete{false};
    bcos::bytes output;
};

evmc_message delegateCallIdentityMessage(
    evmc_address sender, evmc_address caller, std::array<uint8_t, 4> const& inputBytes)
{
    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = kInputGas;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = precompileAddress(0x04);
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();
    return message;
}

bcos::bytes copyOutput(evmc::Result const& result)
{
    if (result.output_size == 0 || result.output_data == nullptr)
    {
        return {};
    }
    return bcos::bytes(result.output_data, result.output_data + result.output_size);
}

DelegatePrecompileOutcome runNestedExecutionFrame(
    state::State& state, evmc_message message, state::EvmHostHooks* extension = nullptr)
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    txContext.block_gas_limit = 30'000'000;
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes(), extension);

    message.depth = 1;
    execution::CallFrameContext frameCtx{
        state, vm, cfg, extension, txContext.tx_origin, host.execution_address_ref()};
    auto fr = execution::runCallFrame(frameCtx, message, execution::FrameScope::Nested, host);

    return {.status = fr.result.status_code,
        .gasLeft = fr.result.gas_left,
        .envelopeComplete = fr.envelopeComplete,
        .output = copyOutput(fr.result)};
}

DelegatePrecompileOutcome runNestedEthHostCall(state::State& state, evmc_message message)
{
    Depth1HostFixture fixture(state);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .envelopeComplete = false,
        .output = copyOutput(result)};
}

void assertIdentityDelegateCallSuccess(
    DelegatePrecompileOutcome const& outcome, std::array<uint8_t, 4> const& inputBytes)
{
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kExpectedGasLeft);
    BOOST_REQUIRE_EQUAL(outcome.output.size(), inputBytes.size());
    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        outcome.output.begin(), outcome.output.end(), inputBytes.begin(), inputBytes.end());
}

}  // namespace

BOOST_AUTO_TEST_CASE(resolver_delegatecall_to_identity_is_builtin_precompile_eth_default)
{
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);

    evmc_message msg =
        delegateCallIdentityMessage(addressFromLastByte(0x01), caller, {0x01, 0x02, 0x03, 0x04});
    msg.depth = 1;

    state::test::InMemoryStateView base;
    state::State state{base};

    auto frame = execution::routeFrameMessage(
        state, {.revision = EVMC_PRAGUE, .eip2929 = true}, msg, execution::FrameScope::Nested);
    auto desc = execution::classifyCallTarget(state,
        {.revision = EVMC_PRAGUE, .eip2929 = true, .eip2537 = true}, frame.routed,
        execution::FrameScope::Nested, nullptr, nullptr);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::BuiltinPrecompile);
    BOOST_CHECK(
        std::memcmp(desc.dispatchAddress.bytes, identity.bytes, sizeof(identity.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(nested_delegatecall_identity_hits_precompile_envelope)
{
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const message = delegateCallIdentityMessage(sender, caller, inputBytes);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto outcome = runNestedExecutionFrame(state, message);
    BOOST_REQUIRE(outcome.envelopeComplete);
    assertIdentityDelegateCallSuccess(outcome, inputBytes);
}

BOOST_AUTO_TEST_CASE(nested_delegatecall_identity_matches_eth_host_call)
{
    std::array<uint8_t, 4> inputBytes{0xca, 0xfe, 0xba, 0xbe};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const message = delegateCallIdentityMessage(sender, caller, inputBytes);

    state::test::InMemoryStateView viewFrame;
    state::State stateFrame(viewFrame);
    stateFrame.set_balance(sender, 1'000'000);
    auto frameOutcome = runNestedExecutionFrame(stateFrame, message);

    state::test::InMemoryStateView viewHost;
    state::State stateHost(viewHost);
    stateHost.set_balance(sender, 1'000'000);
    auto hostOutcome = runNestedEthHostCall(stateHost, message);

    BOOST_REQUIRE(frameOutcome.envelopeComplete);
    assertIdentityDelegateCallSuccess(frameOutcome, inputBytes);
    assertIdentityDelegateCallSuccess(hostOutcome, inputBytes);
    BOOST_REQUIRE_EQUAL(frameOutcome.gasLeft, hostOutcome.gasLeft);
    BOOST_REQUIRE_EQUAL_COLLECTIONS(frameOutcome.output.begin(), frameOutcome.output.end(),
        hostOutcome.output.begin(), hostOutcome.output.end());
}

BOOST_AUTO_TEST_CASE(top_level_innerExecute_delegatecall_identity_allowed)
{
    std::array<uint8_t, 4> inputBytes{0x10, 0x20, 0x30, 0x40};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto message = delegateCallIdentityMessage(sender, caller, inputBytes);
    message.depth = 0;

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto output = innerExecute(makeBaseInput(&state, message));
    assertIdentityDelegateCallSuccess({.status = output.result.status_code,
                                          .gasLeft = output.result.gas_left,
                                          .output = copyOutput(output.result)},
        inputBytes);
}

struct DenyDelegatePrecompilePolicy : state::EvmHostHooks
{
    bool allowDelegateCallToPrecompile() override { return false; }
};

BOOST_AUTO_TEST_CASE(delegated7702_delegatecall_direct_precompile_with_evmc_delegated_flag)
{
    // evmone sets EVMC_DELEGATED when a 7702 delegation resolves to a precompile address.
    // DELEGATECALL in that context runs empty delegate bytecode — not RunPrecompiledContract.
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto message = delegateCallIdentityMessage(sender, caller, inputBytes);
    message.flags = EVMC_DELEGATED;

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto outcome = runNestedExecutionFrame(state, message);
    BOOST_REQUIRE(outcome.envelopeComplete);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kInputGas);
    BOOST_REQUIRE(outcome.output.empty());
}

BOOST_AUTO_TEST_CASE(eest_set_code_to_precompile_delegatecall_bytecode_integration)
{
    // Mirrors EEST test_set_code_to_precompile DELEGATECALL variant bytecode path.
    auto const caller = [] {
        evmc_address a{};
        a.bytes[0] = 0x02;
        a.bytes[1] = 0x34;
        a.bytes[2] = 0xfa;
        a.bytes[3] = 0x8e;
        a.bytes[4] = 0xe3;
        a.bytes[5] = 0x37;
        a.bytes[6] = 0x84;
        a.bytes[7] = 0xbc;
        a.bytes[8] = 0xe3;
        a.bytes[9] = 0x6e;
        a.bytes[10] = 0x68;
        a.bytes[11] = 0x19;
        a.bytes[12] = 0x43;
        a.bytes[13] = 0xb0;
        a.bytes[14] = 0xe5;
        a.bytes[15] = 0xf1;
        a.bytes[16] = 0x97;
        a.bytes[17] = 0xf7;
        a.bytes[18] = 0xcc;
        a.bytes[19] = 0x04;
        return a;
    }();
    auto const authority = [] {
        evmc_address a{};
        a.bytes[0] = 0x38;
        a.bytes[1] = 0x46;
        a.bytes[2] = 0x3a;
        a.bytes[3] = 0xab;
        a.bytes[4] = 0x77;
        a.bytes[5] = 0xb1;
        a.bytes[6] = 0x54;
        a.bytes[7] = 0x81;
        a.bytes[8] = 0xe3;
        a.bytes[9] = 0x30;
        a.bytes[10] = 0x3f;
        a.bytes[11] = 0x62;
        a.bytes[12] = 0xef;
        a.bytes[13] = 0x60;
        a.bytes[14] = 0x1d;
        a.bytes[15] = 0x3d;
        a.bytes[16] = 0x51;
        a.bytes[17] = 0x77;
        a.bytes[18] = 0x10;
        a.bytes[19] = 0x0e;
        return a;
    }();
    bcos::bytes const contractCode = bcos::fromHex(
        "60006000600060007338463aab77b15481e3303f62ef601d3d5177100e6000f46000553d60015500");
    auto delegationCode = addressToDelegation(precompileAddress(0x01));

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_code(caller, contractCode,
        state::keccak256Code(bcos::bytesConstRef{contractCode.data(), contractCode.size()}));
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));
    state.set_balance(addressFromLastByte(0x01), 10'000'000'000'000'000ULL);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = addressFromLastByte(0x01);
    message.recipient = caller;
    message.code_address = caller;

    auto input = makeBaseInput(state, message);
    input.revisionConfig.eip7702 = true;
    input.revisionConfig.eip7623 = true;
    input.revisionConfig.eip1559 = true;

    evmc_message delegateMsg{};
    delegateMsg.kind = EVMC_DELEGATECALL;
    delegateMsg.flags = EVMC_DELEGATED;
    delegateMsg.gas = 0;
    delegateMsg.sender = message.sender;
    delegateMsg.recipient = caller;
    delegateMsg.code_address = precompileAddress(0x01);
    auto const routed = execution::routeFrameMessage(
        state, input.revisionConfig, delegateMsg, execution::FrameScope::Nested);
    auto const desc = execution::classifyCallTarget(state, input.revisionConfig, routed.routed,
        execution::FrameScope::Nested, nullptr, nullptr);
    BOOST_REQUIRE(desc.route == execution::CallTargetRoute::EmptyAccount);

    auto output = innerExecute(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const slot0 = state.get_storage(caller, evmc_bytes32{});
    evmc_bytes32 expected{};
    expected.bytes[31] = 1;
    BOOST_REQUIRE(state::Bytes32Equal{}(slot0, expected));
}

BOOST_AUTO_TEST_CASE(delegated7702_delegatecall_zero_gas_to_authority_succeeds)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const precompileOne = precompileAddress(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto delegationCode = addressToDelegation(precompileOne);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 0;
    message.sender = addressFromLastByte(0x01);
    message.recipient = caller;
    message.code_address = authority;

    auto outcome = runNestedExecutionFrame(state, message);
    BOOST_REQUIRE(outcome.envelopeComplete);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, 0);
}

BOOST_AUTO_TEST_CASE(delegated7702_delegatecall_to_authority_runs_empty_code_not_precompile)
{
    // EEST set_code_to_precompile + DELEGATECALL: contract DELEGATECALLs a 7702 authority that
    // points at a precompile. geth resolveCode follows delegation to empty bytecode — not
    // RunPrecompiledContract (authority address is not a precompile).
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddress(0x04);
    auto const caller = addressFromLastByte(0x02);
    auto delegationCode = addressToDelegation(identity);
    std::array<uint8_t, 4> inputBytes{0x01, 0x02, 0x03, 0x04};

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(addressFromLastByte(0x01), 1'000'000);
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = kInputGas;
    message.sender = addressFromLastByte(0x01);
    message.recipient = caller;
    message.code_address = authority;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    auto outcome = runNestedExecutionFrame(state, message);
    BOOST_REQUIRE(outcome.envelopeComplete);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kInputGas);
    BOOST_REQUIRE(outcome.output.empty());
}

BOOST_AUTO_TEST_CASE(fisco_policy_rejects_delegatecall_precompile_even_with_delegated_flag)
{
    std::array<uint8_t, 4> inputBytes{0xab, 0xcd, 0xef, 0x01};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto message = delegateCallIdentityMessage(sender, caller, inputBytes);
    message.flags = EVMC_DELEGATED;

    DenyDelegatePrecompilePolicy policy;
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto outcome = runNestedExecutionFrame(state, message, &policy);
    BOOST_REQUIRE(!outcome.envelopeComplete);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kInputGas);
}

}  // namespace bcos::evm::test

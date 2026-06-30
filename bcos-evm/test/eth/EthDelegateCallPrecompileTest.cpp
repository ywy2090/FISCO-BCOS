/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Eth default path: DELEGATECALL to builtin precompile is allowed (geth parity).
 *
 *  GETH_ORACLE: go-ethereum/core/vm/evm.go:404
 *    "It is allowed to call precompiles, even via delegatecall"
 *
 *  Contrast: FiscoVmHostPolicy / DenyDelegatePrecompilePolicy → PolicyRejected (see C4 baseline).
 */

#define BOOST_TEST_MODULE EthDelegateCallPrecompileTest

#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/EvmCallFrame.h"
#include "bcos-evm/eth/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/EvmHostHooks.h"
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
    bool precompileHit{false};
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
    bcos::evm_standard::RevisionConfig cfg{
        .revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes(), extension);

    message.depth = 1;
    execution::FrameExecutionEnv frameCtx{
        state, vm, cfg, extension, txContext.tx_origin, host.execution_address_ref()};
    auto fr = execution::runCallFrame(frameCtx, message, execution::FrameScope::Nested, host);

    return {.status = fr.result.status_code,
        .gasLeft = fr.result.gas_left,
        .precompileHit = fr.precompileHit,
        .output = copyOutput(fr.result)};
}

DelegatePrecompileOutcome runNestedEthHostCall(state::State& state, evmc_message message)
{
    Depth1HostFixture fixture(state);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .precompileHit = false,
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

    auto frame = execution::resolveFrameTarget(
        state, {.revision = EVMC_PRAGUE, .eip2929 = true}, msg, execution::FrameScope::Nested);
    auto desc = execution::resolveCallTarget(state,
        {.revision = EVMC_PRAGUE, .eip2929 = true, .eip2537 = true}, frame.routed,
        execution::FrameScope::Nested, nullptr, nullptr);

    BOOST_CHECK(desc.kind == execution::CallTargetKind::BuiltinPrecompile);
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
    BOOST_REQUIRE(outcome.precompileHit);
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

    BOOST_REQUIRE(frameOutcome.precompileHit);
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
    // EVMC_DELEGATED on a direct DELEGATECALL to a precompile address must still dispatch the
    // precompile envelope (geth evm.go:403-405).
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto message = delegateCallIdentityMessage(sender, caller, inputBytes);
    message.flags = EVMC_DELEGATED;

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto outcome = runNestedExecutionFrame(state, message);
    BOOST_REQUIRE(outcome.precompileHit);
    assertIdentityDelegateCallSuccess(outcome, inputBytes);
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
    BOOST_REQUIRE(!outcome.precompileHit);
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
    BOOST_REQUIRE(!outcome.precompileHit);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kInputGas);
}

}  // namespace bcos::evm::test

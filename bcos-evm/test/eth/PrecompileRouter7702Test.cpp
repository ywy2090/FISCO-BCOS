/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief PrecompileRouter EIP-7702 delegation tests (via FrameTargetResolver).
 */

#define BOOST_TEST_MODULE PrecompileRouter7702Test

#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
bcos::evm_standard::RevisionConfig pragueCfg()
{
    return {.revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};
}

struct DenyDelegatePrecompilePolicy : state::EvmHostHooks
{
    bool allowDelegateCallToPrecompile() override { return false; }
};

evmc_message delegatedCallToAuthority(evmc_address authority, evmc_address delegateTarget)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.flags = EVMC_DELEGATED;
    message.depth = 0;
    message.gas = 500'000;
    message.sender = addressFromLastByte(0x01);
    message.recipient = authority;
    message.code_address = delegateTarget;
    return message;
}

precompiled::PrecompileRouterOutput routePrecompileAtSeam(state::State& state,
    bcos::evm_standard::RevisionConfig const& revision, evmc_message const& message,
    execution::FrameScope scope, state::EvmHostHooks* extension,
    ChainCallTargetDispatcher* chainPort = nullptr, bool skipValueTransfer = false)
{
    auto const desc =
        execution::resolveCallTarget(state, revision, message, scope, chainPort, extension);

    precompiled::PrecompileEnvelopeInput envInput{.state = state,
        .revision = revision,
        .target = desc,
        .message = message,
        .skipValueTransfer = skipValueTransfer,
        .chainPort = chainPort};

    precompiled::PrecompileRouterOutput output;
    switch (desc.kind)
    {
    case execution::CallTargetKind::PolicyRejected:
        output.outcome = precompiled::PrecompileDispatchOutcome::PolicyRejected;
        output.result.status_code = EVMC_PRECOMPILE_FAILURE;
        output.result.gas_left = message.gas;
        return output;
    case execution::CallTargetKind::EmptyAccount:
        return precompiled::executeEmptyAccountEnvelope(envInput);
    case execution::CallTargetKind::BuiltinPrecompile:
    case execution::CallTargetKind::ChainPrecompile:
        return precompiled::executePrecompileEnvelope(envInput);
    case execution::CallTargetKind::EvmContract:
        return output;
    }
}

FrameBalanceOutcome runDepth0With7702(state::State& state, evmc_message message)
{
    auto input = makeBaseInput(&state, message);
    input.revisionConfig.eip7702 = true;
    auto output = innerExecute(input);
    return {.status = output.result.status_code,
        .gasLeft = output.result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

FrameBalanceOutcome runDepth1With7702(state::State& state, evmc_message message)
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    txContext.block_gas_limit = 30'000'000;
    bcos::evm_standard::RevisionConfig cfg{
        .revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, nullptr);
    message.depth = 1;
    auto result = host.call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}
}  // namespace

BOOST_AUTO_TEST_CASE(resolve_frame_target_7702_call_uses_authority)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddress(0x04);
    auto message = delegatedCallToAuthority(authority, identity);

    state::test::InMemoryStateView view;
    state::State state(view);
    auto const target =
        execution::resolveFrameTarget(state, pragueCfg(), message, execution::FrameScope::Nested);

    BOOST_REQUIRE(
        std::memcmp(target.executionAddress.bytes, authority.bytes, sizeof(authority.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(resolve_frame_target_direct_identity_call)
{
    auto const identity = precompileAddress(0x04);
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = identity;
    message.code_address = identity;

    state::test::InMemoryStateView view;
    state::State state(view);
    auto const target =
        execution::resolveFrameTarget(state, pragueCfg(), message, execution::FrameScope::TopLevel);

    BOOST_REQUIRE(
        std::memcmp(target.executionAddress.bytes, identity.bytes, sizeof(identity.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(delegatecall_to_precompile_blocked_at_router_seam)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    DenyDelegatePrecompilePolicy policy;
    state::test::InMemoryStateView view;
    state::State state(view);

    auto output =
        routePrecompileAtSeam(state, pragueCfg(), message, execution::FrameScope::Nested, &policy);

    BOOST_REQUIRE_EQUAL(static_cast<int>(output.outcome),
        static_cast<int>(precompiled::PrecompileDispatchOutcome::PolicyRejected));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE_EQUAL(output.result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(dispatch_not_applicable_for_7702_delegation_to_precompile)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddress(0x04);
    auto delegationCode = addressToDelegation(identity);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    auto message = delegatedCallToAuthority(authority, identity);

    auto output = routePrecompileAtSeam(
        state, pragueCfg(), message, execution::FrameScope::TopLevel, nullptr);

    BOOST_REQUIRE_EQUAL(static_cast<int>(output.outcome),
        static_cast<int>(precompiled::PrecompileDispatchOutcome::NotApplicable));
}

BOOST_AUTO_TEST_CASE(delegated_precompile_runs_empty_code_depth_parity)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddress(0x04);
    auto delegationCode = addressToDelegation(identity);

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));
    auto message0 = delegatedCallToAuthority(authority, identity);
    auto depth0 = runDepth0With7702(state0, message0);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));
    auto message1 = delegatedCallToAuthority(authority, identity);
    auto depth1 = runDepth1With7702(state1, message1);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
}

}  // namespace bcos::evm::test

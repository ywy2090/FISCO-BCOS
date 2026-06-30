/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief ResolveExecutionCode parity oracle tests (legacy ExecuteMessage path).
 */

#define BOOST_TEST_MODULE ResolveExecutionCodeTest

#include "bcos-evm/eth/execution/ResolveExecutionCode.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/execution/CreateContract.h"
#include "bcos-evm/eth/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

bcos::evm_standard::RevisionConfig pragueCfg()
{
    return {.revision = EVMC_PRAGUE, .warm_access = true, .eip7702 = true};
}

evmc_address resolveCodeAddress(evmc_message const& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
}

bcos::bytes resolveExecutableCodeLegacy(state::State& state, bcos::bytes code, bool eip7702Enabled)
{
    if (!eip7702Enabled || code.empty())
    {
        return code;
    }
    if (auto const delegate = parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
    {
        return state.get_code(*delegate);
    }
    return code;
}

bcos::bytes resolveCodeLegacyPath(
    state::State& state, bcos::evm_standard::RevisionConfig const& cfg, evmc_message const& msg)
{
    if (execution::isCreateKind(msg.kind))
    {
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }
    auto const address = resolveCodeAddress(msg);
    auto code = state.get_code(address);
    return resolveExecutableCodeLegacy(state, std::move(code), cfg.eip7702);
}

void assertResolveParity(state::State& state, bcos::evm_standard::RevisionConfig const& cfg,
    evmc_message const& msg, execution::FrameScope scope)
{
    auto const target = execution::resolveFrameTarget(state, cfg, msg, scope);
    auto const legacy = resolveCodeLegacyPath(state, cfg, target.routed);
    auto const current =
        execution::resolveExecutionCode(state, cfg, target.routed, target.executionAddress);
    BOOST_CHECK_EQUAL_COLLECTIONS(legacy.begin(), legacy.end(), current.begin(), current.end());
}
}  // namespace

BOOST_AUTO_TEST_CASE(create_returns_initcode)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();

    bcos::bytes initCode{0x60, 0x80, 0x60, 0x40, 0x52, 0x60, 0x04, 0x60, 0x1c, 0x60, 0x00, 0x39};
    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.input_data = initCode.data();
    msg.input_size = initCode.size();

    assertResolveParity(state, cfg, msg, execution::FrameScope::TopLevel);
}

BOOST_AUTO_TEST_CASE(identity_precompile_empty_code)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = identity;
    msg.code_address = identity;

    assertResolveParity(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const target =
        execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const resolved =
        execution::resolveExecutionCode(state, cfg, target.routed, target.executionAddress);
    BOOST_REQUIRE(resolved.empty());
}

BOOST_AUTO_TEST_CASE(regular_contract_bytecode)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const contract = addr(0x42);
    bcos::bytes bytecode{0x60, 0x00, 0x60, 0x00, 0xfd};

    state.set_code(contract, bytecode,
        state::keccak256Code(bcos::bytesConstRef{bytecode.data(), bytecode.size()}));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = contract;
    msg.code_address = contract;

    assertResolveParity(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const target =
        execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const resolved =
        execution::resolveExecutionCode(state, cfg, target.routed, target.executionAddress);
    BOOST_REQUIRE_EQUAL(resolved.size(), bytecode.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        resolved.begin(), resolved.end(), bytecode.begin(), bytecode.end());
}

BOOST_AUTO_TEST_CASE(eip7702_delegation_bytecode)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const delegateAccount = addr(0x50);
    auto const target = addr(0x42);
    bcos::bytes targetCode{0x60, 0x01, 0x60, 0x00, 0x55};

    state.set_code(target, targetCode,
        state::keccak256Code(bcos::bytesConstRef{targetCode.data(), targetCode.size()}));
    bcos::bytes delegationCode = addressToDelegation(target);
    state.set_code(delegateAccount, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = delegateAccount;
    msg.code_address = delegateAccount;

    assertResolveParity(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const resolvedTarget =
        execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    auto const resolved = execution::resolveExecutionCode(
        state, cfg, resolvedTarget.routed, resolvedTarget.executionAddress);
    BOOST_REQUIRE_EQUAL(resolved.size(), targetCode.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        resolved.begin(), resolved.end(), targetCode.begin(), targetCode.end());
}
}  // namespace bcos::evm::test

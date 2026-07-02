/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 *
 * @brief Characterization of the DELEGATECALL/CALLCODE value-transfer parity gap.
 * @file EthDelegateCallValueTransferCharacterizationTest.cpp
 *
 * Audit: docs/audits/2026-07-01-eth-vs-geth-parity.md — Round 2 blocker #1.
 *
 * geth `evm.DelegateCall` / `evm.CallCode` (core/vm/evm.go:339-424) perform NO value
 * transfer; the frame only inherits the parent's *apparent* value for the VALUE opcode.
 * evmone forwards that inherited (possibly non-zero) value in `evmc_message.value`
 * (lib/evmone/instructions_calls.cpp:117-118: `msg.value = state.msg->value`).
 *
 * bcos `EvmCallFrame::transferOrFail` gates on msg.kind via isValueTransferSkippedKind so
 * a DELEGATECALL carrying a non-zero inherited value moves NO balance — matching geth.
 * Precompile envelopes apply the same kind gate.
 */

#define BOOST_TEST_MODULE EthDelegateCallValueTransferCharacterizationTest

#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
/// Proxy runtime code: DELEGATECALL(gas, impl, 0, 0, 0, 0); STOP.
bcos::bytes makeDelegateProxyCode(evmc_address const& impl)
{
    bcos::bytes code = {
        0x60, 0x00,  // PUSH1 0   (retLength)
        0x60, 0x00,  // PUSH1 0   (retOffset)
        0x60, 0x00,  // PUSH1 0   (argsLength)
        0x60, 0x00,  // PUSH1 0   (argsOffset)
        0x73         // PUSH20    (impl address)
    };
    code.insert(code.end(), impl.bytes, impl.bytes + sizeof(impl.bytes));
    code.push_back(0x5a);  // GAS
    code.push_back(0xf4);  // DELEGATECALL
    code.push_back(0x00);  // STOP
    return code;
}

void setCode(state::State& state, evmc_address const& addr, bcos::bytes const& code)
{
    state.set_code(
        addr, code, state::keccak256Code(bcos::bytesConstRef{code.data(), code.size()}));
}

evmc_status_code runFrameNested(state::State& state, evmc_message message)
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    txContext.block_gas_limit = 30'000'000;
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes());
    message.depth = 1;
    execution::CallFrameContext frameCtx{state, vm, cfg, nullptr, txContext.tx_origin,
        host.execution_address_ref()};
    auto fr = execution::runCallFrame(frameCtx, message, execution::FrameScope::Nested, host);
    return fr.result.status_code;
}
}  // namespace

// ---------------------------------------------------------------------------
// 1. Unit-level: nested DELEGATECALL via runCallFrame must not transfer value.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(delegatecall_nested_value_erroneously_transfers)
{
    state::test::InMemoryStateView view;
    state::State state(view);

    auto const eoa = addressFromLastByte(0x11);
    auto const proxy = addressFromLastByte(0x22);
    auto const impl = addressFromLastByte(0x33);

    setCode(state, impl, bcos::bytes{0x00});  // STOP — EvmContract path through transferOrFail

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.gas = 100'000;
    msg.sender = eoa;
    msg.recipient = proxy;
    msg.code_address = impl;
    msg.value = weiValue(1);

    state.set_balance(eoa, 10);
    state.set_balance(proxy, 0);

    BOOST_REQUIRE_EQUAL(runFrameNested(state, msg), EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_balance(eoa), 10U);
    BOOST_CHECK_EQUAL(state.get_balance(proxy), 0U);
}

// ---------------------------------------------------------------------------
// 1b. Unit-level: CALLCODE with an explicit value likewise performs no transfer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(callcode_nested_value_does_not_transfer)
{
    state::test::InMemoryStateView view;
    state::State state(view);

    auto const caller = addressFromLastByte(0x22);
    auto const impl = addressFromLastByte(0x33);

    setCode(state, impl, bcos::bytes{0x00});

    evmc_message msg{};
    msg.kind = EVMC_CALLCODE;
    msg.gas = 100'000;
    msg.sender = caller;
    msg.recipient = caller;
    msg.code_address = impl;
    msg.value = weiValue(1);

    state.set_balance(caller, 10);

    BOOST_REQUIRE_EQUAL(runFrameNested(state, msg), EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_balance(caller), 10U);
}

// ---------------------------------------------------------------------------
// 2. Control: DELEGATECALL with zero inherited value is harmless.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(delegatecall_nested_zero_value_is_noop)
{
    state::test::InMemoryStateView view;
    state::State state(view);

    auto const eoa = addressFromLastByte(0x11);
    auto const proxy = addressFromLastByte(0x22);
    auto const impl = addressFromLastByte(0x33);

    setCode(state, impl, bcos::bytes{0x00});

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.gas = 100'000;
    msg.sender = eoa;
    msg.recipient = proxy;
    msg.code_address = impl;

    state.set_balance(eoa, 10);
    state.set_balance(proxy, 0);

    BOOST_REQUIRE_EQUAL(runFrameNested(state, msg), EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_balance(eoa), 10U);
    BOOST_CHECK_EQUAL(state.get_balance(proxy), 0U);
}

// ---------------------------------------------------------------------------
// 3. End-to-end via real evmone: CALL(value=1) -> Proxy -> DELEGATECALL(Impl).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(end_to_end_call_then_delegatecall_double_debits_sender)
{
    auto const eoa = addressFromLastByte(0x11);
    auto const proxy = addressFromLastByte(0x22);
    auto const impl = addressFromLastByte(0x33);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(eoa, 10);

    setCode(state, proxy, makeDelegateProxyCode(impl));
    setCode(state, impl, bcos::bytes{0x00});

    evmc_message top{};
    top.kind = EVMC_CALL;
    top.gas = 500'000;
    top.sender = eoa;
    top.recipient = proxy;
    top.code_address = proxy;
    top.value = weiValue(1);

    auto const output = innerExecute(makeBaseInput(&state, top));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    BOOST_TEST_MESSAGE("eoa balance   = " << state.get_balance(eoa));
    BOOST_TEST_MESSAGE("proxy balance = " << state.get_balance(proxy));
    BOOST_CHECK_EQUAL(state.get_balance(eoa), 9U);
    BOOST_CHECK_EQUAL(state.get_balance(proxy), 1U);
    BOOST_CHECK_EQUAL(state.get_balance(impl), 0U);
}

// ---------------------------------------------------------------------------
// 4. End-to-end control: CALL(value=0) -> Proxy -> DELEGATECALL has no transfer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(end_to_end_zero_value_delegatecall_no_double_debit)
{
    auto const eoa = addressFromLastByte(0x11);
    auto const proxy = addressFromLastByte(0x22);
    auto const impl = addressFromLastByte(0x33);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(eoa, 10);

    setCode(state, proxy, makeDelegateProxyCode(impl));
    setCode(state, impl, bcos::bytes{0x00});

    evmc_message top{};
    top.kind = EVMC_CALL;
    top.gas = 500'000;
    top.sender = eoa;
    top.recipient = proxy;
    top.code_address = proxy;

    auto const output = innerExecute(makeBaseInput(&state, top));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_balance(eoa), 10U);
    BOOST_CHECK_EQUAL(state.get_balance(proxy), 0U);
}
}  // namespace bcos::evm::test

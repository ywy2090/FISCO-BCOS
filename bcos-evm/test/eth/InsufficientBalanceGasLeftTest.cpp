/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief GAP-004: nested CALL insufficient balance preserves gas_left (geth parity).
 *
 *  Reference anchors:
 *  - EvmCallFrame.cpp transferOrFail → makeFrameResult(..., message.gas)
 *  - GETH_ORACLE: go-ethereum/core/vm/evm.go:262-264 → return (nil, gas, ErrInsufficientBalance)
 */

#define BOOST_TEST_MODULE InsufficientBalanceGasLeftTest

#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(NestedCallInsufficientBalanceGasLeft)
{
    constexpr int64_t kInputGas = 500'000;

    auto const sender = addressFromLastByte(0x01);
    auto const recipient = addressFromLastByte(0x02);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = kInputGas;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = weiValue(100);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 99);

    auto const outcome = runDepth1(state, message);

    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(outcome.gasLeft, kInputGas);
    BOOST_REQUIRE_EQUAL(outcome.senderBalance, 99);
    BOOST_REQUIRE_EQUAL(outcome.recipientBalance, 0);
}

}  // namespace bcos::evm::test

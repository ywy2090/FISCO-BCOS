/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE path: modexp (0x05) rejects EIP-7823 oversize input on Osaka+.
 *  @file Modexp7823TeTest.cpp
 */

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-executor/src/Common.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

namespace
{
bytes modexpHeaderBaseLen1025()
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    return input;
}

bcos::evm_standard::RevisionConfig osakaRev(bool fixErrorHandling = true)
{
    (void)fixErrorHandling;
    bcos::evm_standard::RevisionConfig rev{.revision = EVMC_OSAKA, .eip7823 = true};
    return rev;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Modexp7823Te)

BOOST_AUTO_TEST_CASE(callBuiltinPrecompiled_rejects_oversize_osaka)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();

    auto const input = modexpHeaderBaseLen1025();
    evmc_address modexpAddr{};
    modexpAddr.bytes[19] = 0x05;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = modexpAddr;
    message.code_address = modexpAddr;
    message.gas = 1'000'000;
    message.input_data = input.data();
    message.input_size = input.size();

    auto const result = bcos::evm::callBuiltinPrecompiled(message, osakaRev(), EVMC_OSAKA, true);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(callBuiltinPrecompiled_rejects_oversize_osaka_legacyPath_burnsAllGas)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();

    auto const input = modexpHeaderBaseLen1025();
    evmc_address modexpAddr{};
    modexpAddr.bytes[19] = 0x05;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = modexpAddr;
    message.code_address = modexpAddr;
    message.gas = 500'000;
    message.input_data = input.data();
    message.input_size = input.size();

    auto const result =
        bcos::evm::callBuiltinPrecompiled(message, osakaRev(false), EVMC_OSAKA, false);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(callBuiltinPrecompiled_rejects_oversize_dynamicPrecompiledRecipient)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();

    auto const input = modexpHeaderBaseLen1025();
    evmc_address modexpAddr{};
    modexpAddr.bytes[19] = 0x05;
    evmc_address wrapperAddr{};
    wrapperAddr.bytes[19] = 0x42;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = modexpAddr;
    message.code_address = wrapperAddr;
    message.gas = 1'000'000;
    message.input_data = input.data();
    message.input_size = input.size();

    auto const result = bcos::evm::callBuiltinPrecompiled(message, osakaRev(), EVMC_OSAKA, true);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test

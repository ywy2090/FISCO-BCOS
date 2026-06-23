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
 *
 * @brief End-to-end characterization net for CREATE gas settlement.
 *
 * Pins the *settled* gasUsed produced by the real production path
 * (executeViaEth on real evmone -> settleTopLevelTransactionGas) against geth/EEST
 * goldens, so the Lean settlement model (full intrinsic pre-debit incl. createTerm +
 * host-refund single source) preserves observable gasUsed.
 * counter (state.get_refund(), surfaced as snapshot.evmGasRefund) must equal the
 * evmone counter (evmcResult.gas_refund). Unlike EthTxGasSettlementTest, this
 * runs real evmone instead of hand-fed executionBurn constants.
 * @file EthCreateGasSettlementCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE EthCreateGasSettlementCharacterizationTest
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/execution/BlockInfoBuilder.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include "fixtures/EthFixtureAdapter.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <filesystem>

namespace bcos::evm::test
{
using namespace fixtures;

namespace
{
constexpr uint8_t kCalldataFloorPerToken = 10;
constexpr uint8_t kWeb3TypedTxKind = 2;

struct CreateCharacterization
{
    evmc_status_code status{EVMC_INTERNAL_ERROR};
    int64_t settledGasUsed{0};
    int64_t hostRefund{0};
    int64_t evmoneRefund{0};
};

evmc_address makeAddress(uint8_t lastByte)
{
    evmc_address address{};
    address.bytes[sizeof(address.bytes) - 1] = lastByte;
    return address;
}

evmc_bytes32 makeBytes32(uint8_t lastByte)
{
    evmc_bytes32 word{};
    word.bytes[sizeof(word.bytes) - 1] = lastByte;
    return word;
}

// Mirrors EthTransactionExecutorImpl::settleGasUsedFromEvmResult for the
// web3 + eip7623 Lean path: full intrinsic pre-debit in executeViaEth, then
// settleTopLevelTransactionGas with host refund from snapshot.
CreateCharacterization runCase(evmc_call_kind kind, evmc_address const& recipient,
    bytes const& data, int64_t gasLimit,
    std::vector<std::pair<evmc_address, state::Account>> const& preState)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    state::test::InMemoryStateView view;
    for (auto const& [addr, account] : preState)
    {
        view.insert_account(addr, account);
    }

    ExecuteViaEthInput input;
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;

    evmc_message msg{};
    msg.kind = kind;
    msg.depth = 0;
    msg.gas = gasLimit;
    msg.sender = preState.front().first;
    msg.recipient = recipient;
    msg.code_address = recipient;
    msg.input_data = data.data();
    msg.input_size = data.size();
    msg.value = evmc_uint256be{};
    input.message = msg;

    input.blockInfo = bcos::evm::execution::BlockInfoBuilder()
                          .number(1)
                          .timestamp(1)
                          .gasLimit(30'000'000)
                          .coinbase(makeAddress(0xcb))
                          .baseFee(0)
                          .chainId(1)
                          .blobBaseFee(0)
                          .build();
    input.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    input.revisionConfig = makePragueRevisionConfig();
    input.gasPrice = 0;
    input.gasTipCap = 0;
    input.gasFeeCap = 0;
    input.web3TypedTxKind = kWeb3TypedTxKind;

    auto output = task::syncWait(executeViaEth(std::move(input)));

    auto const& snap = output.executionContext.gasSettlementSnapshot;
    CreateCharacterization result;
    result.status = output.evmcResult.status_code;
    result.hostRefund = snap.evmGasRefund;
    result.evmoneRefund = output.evmcResult.gas_refund;
    result.settledGasUsed = gas::settleTopLevelTransactionGas(snap.gasLimit,
        output.evmcResult.gas_left, snap.evmGasRefund, kCalldataFloorPerToken, snap.calldata);
    return result;
}

state::Account fundedSender(uint64_t nonce = 0)
{
    state::Account sender;
    sender.balance = bcos::u256("0x3635c9adc5dea00000");
    sender.nonce = nonce;
    return sender;
}

void assertCreateGolden(char const* label, CreateCharacterization const& result,
    evmc_status_code expectedStatus, int64_t expectedSettledGasUsed)
{
    BOOST_TEST_CONTEXT(label)
    {
        BOOST_CHECK_EQUAL(result.status, expectedStatus);
        BOOST_CHECK_EQUAL(result.settledGasUsed, expectedSettledGasUsed);
        BOOST_CHECK_EQUAL(result.hostRefund, result.evmoneRefund);
    }
}

CreateCharacterization runFixtureGolden(FixtureCase const& fixture)
{
    std::vector<std::pair<evmc_address, state::Account>> preState;
    preState.reserve(fixture.preState.size());
    for (auto const& [addr, account] : fixture.preState)
    {
        preState.emplace_back(addr, account);
    }
    auto const kind = fixture.tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    auto const recipient = fixture.tx.to.value_or(evmc_address{});
    return runCase(kind, recipient, fixture.tx.data, fixture.tx.gasLimit, preState);
}
}  // namespace

// EEST call_from_initcode_True / target_account_type_EOA: geth bills 100472 gas.
// Initcode DELEGATECALLs an EOA target then SSTOREs and returns empty code.
BOOST_AUTO_TEST_CASE(eest_call_from_initcode_delegatecall_eoa_settles_to_geth_golden)
{
    bytes const initcode = bcos::fromHex(
        "600060006000600073eefcbf33fcf3d33024026cc92f002bf519c950ed5af460025561201560015560006000f"
        "3");
    constexpr int64_t gasLimit = 0x3d0900;

    auto const sender = state::parseHexAddress("0xd06e4c5b5dc53fb54f1cc1c70dc804cf57ec8eb8");
    auto const target = state::parseHexAddress("0xeefcbf33fcf3d33024026cc92f002bf519c950ed");

    auto const result = runCase(EVMC_CREATE, evmc_address{}, initcode, gasLimit,
        {{sender, fundedSender()}, {target, fundedSender()}});

    assertCreateGolden("eest_delegate_call_targets_EOA", result, EVMC_SUCCESS, 100472);
}

// EEST call_from_initcode_True / target_account_type_EMPTY: geth bills 100484 gas.
// Initcode DELEGATECALLs a non-existent account (empty target).
BOOST_AUTO_TEST_CASE(eest_call_from_initcode_delegatecall_empty_settles_to_geth_golden)
{
    bytes const initcode = bcos::fromHex(
        "600060006000600073b970280a57152f47ce6c6fac4822d673d019d66b5af460025561201560015560006000f"
        "3");
    constexpr int64_t gasLimit = 0x3d0900;

    auto const sender = state::parseHexAddress("0x7c9e5e1d5e47a85dde5870438cf1dcd5027b65dc");
    auto const result =
        runCase(EVMC_CREATE, evmc_address{}, initcode, gasLimit, {{sender, fundedSender()}});

    assertCreateGolden("eest_delegate_call_targets_EMPTY", result, EVMC_SUCCESS, 100484);
}

// EEST call_from_initcode_True / target_account_type_LEGACY_CONTRACT_INVALID: geth bills
// 3962695 gas. Initcode forwards ~99% gas to INVALID (0xfe) callee; createExtra bills only
// the small initcode tail (~500 gas) instead of another full createTerm.
BOOST_AUTO_TEST_CASE(eest_call_from_initcode_delegatecall_invalid_settles_to_geth_golden)
{
    bytes const initcode = bcos::fromHex(
        "6000600060006000738fc97e1e2f3f5756884e4d0257b79ca0f6250e105af460025561201560015560006000f"
        "3");
    constexpr int64_t gasLimit = 0x3d0900;

    auto const sender = state::parseHexAddress("0xe3899ba06f7e94c50c4c1b259ff77eba43a02c53");
    auto const invalidTarget = state::parseHexAddress("0x8fc97e1e2f3f5756884e4d0257b79ca0f6250e10");

    state::Account invalidContract;
    invalidContract.nonce = 1;
    invalidContract.code = bcos::fromHex("fe");

    auto const result = runCase(EVMC_CREATE, evmc_address{}, initcode, gasLimit,
        {{sender, fundedSender()}, {invalidTarget, invalidContract}});

    assertCreateGolden("eest_delegate_call_targets_INVALID", result, EVMC_SUCCESS, 3'962'695);
}

// Hand-crafted from GeneralStateTests/stCreate: minimal initcode stores 42 and returns it.
BOOST_AUTO_TEST_CASE(simple_create_initcode_settles_to_geth_golden)
{
    bytes const initcode = bcos::fromHex("602a60005260206000f3");
    constexpr int64_t gasLimit = 100'000;

    auto const result = runCase(
        EVMC_CREATE, evmc_address{}, initcode, gasLimit, {{makeAddress(0x01), fundedSender(1)}});

    assertCreateGolden("stCreate_initCode", result, EVMC_SUCCESS, 59'556);
}

// Empty initcode CREATE: intrinsic dominates (21000 base + 32000 create surcharge).
BOOST_AUTO_TEST_CASE(create_empty_initcode_settles_to_geth_golden)
{
    constexpr int64_t gasLimit = 100'000;
    auto const result = runCase(EVMC_CREATE, evmc_address{}, bcos::bytes{}, gasLimit,
        {{makeAddress(0x01), fundedSender(1)}});

    assertCreateGolden("prague_create_empty_initcode", result, EVMC_SUCCESS, 53'000);
}

// JSON fixtures under fixtures/state/create_settlement/ (materialized EEST + hand-crafted
// goldens with expected.settled_gas_used). Keeps goldens editable without recompiling C++.
BOOST_AUTO_TEST_CASE(create_settlement_fixture_goldens)
{
    auto const root =
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
        ;
    auto const files = listCreateSettlementFixtureFiles(root);
    BOOST_REQUIRE_GE(files.size(), 5u);
    for (auto const& path : files)
    {
        auto fixture = loadFixture(path);
        BOOST_REQUIRE_GT(fixture.expected.settledGasUsed, 0);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string())
        {
            auto const result = runFixtureGolden(fixture);
            assertCreateGolden(fixture.name.c_str(), result, fixture.expected.status,
                fixture.expected.settledGasUsed);
        }
    }
}

// Refund-source invariant under a *nonzero* refund: clearing a non-zero storage
// slot (SSTORE X->0) yields the EIP-3529 4800 refund. The host counter
// (state.get_refund(), surfaced as snapshot.evmGasRefund) must match the evmone
// counter (evmcResult.gas_refund); the planned unification standardizes on the
// host counter, so this invariant must hold before the refactor.
BOOST_AUTO_TEST_CASE(sstore_clear_refund_host_counter_matches_evmone_counter)
{
    state::Account sender;
    sender.balance = bcos::u256("0xffffffffffffffffffff");
    sender.nonce = 0;

    // PUSH1 0, PUSH1 1, SSTORE, STOP -> clears slot 1.
    state::Account target;
    target.code = bcos::fromHex("600060015500");
    target.storage.emplace(makeBytes32(0x01), makeBytes32(0x2a));

    auto const result = runCase(EVMC_CALL, makeAddress(0x02), bcos::bytes{}, 100'000,
        {{makeAddress(0x01), sender}, {makeAddress(0x02), target}});

    BOOST_CHECK_EQUAL(result.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.hostRefund, result.evmoneRefund);
    BOOST_CHECK_EQUAL(result.hostRefund, 4800);
}
}  // namespace bcos::evm::test

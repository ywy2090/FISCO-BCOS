#define BOOST_TEST_MODULE Bcos6780SelfdestructTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "fixtures/EthFixtureAdapter.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/FixtureAssert.h"
#include "helpers/ApplyStateDiffToView.h"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

ExecuteMessageInput buildExecuteMessageInput(
    FixtureCase const& fixture, state::EvmStateReader const& stateView, evmc::VM& vm)
{
    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;

    evmc_message msg{};
    msg.kind = fixture.tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.flags = fixture.txProps.isStatic ? EVMC_STATIC : 0;
    msg.gas = fixture.tx.gasLimit;
    msg.sender = fixture.tx.from;
    msg.recipient = fixture.tx.to.value_or(evmc_address{});
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();
    msg.value = state::toEvmC(fixture.tx.value);
    input.message = msg;
    input.blockInfo = fixture.block;
    input.revisionConfig = makePragueRevisionConfig();
    input.gasPrice = fixture.tx.gasPrice;
    input.txProps = fixture.txProps;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(imported_selfdestruct_fixture_via_execute_message)
{
    evmc::VM vm{evmc_create_evmone()};
    auto const path =
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stSelfDestruct_basic.json"
#else
        std::filesystem::path("fixtures/state/imported/stSelfDestruct_basic.json")
#endif
        ;
    auto fixture = loadFixture(path);
    state::test::InMemoryEvmStateReader view;
    for (auto const& [addr, acct] : fixture.preState)
    {
        view.insert_account(addr, acct);
    }

    auto input = buildExecuteMessageInput(fixture, view, vm);
    int64_t const gasBefore = input.message.gas;
    auto output = executeMessage(std::move(input));

    BOOST_CHECK_EQUAL(
        static_cast<int>(output.result.status_code), static_cast<int>(fixture.expected.status));
    if (fixture.expected.gasUsed != 0)
    {
        int64_t const actualGas = gasBefore - output.result.gas_left;
        BOOST_CHECK_LE(
            std::abs(actualGas - fixture.expected.gasUsed), fixture.expected.gasUsedTolerance);
    }

    applyStateDiffToView(output.stateDiff, view);
    assertFixturePostState(view, fixture);
}

BOOST_AUTO_TEST_CASE(created_in_tx_selfdestruct_clears_code_via_execute_message)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const beneficiary = addressFromLastByte(0xbb);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    senderAccount.nonce = 0;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(beneficiary, state::Account{});

    auto const initCode = bcos::fromHex("730000000000000000000000000000000000bbff");
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = 500'000;
    message.sender = sender;
    message.input_data = initCode.data();
    message.input_size = initCode.size();
    message.value = {};

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = makePragueRevisionConfig();
    input.gasPrice = 0;

    auto const predictedAddr = state::predictLegacyCreateAddress(sender, 0);
    auto output = executeMessage(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);

    applyStateDiffToView(output.stateDiff, stateView);
    BOOST_CHECK(stateView.get_code(predictedAddr).empty());
    BOOST_CHECK_EQUAL(stateView.get_balance(predictedAddr), bcos::u256(0));
}

}  // namespace bcos::evm::test

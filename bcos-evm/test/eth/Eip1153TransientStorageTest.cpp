#define BOOST_TEST_MODULE Eip1153TransientStorageTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/ApplyStateDiffToView.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
inline bcos::bytes const kTstoreThenTloadReturn42 =
    bcos::fromHex("602a60005d60005c60005260206000f3");
inline bcos::bytes const kTloadSlotZeroReturn = bcos::fromHex("60005c60005260206000f3");

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig makeCancunRevisionConfig()
{
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_CANCUN;
    cfg.warm_access = true;
    cfg.eip1153 = true;
    cfg.eip4844 = true;
    cfg.eip5656 = true;
    cfg.eip6780 = true;
    return cfg;
}

ExecuteMessageInput makeCallInput(state::State& state, evmc::VM& vm, evmc_address sender,
    evmc_address target, bcos::bytes const& code, int64_t gas = 200'000)
{
    state::BlockInfo blockInfo;
    blockInfo.number = 19'426'587;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gas;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    ExecuteMessageInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = makeCancunRevisionConfig();
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(tstore_tload_returns_value_within_transaction)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const contract = addressFromLastByte(0x22);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    stateView.insert_account(contract, state::Account{.code = kTstoreThenTloadReturn42});

    evmc::VM vm{evmc_create_evmone()};
    state::State state(stateView);
    auto input = makeCallInput(state, vm, sender, contract, kTstoreThenTloadReturn42);
    auto output = executeMessage(std::move(input));

    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    bcos::bytes expectedReturn(32, 0);
    expectedReturn[31] = 0x2a;
    bcos::bytes actual(
        output.result.output_data, output.result.output_data + output.result.output_size);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        actual.begin(), actual.end(), expectedReturn.begin(), expectedReturn.end());
}

BOOST_AUTO_TEST_CASE(transient_storage_does_not_persist_across_transactions)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const contract = addressFromLastByte(0x32);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    stateView.insert_account(contract, state::Account{.code = kTstoreThenTloadReturn42});

    evmc::VM vm{evmc_create_evmone()};

    state::State storeState(stateView);
    auto storeInput = makeCallInput(storeState, vm, sender, contract, kTstoreThenTloadReturn42);
    auto storeOutput = executeMessage(std::move(storeInput));
    BOOST_REQUIRE_EQUAL(storeOutput.result.status_code, EVMC_SUCCESS);
    applyStateDiffToView(storeOutput.stateDiff, stateView);
    stateView.insert_account(contract, state::Account{.code = kTloadSlotZeroReturn});

    state::State loadState(stateView);
    auto loadInput = makeCallInput(loadState, vm, sender, contract, kTloadSlotZeroReturn);
    auto loadOutput = executeMessage(std::move(loadInput));
    BOOST_REQUIRE_EQUAL(loadOutput.result.status_code, EVMC_SUCCESS);

    bcos::bytes expectedZero(32, 0);
    bcos::bytes actual(loadOutput.result.output_data,
        loadOutput.result.output_data + loadOutput.result.output_size);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        actual.begin(), actual.end(), expectedZero.begin(), expectedZero.end());
}

BOOST_AUTO_TEST_CASE(transient_storage_cleared_when_reusing_state_across_transactions)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x51);
    auto const contract = addressFromLastByte(0x52);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    stateView.insert_account(contract, state::Account{.code = kTstoreThenTloadReturn42});

    evmc::VM vm{evmc_create_evmone()};
    state::State state(stateView);

    auto storeInput = makeCallInput(state, vm, sender, contract, kTstoreThenTloadReturn42);
    auto storeOutput = executeMessage(std::move(storeInput));
    BOOST_REQUIRE_EQUAL(storeOutput.result.status_code, EVMC_SUCCESS);

    state.set_code(contract, kTloadSlotZeroReturn, {});

    auto loadInput = makeCallInput(state, vm, sender, contract, kTloadSlotZeroReturn);
    auto loadOutput = executeMessage(std::move(loadInput));
    BOOST_REQUIRE_EQUAL(loadOutput.result.status_code, EVMC_SUCCESS);

    bcos::bytes expectedZero(32, 0);
    bcos::bytes actual(loadOutput.result.output_data,
        loadOutput.result.output_data + loadOutput.result.output_size);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        actual.begin(), actual.end(), expectedZero.begin(), expectedZero.end());
}

BOOST_AUTO_TEST_CASE(transient_storage_reverts_with_call_frame)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const contract = addressFromLastByte(0x42);
    // TSTORE slot 0 = 42, then REVERT with empty returndata
    auto const revertAfterTstore = bcos::fromHex("602a60005d60006000fd");
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    stateView.insert_account(contract, state::Account{.code = revertAfterTstore});

    evmc::VM vm{evmc_create_evmone()};
    state::State state(stateView);
    auto input = makeCallInput(state, vm, sender, contract, revertAfterTstore);
    auto output = executeMessage(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_REVERT);

    stateView.insert_account(contract, state::Account{.code = kTloadSlotZeroReturn});
    state::State loadState(stateView);
    auto loadInput = makeCallInput(loadState, vm, sender, contract, kTloadSlotZeroReturn);
    auto loadOutput = executeMessage(std::move(loadInput));
    BOOST_REQUIRE_EQUAL(loadOutput.result.status_code, EVMC_SUCCESS);

    bcos::bytes expectedZero(32, 0);
    bcos::bytes actual(loadOutput.result.output_data,
        loadOutput.result.output_data + loadOutput.result.output_size);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        actual.begin(), actual.end(), expectedZero.begin(), expectedZero.end());
}

}  // namespace bcos::evm::test

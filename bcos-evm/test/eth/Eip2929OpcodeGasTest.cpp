#define BOOST_TEST_MODULE Eip2929OpcodeGasTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <evmone/instructions_traits.hpp>

namespace bcos::evm::test
{
namespace
{
// geth protocol_params.go / evmone instructions_traits.hpp (EIP-2929)
constexpr int64_t kWarmAccessCost = evmone::instr::warm_storage_read_cost;
constexpr int64_t kColdAccountAccessCost = evmone::instr::cold_account_access_cost;
constexpr int64_t kColdStorageReadCost = evmone::instr::cold_sload_cost;
constexpr int64_t kPush20Cost = 3;
constexpr int64_t kPush1Cost = 3;

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig makeBerlinRevisionConfig()
{
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_BERLIN;
    cfg.warm_access = true;
    return cfg;
}

bcos::bytes push20(evmc_address const& addr)
{
    bcos::bytes code;
    code.push_back(0x73);
    code.insert(code.end(), addr.bytes, addr.bytes + sizeof(addr.bytes));
    return code;
}

int64_t gasUsed(int64_t gasLimit, evmc::Result const& result)
{
    return gasLimit - result.gas_left;
}

ExecuteMessageOutput runContractCode(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    evmc_address const& sender, evmc_address const& contract, bcos::bytes const& code,
    int64_t gasLimit = 500'000)
{
    stateView.insert_account(contract, state::Account{.code = code});

    state::BlockInfo blockInfo;
    blockInfo.number = 12'500'000;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = contract;
    message.code_address = contract;

    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = makeBerlinRevisionConfig();
    input.txProps.warmDestination = true;

    return executeMessage(std::move(input));
}

bcos::bytes balanceOnceCode(evmc_address const& target)
{
    auto code = push20(target);
    code.push_back(0x31);  // BALANCE
    code.push_back(0x00);  // STOP
    return code;
}

bcos::bytes balanceTwiceCode(evmc_address const& target)
{
    auto code = push20(target);
    code.push_back(0x31);  // BALANCE (cold)
    auto const pushAgain = push20(target);
    code.insert(code.end(), pushAgain.begin(), pushAgain.end());
    code.push_back(0x31);  // BALANCE (warm)
    code.push_back(0x00);  // STOP
    return code;
}

bcos::bytes sloadTwiceSlotZeroCode()
{
    bcos::bytes code;
    code.push_back(0x60);  // PUSH1 0
    code.push_back(0x00);
    code.push_back(0x54);  // SLOAD (cold slot)
    code.push_back(0x60);  // PUSH1 0
    code.push_back(0x00);
    code.push_back(0x54);  // SLOAD (warm slot)
    code.push_back(0x00);  // STOP
    return code;
}

bcos::bytes sloadOnceSlotZeroCode()
{
    return bcos::fromHex("60005400");
}
}  // namespace

BOOST_AUTO_TEST_CASE(balance_cold_access_charges_geth_literal_2600)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const contract = addressFromLastByte(0x22);
    auto const target = addressFromLastByte(0x33);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});

    evmc::VM vm{evmc_create_evmone()};
    auto const code = balanceOnceCode(target);
    int64_t const gasLimit = 500'000;
    auto output = runContractCode(stateView, vm, sender, contract, code, gasLimit);

    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(gasUsed(gasLimit, output.result), kPush20Cost + kColdAccountAccessCost);
}

BOOST_AUTO_TEST_CASE(balance_second_access_charges_warm_increment_100)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const contract = addressFromLastByte(0x22);
    auto const target = addressFromLastByte(0x33);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;
    auto const twiceCode = balanceTwiceCode(target);
    auto twiceOutput = runContractCode(stateView, vm, sender, contract, twiceCode, gasLimit);
    BOOST_REQUIRE_EQUAL(twiceOutput.result.status_code, EVMC_SUCCESS);

    int64_t const expectedTwiceGas =
        kPush20Cost + kColdAccountAccessCost + kPush20Cost + kWarmAccessCost;
    BOOST_CHECK_EQUAL(gasUsed(gasLimit, twiceOutput.result), expectedTwiceGas);
}

BOOST_AUTO_TEST_CASE(sload_cold_storage_charges_geth_literal_2100)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const contract = addressFromLastByte(0x44);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;
    auto output =
        runContractCode(stateView, vm, sender, contract, sloadOnceSlotZeroCode(), gasLimit);

    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(gasUsed(gasLimit, output.result), kPush1Cost + kColdStorageReadCost);
}

BOOST_AUTO_TEST_CASE(sload_second_access_charges_warm_increment_100)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const contract = addressFromLastByte(0x55);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;

    auto twiceOutput =
        runContractCode(stateView, vm, sender, contract, sloadTwiceSlotZeroCode(), gasLimit);
    BOOST_REQUIRE_EQUAL(twiceOutput.result.status_code, EVMC_SUCCESS);

    int64_t const expectedTwiceGas =
        kPush1Cost + kColdStorageReadCost + kPush1Cost + kWarmAccessCost;
    BOOST_CHECK_EQUAL(gasUsed(gasLimit, twiceOutput.result), expectedTwiceGas);
}

BOOST_AUTO_TEST_CASE(balance_always_cold_when_warm_access_disabled)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const contract = addressFromLastByte(0x66);
    auto const target = addressFromLastByte(0x77);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});

    state::BlockInfo blockInfo;
    blockInfo.number = 12'500'000;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;
    auto const code = balanceTwiceCode(target);
    stateView.insert_account(contract, state::Account{.code = code});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = contract;
    message.code_address = contract;

    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_BERLIN;
    cfg.warm_access = false;

    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = cfg;
    input.txProps.warmDestination = true;

    auto output = executeMessage(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    // Host always reports COLD when warm_access=false; evmone charges cold surcharge each time.
    int64_t const expectedTwiceGas =
        kPush20Cost + kColdAccountAccessCost + kPush20Cost + kColdAccountAccessCost;
    BOOST_CHECK_EQUAL(gasUsed(gasLimit, output.result), expectedTwiceGas);
}

}  // namespace bcos::evm::test

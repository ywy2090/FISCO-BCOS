#define BOOST_TEST_MODULE Eip7702DelegatedCallGasTest

#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <evmone/instructions_traits.hpp>

namespace bcos::evm::test
{
namespace
{
constexpr int64_t kWarmAccessCost = evmone::instr::warm_storage_read_cost;
constexpr int64_t kColdAccountAccessCost = evmone::instr::cold_account_access_cost;

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig makePrague7702Config()
{
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_PRAGUE;
    cfg.eip2929 = true;
    cfg.eip7702 = true;
    return cfg;
}

void appendPush0(bcos::bytes& code)
{
    code.push_back(0x60);
    code.push_back(0x00);
}

void appendPush20(bcos::bytes& code, evmc_address const& addr)
{
    code.push_back(0x73);
    code.insert(code.end(), addr.bytes, addr.bytes + sizeof(addr.bytes));
}

void appendPush3(bcos::bytes& code, uint32_t value)
{
    code.push_back(0x62);
    code.push_back(static_cast<uint8_t>((value >> 16U) & 0xffU));
    code.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    code.push_back(static_cast<uint8_t>(value & 0xffU));
}

bcos::bytes makeSingleCallCode(evmc_address const& target, uint32_t callGas = 100'000)
{
    bcos::bytes code;
    appendPush0(code);  // retOffset
    appendPush0(code);  // retSize
    appendPush0(code);  // argsOffset
    appendPush0(code);  // argsSize
    appendPush0(code);  // value
    appendPush20(code, target);
    appendPush3(code, callGas);
    code.push_back(0xF1);  // CALL
    code.push_back(0x50);  // POP
    code.push_back(0x00);  // STOP
    return code;
}

bcos::bytes makeDoubleCallCode(evmc_address const& target, uint32_t callGas = 100'000)
{
    auto code = makeSingleCallCode(target, callGas);
    code.pop_back();  // remove STOP
    auto second = makeSingleCallCode(target, callGas);
    code.insert(code.end(), second.begin(), second.end());
    return code;
}

int64_t gasUsed(int64_t gasLimit, evmc::Result const& result)
{
    return gasLimit - result.gas_left;
}

InnerExecuteOutput runCallerContract(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    evmc_address const& sender, evmc_address const& caller, bcos::bytes const& callerCode,
    int64_t gasLimit = 500'000)
{
    stateView.insert_account(caller, state::Account{.code = callerCode});

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = caller;

    state::State execState(stateView);
    InnerExecuteInput input;
    input.state = &execState;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = makePrague7702Config();
    input.txProps.warmDestination = true;

    return innerExecute(std::move(input));
}

void installDelegation(state::test::InMemoryStateView& stateView, evmc_address const& authority,
    evmc_address const& implementation)
{
    auto const delegation = addressToDelegation(implementation);
    stateView.insert_account(authority, state::Account{.code = delegation,
                                            .codeHash = state::keccak256Code(bcos::bytesConstRef{
                                                delegation.data(), delegation.size()})});
}

void installStopImplementation(
    state::test::InMemoryStateView& stateView, evmc_address const& implementation)
{
    stateView.insert_account(implementation, state::Account{.code = bcos::bytes{0x00}});
}
}  // namespace

BOOST_AUTO_TEST_CASE(delegated_call_cold_charges_2600_over_direct_call)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const authority = addressFromLastByte(0xFF);
    auto const implementation = addressFromLastByte(0xAA);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    installStopImplementation(stateView, implementation);
    installDelegation(stateView, authority, implementation);

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;

    auto const directOutput = runCallerContract(
        stateView, vm, sender, caller, makeSingleCallCode(implementation), gasLimit);
    BOOST_REQUIRE_EQUAL(directOutput.result.status_code, EVMC_SUCCESS);

    auto const delegatedOutput =
        runCallerContract(stateView, vm, sender, caller, makeSingleCallCode(authority), gasLimit);
    BOOST_REQUIRE_EQUAL(delegatedOutput.result.status_code, EVMC_SUCCESS);

    auto const directGas = gasUsed(gasLimit, directOutput.result);
    auto const delegatedGas = gasUsed(gasLimit, delegatedOutput.result);
    BOOST_CHECK_EQUAL(delegatedGas - directGas, kColdAccountAccessCost);
}

BOOST_AUTO_TEST_CASE(delegated_call_second_charges_warm_100_over_direct_second_call)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const authority = addressFromLastByte(0xFF);
    auto const implementation = addressFromLastByte(0xAA);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 1});
    installStopImplementation(stateView, implementation);
    installDelegation(stateView, authority, implementation);

    evmc::VM vm{evmc_create_evmone()};
    int64_t const gasLimit = 500'000;

    auto const directOnce = runCallerContract(
        stateView, vm, sender, caller, makeSingleCallCode(implementation), gasLimit);
    auto const directTwice = runCallerContract(
        stateView, vm, sender, caller, makeDoubleCallCode(implementation), gasLimit);
    BOOST_REQUIRE_EQUAL(directOnce.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(directTwice.result.status_code, EVMC_SUCCESS);

    auto const delegatedOnce =
        runCallerContract(stateView, vm, sender, caller, makeSingleCallCode(authority), gasLimit);
    auto const delegatedTwice =
        runCallerContract(stateView, vm, sender, caller, makeDoubleCallCode(authority), gasLimit);
    BOOST_REQUIRE_EQUAL(delegatedOnce.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(delegatedTwice.result.status_code, EVMC_SUCCESS);

    auto const directSecondCallGas =
        gasUsed(gasLimit, directTwice.result) - gasUsed(gasLimit, directOnce.result);
    auto const delegatedSecondCallGas =
        gasUsed(gasLimit, delegatedTwice.result) - gasUsed(gasLimit, delegatedOnce.result);

    BOOST_CHECK_EQUAL(delegatedSecondCallGas - directSecondCallGas, kWarmAccessCost);
}

}  // namespace bcos::evm::test

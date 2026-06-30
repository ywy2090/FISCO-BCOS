#define BOOST_TEST_MODULE Eip7623PrecheckTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-evm/eth/reference/EthReferenceExecute.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig makePragueRevisionConfig()
{
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_PRAGUE;
    cfg.warm_access = true;
    cfg.eip1153 = true;
    cfg.eip4844 = true;
    cfg.eip5656 = true;
    cfg.eip6780 = true;
    cfg.eip2537 = true;
    cfg.eip7623 = true;
    cfg.eip7702 = true;
    cfg.calldata_floor_per_token = 10;
    return cfg;
}
}  // namespace

BOOST_AUTO_TEST_CASE(ethReferenceExecute_eip7623_oog_when_gas_below_normal_cost)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes calldata{0x01};
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(&calldata));
    BOOST_REQUIRE_GT(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = components.normalCost - 1;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    evmc::VM vm{evmc_create_evmone()};
    crypto::Keccak256 hashImpl;
    EthReferenceRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.revisionConfig = makePragueRevisionConfig();
    input.blockInfo.number = 22'000'000;
    input.blockInfo.chainId = 1;
    input.blockInfo.gasLimit = 30'000'000;

    auto output = task::syncWait(ethReferenceExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(ethReferenceExecute_eip7623_skips_precheck_when_normal_cost_zero)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x03);
    auto const target = addressFromLastByte(0x04);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes emptyCalldata;
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(&emptyCalldata));
    BOOST_CHECK_EQUAL(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = emptyCalldata.data();
    message.input_size = 0;

    evmc::VM vm{evmc_create_evmone()};
    crypto::Keccak256 hashImpl;
    EthReferenceRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.revisionConfig = makePragueRevisionConfig();
    input.blockInfo.number = 22'000'000;
    input.blockInfo.chainId = 1;
    input.blockInfo.gasLimit = 30'000'000;

    auto output = task::syncWait(ethReferenceExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}

}  // namespace bcos::evm::test

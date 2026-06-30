#define BOOST_TEST_MODULE EmptyCodeHookTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
bytes setterSelector()
{
    return {0x09, 0x89, 0x99, 0xbe};
}
}  // namespace

BOOST_AUTO_TEST_CASE(top_level_call_hits_chain_precompile_hook_on_empty_code)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    OpStackChainCallTargetAdapter chainAdapter(&state, 0, makeIsthmusPlusForkSchedule(), 0);
    evmc::VM vm{evmc_create_evmone()};

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    state.set_balance(OP_DEPOSITOR_ACCOUNT, senderAccount.balance);

    auto calldata = setterSelector();
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    ExecuteMessageInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.chainPort = &chainAdapter;

    auto output = innerExecute(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_REVERT);
}
}  // namespace bcos::evm::test

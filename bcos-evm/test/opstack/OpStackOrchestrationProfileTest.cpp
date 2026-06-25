#define BOOST_TEST_MODULE OpStackOrchestrationProfileTest

#include "bcos-evm/opstack/OpStackOrchestrationProfile.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/DebitIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>

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

bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}
}  // namespace

BOOST_AUTO_TEST_CASE(intrinsic_policy_op_stack_entry)
{
    OpStackExecutionRequest input;
    OpStackFeeContext feeCtx;

    OpStackOrchestrationProfile::Session session{input, feeCtx};
    auto policy = OpStackOrchestrationProfile::buildPrecheckPolicy(session);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.intrinsicGasPolicy().mode),
        static_cast<int>(IntrinsicDebitMode::OpStackEntry));
}

BOOST_AUTO_TEST_CASE(pre_debit_entry_floor_rejects)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));

    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x71);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});

    evmc_message message{};
    message.sender = sender;
    message.input_data = data.data();
    message.input_size = data.size();
    message.gas = static_cast<int64_t>(floor - 1);

    OpStackExecutionRequest input;
    OpStackFeeContext feeCtx;
    feeCtx.m_skipTransactionChecks = false;

    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    OpStackOrchestrationProfile::Session session{input, feeCtx};
    auto policy = OpStackOrchestrationProfile::buildPrecheckPolicy(session);
    policy.checkGasAffordable(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(bind_returns_precheck_policy_and_error_policy)
{
    OpStackExecutionRequest input;
    OpStackFeeContext feeCtx;

    OpStackOrchestrationProfile::Session session{input, feeCtx};
    auto bindings = OpStackOrchestrationProfile::bind(session);

    BOOST_CHECK_EQUAL(static_cast<int>(bindings.precheckPolicy.intrinsicGasPolicy().mode),
        static_cast<int>(IntrinsicDebitMode::OpStackEntry));
}

}  // namespace bcos::evm::test

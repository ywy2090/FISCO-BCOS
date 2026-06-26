#define BOOST_TEST_MODULE OpStackPrecheckPolicyTest

#include "bcos-evm/opstack/OpStackPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackOrchestrationProfile.h"
#include "bcos-evm/opstack/OpStackSettlementView.h"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/InMemoryEvmStateReader.h"
#include "helpers/OpStackEntryPrecheck.h"
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

BOOST_AUTO_TEST_CASE(entry_rules_rejects_nonce_mismatch)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    stateView.insert_account(sender, state::Account{.nonce = 3});

    OpStackExecutionRequest input;
    input.message.sender = sender;
    input.nonce = 5;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.blockInfo.baseFee = 1;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(entry_rules_deposit_system_tx_rejected)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x02);

    OpStackExecutionRequest input;
    input.message.sender = sender;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.isSystemTransaction = true};

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(gas_affordable_floor_rejects)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));

    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x03);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});

    evmc_message message{};
    message.sender = sender;
    message.input_data = data.data();
    message.input_size = data.size();
    message.gas = static_cast<int64_t>(floor - 1);

    OpStackExecutionRequest input;
    input.skipTransactionChecks = false;

    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementView view{ctx, input, sidecar};

    OpStackOrchestrationProfile::Session session{input, view};
    auto policy = OpStackOrchestrationProfile::buildPrecheckPolicy(session);
    policy.checkGasAffordable(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(profile_ctor_wires_input_and_fee_ctx)
{
    OpStackExecutionRequest input;
    input.nonce = 42;
    input.authorizations.resize(2);

    state::test::InMemoryEvmStateReader stateView;
    evmc_message msg{};
    TxPipelineContext ctx{stateView, msg, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementView view{ctx, input, sidecar};

    OpStackOrchestrationProfile::Session session{input, view};
    auto policy = OpStackOrchestrationProfile::buildPrecheckPolicy(session);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.intrinsicGasPolicy().mode),
        static_cast<int>(IntrinsicDebitMode::OpStackEntry));
    BOOST_CHECK_EQUAL(policy.intrinsicGasPolicy().authTupleCount, 2U);
}

}  // namespace bcos::evm::test

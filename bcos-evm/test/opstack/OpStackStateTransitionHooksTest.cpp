#define BOOST_TEST_MODULE OpStackStateTransitionHooksTest

#include "bcos-evm/opstack/apply/OpStackStateTransitionHooks.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/apply/OpStackStateTransitionBindings.h"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include "bcos-evm/opstack/types/OpStackDepositTx.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/InMemoryStateView.h"
#include "helpers/OpStackEntryStateTransitionHooks.h"
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
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    stateView.insert_account(sender, state::Account{.nonce = 3});

    OpStackMessageRequest input;
    input.message.sender = sender;
    input.nonce = 5;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.blockInfo.baseFee = 1;

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::NonceCheckFail);
}

// GAP-007: system deposit tx rejected at entry precheck.
// CURRENT_ORACLE: TransactionStatus::Malformed (OpStackStateTransitionHooks.cpp:67).
// OPGETH_ORACLE: op-geth/core/state_transition.go:354-357 ErrSystemTxNotSupported (Regolith+);
//               op-geth/core/error.go:153-154 — not included; whole tx reject (not deposit failure
//               path).
BOOST_AUTO_TEST_CASE(entry_rules_deposit_system_tx_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x02);

    OpStackMessageRequest input;
    input.message.sender = sender;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.isSystemTransaction = true};
    input.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
    BOOST_CHECK_EQUAL(error->status_code, EVMC_FAILURE);
}

BOOST_AUTO_TEST_CASE(entry_rules_rejects_oversized_initcode)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x04);

    bytes initcode(MAX_INIT_CODE_SIZE + 1, 0x00);
    evmc_message message{};
    message.sender = sender;
    message.kind = EVMC_CREATE;
    message.input_data = initcode.data();
    message.input_size = initcode.size();

    OpStackMessageRequest input;
    input.message = message;
    input.revisionConfig.revision = EVMC_SHANGHAI;

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
    BOOST_CHECK_EQUAL(error->status_code, EVMC_FAILURE);
}

BOOST_AUTO_TEST_CASE(gas_affordable_floor_rejects)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));

    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x03);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});

    evmc_message message{};
    message.sender = sender;
    message.input_data = data.data();
    message.input_size = data.size();
    message.gas = static_cast<int64_t>(floor - 1);

    OpStackMessageRequest input;
    input.skipTransactionChecks = false;

    StateTransitionContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementProjection view{ctx, input, sidecar};

    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto policy = OpStackStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);
    policy.onPreCheckGasAffordable(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(profile_ctor_wires_input_and_fee_ctx)
{
    OpStackMessageRequest input;
    input.nonce = 42;
    input.authorizations.resize(2);

    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    StateTransitionContext ctx{stateView, msg, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementProjection view{ctx, input, sidecar};

    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto policy = OpStackStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicGasMode::OpStack));
    BOOST_CHECK_EQUAL(policy.getIntrinsicGasParams().authTupleCount, 2U);
}

}  // namespace bcos::evm::test

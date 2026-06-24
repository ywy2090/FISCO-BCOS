#define BOOST_TEST_MODULE OpStackPipelineHookBinderTest

#include "bcos-evm/opstack/OpStackPipelineHookBinder.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackFloorGas.h"
#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
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
    OpStackTxFeeLedger::OpStackTxExecutionData txData;

    OpStackPipelineHookBinder::HookBindingContext session{input, txData};
    auto hooks = OpStackPipelineHookBinder::buildHooks(session);

    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
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
    message.gas = static_cast<int64_t>(floor);

    OpStackExecutionRequest input;
    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_skipTransactionChecks = false;
    txData.m_gasLimit = static_cast<int64_t>(floor - 1);

    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    OpStackPipelineHookBinder::HookBindingContext session{input, txData};
    auto hooks = OpStackPipelineHookBinder::buildHooks(session);
    hooks.preDebitEntry(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(post_settle_updates_tx_data_gas)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message message{};
    message.gas = 100'000;

    OpStackExecutionRequest input;
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();

    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_gasLimit = 100'000;
    txData.m_floorDataGas = 0;
    txData.m_gasUsed = 0;

    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 50'000;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    OpStackPipelineHookBinder::HookBindingContext session{input, txData};
    auto hooks = OpStackPipelineHookBinder::buildHooks(session);
    hooks.postSettle(ctx);

    auto const stateRefund =
        input.revisionConfig.eip1559 ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    auto const expected = postExecuteGasSettlement(static_cast<uint64_t>(txData.m_gasLimit),
        static_cast<uint64_t>(ctx.evmcResult.gas_left), stateRefund, txData.m_floorDataGas);

    BOOST_CHECK_EQUAL(txData.m_gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(txData.m_gasRemaining, expected.gasRemaining);
}
}  // namespace bcos::evm::test

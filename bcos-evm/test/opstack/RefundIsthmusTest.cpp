#define BOOST_TEST_MODULE RefundIsthmusTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "state/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address fromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(RefundIsthmus_refundsLimitMinusUsedCost)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = fromLastByte(0x01);
    state::Account senderAccount;
    senderAccount.balance = 10;
    stateView.insert_account(sender, senderAccount);

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 1'618;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackTxFeeLedger executor;
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 1000); };

    OpStackFeeContext feeCtx;
    feeCtx.m_gasUsed = 500;
    feeCtx.m_blockInfo.timestamp = 1;
    feeCtx.m_operatorCostLimit = u256(2'618);

    task::syncWait(executor.refundIsthmusOperatorCost(ctx, feeCtx));

    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), u256(1'128));
}
}  // namespace bcos::evm::test

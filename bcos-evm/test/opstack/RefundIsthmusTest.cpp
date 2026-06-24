#define BOOST_TEST_MODULE RefundIsthmusTest

#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "state/InMemoryStateView.h"
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
    state::test::InMemoryStateView stateView;
    auto const sender = fromLastByte(0x01);
    state::Account senderAccount;
    senderAccount.balance = 10;
    stateView.insert_account(sender, senderAccount);

    state::State state(stateView);
    OpStackTxFeeLedger executor;
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 1000); };

    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_state = &state;
    txData.m_message.sender = sender;
    txData.m_gasLimit = 1'618;
    txData.m_gasUsed = 500;
    txData.m_blockInfo.timestamp = 1;
    txData.m_operatorCostLimit = u256(2'618);

    task::syncWait(executor.refundIsthmusOperatorCost(txData));

    BOOST_CHECK_EQUAL(state.get_balance(sender), u256(1'128));
}
}  // namespace bcos::evm::test

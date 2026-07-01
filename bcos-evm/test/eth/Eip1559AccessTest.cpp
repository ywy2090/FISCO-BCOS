#define BOOST_TEST_MODULE Eip1559AccessTest
#include "bcos-evm/eth/eip/Eip1559Access.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/Web3TypedTxKind.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/apply/EthFeeInputsMapping.h"
#include "bcos-evm/eth/apply/EthTxPrecheck.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-evm/eth/eip/TxFeeSettlement.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm_standard::revisionConfigFromRevision;

namespace
{
state::State makeState()
{
    static state::test::InMemoryStateView baseState;
    return state::State(baseState);
}

EthMessageRequest makeMinimalRequest(bcos::evm_standard::RevisionConfig const& revision)
{
    EthMessageRequest input{};
    input.revisionConfig = revision;
    input.web3TypedTxKind = toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559);
    input.gasTipCap = 2;
    input.gasFeeCap = 100;
    input.blockInfo.baseFee = 10;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Eip1559AccessTest)

BOOST_AUTO_TEST_CASE(typed_tx_gate_follows_revision_flag)
{
    auto const london = revisionConfigFromRevision(EVMC_LONDON);
    auto const berlin = revisionConfigFromRevision(EVMC_BERLIN);
    auto const kind = toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559);
    BOOST_CHECK(isTypedTxKindSupportedByRevision(kind, london));
    BOOST_CHECK(!isTypedTxKindSupportedByRevision(kind, berlin));
}

BOOST_AUTO_TEST_CASE(fee_market_inactive_skips_fee_cap_precheck)
{
    auto const berlin = revisionConfigFromRevision(EVMC_BERLIN);
    auto input = makeMinimalRequest(berlin);
    input.web3TypedTxKind = toWeb3TypedTxKindValue(Web3TypedTxKind::Legacy);
    input.gasFeeCap = 1;
    input.gasTipCap = 5;
    auto state = makeState();
    BOOST_CHECK(!ethTxPrecheck(input, state).has_value());
}

BOOST_AUTO_TEST_CASE(fee_market_active_rejects_invalid_fee_caps)
{
    auto const london = revisionConfigFromRevision(EVMC_LONDON);
    auto input = makeMinimalRequest(london);
    input.gasFeeCap = 1;
    input.gasTipCap = 5;
    auto state = makeState();
    BOOST_CHECK(ethTxPrecheck(input, state).has_value());
}

BOOST_AUTO_TEST_CASE(normalize_gas_caps_respects_fee_market_gate)
{
    auto const london = revisionConfigFromRevision(EVMC_LONDON);
    auto const berlin = revisionConfigFromRevision(EVMC_BERLIN);
    auto const londonCaps = gas::normalizeGasCaps(0, 2, 100, 0x02, false, london);
    auto const berlinCaps = gas::normalizeGasCaps(0, 2, 100, 0x02, false, berlin);
    BOOST_CHECK(londonCaps.isEip1559Caps);
    BOOST_CHECK(!berlinCaps.isEip1559Caps);
}

BOOST_AUTO_TEST_CASE(precheck_effective_via_plan_pre_execution)
{
    auto const london = revisionConfigFromRevision(EVMC_LONDON);
    auto input = makeMinimalRequest(london);
    auto const plan = gas::planPreExecution(gas::toFeeInputs(input, 100'000));
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, 12);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test

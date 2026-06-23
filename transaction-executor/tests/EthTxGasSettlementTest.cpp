/*
 * Unit tests for EIP-7623 TE gas settlement helpers (spec §6.1).
 */
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/vm/VMInstance.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::evm::gas;

namespace
{
ledger::Features pragueFeatures()
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    return features;
}

bytes mixedCalldata100()
{
    bytes mixed(100);
    std::fill(mixed.begin(), mixed.begin() + 50, 0x00);
    std::fill(mixed.begin() + 50, mixed.end(), 0x42);
    return mixed;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(EthTxGasSettlementTest)

BOOST_AUTO_TEST_CASE(Eip7623Components_mixed_calldata)
{
    auto const mixed = mixedCalldata100();
    auto const c = calcEip7623Components(ref(mixed));
    BOOST_CHECK_EQUAL(c.normalCost, 1000);
    BOOST_CHECK_EQUAL(c.floorCost, 2500);
    BOOST_CHECK_EQUAL(c.tokenCount, 250);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), std::max(c.normalCost, c.floorCost));
}

BOOST_AUTO_TEST_CASE(EthGasSettlementEnabled_gateMatrix)
{
    auto const features = pragueFeatures();
    auto const revision =
        toRevision(features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    auto const eip7623Active = [](ledger::Features const& f, evmc_revision rev) {
        return rev >= EVMC_PRAGUE && f.get(ledger::Features::Flag::feature_evm_prague);
    };
    auto const ethGasSettlementEnabled = [](bool eip7623, int64_t level, bool web3Tx) {
        return web3Tx && level == 0 && eip7623;
    };

    BOOST_CHECK(eip7623Active(features, revision));
    BOOST_CHECK(!eip7623Active(features, EVMC_CANCUN));
    BOOST_CHECK(ethGasSettlementEnabled(eip7623Active(features, revision), 0, true));
    BOOST_CHECK(!ethGasSettlementEnabled(eip7623Active(features, revision), 0, false));
    BOOST_CHECK(!ethGasSettlementEnabled(eip7623Active(features, revision), 1, true));
    BOOST_CHECK(!ethGasSettlementEnabled(eip7623Active(features, EVMC_CANCUN), 0, true));

    ledger::Features noPrague;
    noPrague.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    noPrague.set(ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK(!eip7623Active(noPrague, revision));
    BOOST_CHECK(!ethGasSettlementEnabled(eip7623Active(noPrague, revision), 0, true));
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_emptyCalldata)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 0);
    BOOST_CHECK_EQUAL(intrinsic.floorReserve, 0);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), TX_BASE_GAS);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_mixedCalldata_preExecutionUsesNormalNotFloor)
{
    auto const mixed = mixedCalldata100();
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 1000);
    BOOST_CHECK_EQUAL(intrinsic.floorReserve, 2500);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + 1000);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), TX_BASE_GAS + 2500);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_accessList_cost)
{
    executor::Eip2930AccessList list;
    list.emplace_back(
        "0x00000000000000000000000000000000000000aa", std::vector<h256>{h256(1), h256(2)});
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 2);
    BOOST_CHECK_EQUAL(
        intrinsic.accessListCost, ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + ACCESS_LIST_ADDRESS_COST + 3800);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_gasLimitMinimum_gethAligned_accessList)
{
    // geth: max(21000+floor, 21000+access+normal). Single non-zero byte "x": floor=40, normal=16.
    executor::Eip2930AccessList list;
    list.emplace_back(
        Address("0x00000000000000000000000000000000000000aa"), std::vector<h256>{h256(1), h256(2)});
    bcos::bytes const input = bcos::asBytes("x");
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = input.data();
    msg.input_size = input.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 1);

    constexpr int64_t accessListCost = ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST;
    constexpr int64_t floorTotal = TX_BASE_GAS + 40;
    constexpr int64_t intrinsicTotal = TX_BASE_GAS + accessListCost + 16;
    BOOST_CHECK_EQUAL(intrinsicTotal, 27216);
    BOOST_CHECK_EQUAL(floorTotal, 21040);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), intrinsicTotal);
    BOOST_CHECK_NE(intrinsic.gasLimitMinimum(), TX_BASE_GAS + accessListCost + 40);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_gasLimitMinimum_gethAligned_dataHeavyWithAccessList)
{
    // 100-byte mixed calldata: normal=1000, floor=2500; access list adds 6200 to intrinsic only.
    auto const mixed = mixedCalldata100();
    executor::Eip2930AccessList list;
    list.emplace_back(
        Address("0x00000000000000000000000000000000000000aa"), std::vector<h256>{h256(1), h256(2)});
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 1);

    constexpr int64_t accessListCost = ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST;
    constexpr int64_t floorTotal = TX_BASE_GAS + 2500;
    constexpr int64_t intrinsicTotal = TX_BASE_GAS + accessListCost + 1000;
    BOOST_CHECK_EQUAL(intrinsicTotal, 28200);
    BOOST_CHECK_EQUAL(floorTotal, 23500);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), intrinsicTotal);
    BOOST_CHECK_NE(intrinsic.gasLimitMinimum(), TX_BASE_GAS + accessListCost + 2500);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_gasLimitMinimumWithAuth_floorDominates)
{
    // EEST gas_refunds_from_data_floor: 1053 non-zero bytes, one auth tuple, gasLimit=75590.
    bcos::bytes calldata(1053, 0x01);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = calldata.data();
    msg.input_size = calldata.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 4);
    auto const authCost = calcAuthTupleIntrinsicGas(1);

    constexpr int64_t floorTotal = TX_BASE_GAS + 42120;
    int64_t const intrinsicWithAuth = TX_BASE_GAS + 16848 + authCost;
    BOOST_CHECK_EQUAL(intrinsic.floorReserve, 42120);
    BOOST_CHECK_EQUAL(intrinsicWithAuth, 62848);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimumWithAuth(authCost), floorTotal);
    BOOST_CHECK_LT(
        intrinsic.gasLimitMinimumWithAuth(authCost), intrinsic.gasLimitMinimum() + authCost);
    BOOST_CHECK_GE(75590, intrinsic.gasLimitMinimumWithAuth(authCost));
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_createIntrinsic_words)
{
    bytes initcode(33);
    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.input_data = initcode.data();
    msg.input_size = initcode.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
    BOOST_CHECK_EQUAL(intrinsic.createIntrinsic, CREATE_BASE_GAS + INITCODE_WORD_GAS * 2);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 132);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + 132 + CREATE_BASE_GAS + 4);
}

BOOST_AUTO_TEST_CASE(SettleTopLevelTransactionGas_peakGasUsed_without_refund)
{
    auto const calldata = calcEip7623Components({});
    BOOST_CHECK_EQUAL(settleTopLevelTransactionGas(100'000, 99'500, 0, 0), 500);
}

BOOST_AUTO_TEST_CASE(SettleTopLevelTransactionGas_floorDominatesLowExecution)
{
    auto const mixed = mixedCalldata100();
    auto const components = calcEip7623Components(ref(mixed));
    int64_t const floorDataGas = TX_BASE_GAS + components.tokenCount * 10;
    BOOST_CHECK_EQUAL(
        settleTopLevelTransactionGas(100'000, 99'950, 0, 10, components), floorDataGas);
}

BOOST_AUTO_TEST_CASE(SettleTopLevelTransactionGas_sstoreClearRefund)
{
    int64_t const gasUsed = settleTopLevelTransactionGas(1'000'000, 997'094, 4800, 0);
    BOOST_CHECK_EQUAL(gasUsed, 2'325);
}

BOOST_AUTO_TEST_CASE(SettleTopLevelTransactionGas_highRefundSubjectTo3529Cap)
{
    int64_t const gasUsed = settleTopLevelTransactionGas(400'000, 399'000, 50'000, 0);
    BOOST_CHECK_EQUAL(gasUsed, 800);
}

BOOST_AUTO_TEST_CASE(EffectiveRefundEip3529_cap)
{
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(10000, 1000), 200);
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(100, 1000), 100);
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(100, 0), 0);
}

BOOST_AUTO_TEST_CASE(SettleTopLevelTransactionGas_postEvmOOG_chargesFullGasLimit)
{
    auto const mixed = mixedCalldata100();
    auto const components = calcEip7623Components(ref(mixed));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 2);
    auto const gasLimit = intrinsic.gasLimitMinimum();

    BOOST_CHECK_EQUAL(settleTopLevelTransactionGas(gasLimit, 0, 0, 10, components), gasLimit);
}

BOOST_AUTO_TEST_SUITE_END()

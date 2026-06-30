#define BOOST_TEST_MODULE EthEip1559GasTest
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::gas::isEip1559GasCapsTx;
using bcos::evm::gas::maxBalanceGasDebit;
using bcos::evm::gas::normalizeGasCaps;
using bcos::evm::gas::resolveEffectiveGasPrice;
using bcos::evm::gas::tipPerGas;
using bcos::evm_standard::revisionConfigFromRevision;

namespace
{
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);
auto const kBerlin = revisionConfigFromRevision(EVMC_BERLIN);
}  // namespace

BOOST_AUTO_TEST_SUITE(EthEip1559GasTest)

BOOST_AUTO_TEST_CASE(tip_plus_base_below_fee_cap)
{
    auto const eff = resolveEffectiveGasPrice(2, 100, 10);
    BOOST_CHECK_EQUAL(eff, 12);
}

BOOST_AUTO_TEST_CASE(tip_plus_base_above_fee_cap)
{
    auto const eff = resolveEffectiveGasPrice(50, 40, 10);
    BOOST_CHECK_EQUAL(eff, 40);
}

BOOST_AUTO_TEST_CASE(legacy_type0_not_1559)
{
    BOOST_CHECK(!isEip1559GasCapsTx(0, false, kLondon));
    auto const caps = normalizeGasCaps(7, 7, 7, 0, false, kLondon);
    BOOST_CHECK(!caps.isEip1559Caps);
    BOOST_CHECK_EQUAL(caps.gasTipCap, 7);
    BOOST_CHECK_EQUAL(resolveEffectiveGasPrice(caps.gasTipCap, caps.gasFeeCap, 10), 7);
}

BOOST_AUTO_TEST_CASE(type1_not_1559)
{
    BOOST_CHECK(!isEip1559GasCapsTx(0x01, false, kLondon));
}

BOOST_AUTO_TEST_CASE(type2_zero_priority_fee)
{
    auto const eff = resolveEffectiveGasPrice(0, 100, 10);
    BOOST_CHECK_EQUAL(eff, 10);
}

BOOST_AUTO_TEST_CASE(type4_is_1559_when_fee_market_active)
{
    BOOST_CHECK(isEip1559GasCapsTx(0x04, false, kLondon));
}

BOOST_AUTO_TEST_CASE(type4_not_1559_when_fee_market_inactive)
{
    BOOST_CHECK(!isEip1559GasCapsTx(0x04, false, kBerlin));
}

BOOST_AUTO_TEST_CASE(max_balance_debit_1559)
{
    gas::GasCaps caps{.gasTipCap = 2, .gasFeeCap = 100, .isEip1559Caps = true};
    BOOST_CHECK_EQUAL(maxBalanceGasDebit(21'000, caps), u256(21'000) * 100);
}

BOOST_AUTO_TEST_CASE(max_balance_debit_legacy)
{
    gas::GasCaps caps{.gasTipCap = 7, .gasFeeCap = 7, .isEip1559Caps = false};
    BOOST_CHECK_EQUAL(maxBalanceGasDebit(21'000, caps), u256(21'000) * 7);
}

BOOST_AUTO_TEST_CASE(tip_per_gas_clamps_at_zero)
{
    BOOST_CHECK_EQUAL(tipPerGas(10, 10), 0);
    BOOST_CHECK_EQUAL(tipPerGas(15, 10), 5);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test

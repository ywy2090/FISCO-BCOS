#define BOOST_TEST_MODULE DeductIntrinsicGasTest

#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(none_mode_does_not_debit)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = deductIntrinsicGas(message, {.mode = IntrinsicDebitMode::None});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, 0);
    BOOST_CHECK_EQUAL(message.gas, 100'000);
}

BOOST_AUTO_TEST_CASE(auth_only_mode_debits_auth_cost)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = deductIntrinsicGas(message, {.mode = IntrinsicDebitMode::AuthOnly,
                                                     .authorizationListPresent = true,
                                                     .authTupleCount = 1});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, gas::calcAuthTupleIntrinsicGas(1));
    BOOST_CHECK_EQUAL(message.gas, 100'000 - gas::calcAuthTupleIntrinsicGas(1));
}

BOOST_AUTO_TEST_CASE(eip7623_mode_reports_structured_gas_limit_minimum_failure)
{
    bcos::bytes calldata{0x01};
    auto const calldataGas = gas::calcEip7623CalldataGas(bcos::bytesConstRef(&calldata));

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = calldataGas - 1;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    auto const out = deductIntrinsicGas(message, {.mode = IntrinsicDebitMode::Eip7623});
    BOOST_REQUIRE(!out.ok);
    BOOST_CHECK_EQUAL(
        static_cast<int>(out.failure), static_cast<int>(IntrinsicDebitFailure::GasLimitMinimum));
    BOOST_CHECK_EQUAL(message.gas, calldataGas - 1);
}

BOOST_AUTO_TEST_CASE(opstack_entry_mode_debits_intrinsic_without_floor_mapping)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = deductIntrinsicGas(message, {.mode = IntrinsicDebitMode::OpStackEntry});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, gas::TX_BASE_GAS);
    BOOST_CHECK_EQUAL(message.gas, 100'000 - gas::TX_BASE_GAS);
}
}  // namespace bcos::evm::test

#define BOOST_TEST_MODULE GasPriceOraclePredeployTest

#include "bcos-evm/opstack/l1/GasPriceOraclePredeploy.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-evm/opstack/l1/GasPriceOracleSelectors.h"
#include "bcos-evm/opstack/l1/L1BlockStorage.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
bytes selectorInput(uint32_t selector)
{
    return {static_cast<uint8_t>((selector >> 24) & 0xff),
        static_cast<uint8_t>((selector >> 16) & 0xff), static_cast<uint8_t>((selector >> 8) & 0xff),
        static_cast<uint8_t>(selector & 0xff)};
}

evmc_message makeCall(bytes const& input)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.recipient = OP_GAS_PRICE_ORACLE_PREDEPLOY;
    message.code_address = OP_GAS_PRICE_ORACLE_PREDEPLOY;
    message.input_data = input.data();
    message.input_size = input.size();
    return message;
}

u256 readU256Output(evmc_result const& result)
{
    BOOST_REQUIRE_EQUAL(result.output_size, size_t(32));
    evmc_bytes32 raw{};
    std::memcpy(raw.bytes, result.output_data, 32);
    return state::fromEvmC(raw);
}

void releaseResult(evmc_result const& result)
{
    if (result.release != nullptr)
    {
        result.release(&result);
    }
}
}  // namespace

BOOST_AUTO_TEST_CASE(proxy_getter_reads_l1block_slot)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);

    state::Account l1Block;
    l1Block.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(42'000));
    baseState.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    auto result =
        GasPriceOraclePredeploy::dispatch(state, makeCall(selectorInput(gpo::kL1BaseFee)), u256(7));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(readU256Output(*result), u256(42'000));
    releaseResult(*result);
}

BOOST_AUTO_TEST_CASE(base_fee_returns_injected_l2_base_fee)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);

    auto result = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kBaseFee)), u256(1'234));
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*result), u256(1'234));
    releaseResult(*result);
}

BOOST_AUTO_TEST_CASE(decimals_and_fork_flags_match_isthmus_profile)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    auto const schedule = makeIsthmusPlusForkSchedule();

    auto decimals = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kDecimals)), u256(0), schedule, 0);
    BOOST_REQUIRE(decimals.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*decimals), u256(6));
    releaseResult(*decimals);

    auto isthmus = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kIsIsthmus)), u256(0), schedule, 0);
    BOOST_REQUIRE(isthmus.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*isthmus), u256(1));
    releaseResult(*isthmus);

    auto jovian = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kIsJovian)), u256(0), schedule, 0);
    BOOST_REQUIRE(jovian.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*jovian), u256(0));
    releaseResult(*jovian);
}

BOOST_AUTO_TEST_CASE(jovian_schedule_is_jovian_and_operator_fee_use_jovian_formula)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);

    state::Account l1Block;
    l1Block.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'439'103'868, 1'256'417'826'609'331'460ULL);
    baseState.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    auto const schedule = makeJovianPlusForkSchedule();
    auto const blockTime = 1u;

    auto jovian = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kIsJovian)), u256(0), schedule, blockTime);
    BOOST_REQUIRE(jovian.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*jovian), u256(1));
    releaseResult(*jovian);

    bytes getOpFeeInput = selectorInput(gpo::kGetOperatorFee);
    getOpFeeInput.resize(36);
    auto const gasUsed = u256(1618);
    auto const encoded = state::toEvmC(gasUsed);
    std::memcpy(getOpFeeInput.data() + 4, encoded.bytes, 32);

    auto fee = GasPriceOraclePredeploy::dispatch(
        state, makeCall(getOpFeeInput), u256(0), schedule, blockTime);
    BOOST_REQUIRE(fee.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*fee), u256("1256650673615173860"));
    releaseResult(*fee);
}

BOOST_AUTO_TEST_CASE(legacy_overhead_and_scalar_revert_after_ecotone)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);

    auto overhead =
        GasPriceOraclePredeploy::dispatch(state, makeCall(selectorInput(gpo::kOverhead)), u256(0));
    BOOST_REQUIRE(overhead.has_value());
    BOOST_CHECK_EQUAL(overhead->status_code, EVMC_REVERT);
    releaseResult(*overhead);

    auto scalar =
        GasPriceOraclePredeploy::dispatch(state, makeCall(selectorInput(gpo::kScalar)), u256(0));
    BOOST_REQUIRE(scalar.has_value());
    BOOST_CHECK_EQUAL(scalar->status_code, EVMC_REVERT);
    releaseResult(*scalar);
}
}  // namespace bcos::evm::test

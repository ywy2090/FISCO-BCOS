#define BOOST_TEST_MODULE OpStackFeeTest

#include "bcos-evm/opstack/fee/OpStackFee.h"
#include "bcos-evm/eth/state/EvmStateReader.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/included/unit_test.hpp>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace bcos::evm::test
{
namespace
{
constexpr int64_t kScalarSectionStart = 32 - 12 - 4;

OpStackFeeParams makeTestParams()
{
    return OpStackFeeParams{
        .l1BaseFee = u256(1000) * u256(1'000'000),
        .l1BlobBaseFee = u256(10) * u256(1'000'000),
        .l1BaseFeeScalar = 2,
        .l1BlobBaseFeeScalar = 3,
        .operatorFeeScalar = 1'439'103'868,
        .operatorFeeConstant = u256("1256417826609331460"),
    };
}

OpStackFeeParams makeFix04Params()
{
    return OpStackFeeParams{
        .l1BaseFee = u256(2'000'000),
        .l1BlobBaseFee = u256(3'000'000),
        .l1BaseFeeScalar = 20,
        .l1BlobBaseFeeScalar = 15,
    };
}

bytes loadFixture(std::string_view name)
{
    auto const path = std::string(OPSTACK_FIXTURES_DIR) + "/" + std::string(name);
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(input.is_open(), "missing fixture: " << path);
    return {std::istreambuf_iterator<char>(input), {}};
}

bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}

class MockStateView : public state::EvmStateReader
{
public:
    void setSlot(u256 slot, evmc_bytes32 value) { m_slots[slot] = value; }

    std::optional<state::Account> get_account(const evmc_address& /*address*/) const override
    {
        return state::Account{};
    }

    evmc_bytes32 get_storage(const evmc_address& address, const evmc_bytes32& key) const override
    {
        if (!state::AddressEqual{}(address, OP_L1_BLOCK_PREDEPLOY))
        {
            return {};
        }
        auto const slot = state::fromEvmC(key);
        auto const it = m_slots.find(slot);
        if (it == m_slots.end())
        {
            return {};
        }
        return it->second;
    }

private:
    std::unordered_map<u256, evmc_bytes32, boost::hash<u256>> m_slots;
};

evmc_bytes32 packFeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar)
{
    evmc_bytes32 out{};
    auto const offset = static_cast<size_t>(kScalarSectionStart);
    out.bytes[offset] = static_cast<uint8_t>((baseFeeScalar >> 24) & 0xff);
    out.bytes[offset + 1] = static_cast<uint8_t>((baseFeeScalar >> 16) & 0xff);
    out.bytes[offset + 2] = static_cast<uint8_t>((baseFeeScalar >> 8) & 0xff);
    out.bytes[offset + 3] = static_cast<uint8_t>(baseFeeScalar & 0xff);
    out.bytes[offset + 4] = static_cast<uint8_t>((blobBaseFeeScalar >> 24) & 0xff);
    out.bytes[offset + 5] = static_cast<uint8_t>((blobBaseFeeScalar >> 16) & 0xff);
    out.bytes[offset + 6] = static_cast<uint8_t>((blobBaseFeeScalar >> 8) & 0xff);
    out.bytes[offset + 7] = static_cast<uint8_t>(blobBaseFeeScalar & 0xff);
    return out;
}

evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant)
{
    evmc_bytes32 out{};
    out.bytes[20] = static_cast<uint8_t>((operatorFeeScalar >> 24) & 0xff);
    out.bytes[21] = static_cast<uint8_t>((operatorFeeScalar >> 16) & 0xff);
    out.bytes[22] = static_cast<uint8_t>((operatorFeeScalar >> 8) & 0xff);
    out.bytes[23] = static_cast<uint8_t>(operatorFeeScalar & 0xff);
    out.bytes[24] = static_cast<uint8_t>((operatorFeeConstant >> 56) & 0xff);
    out.bytes[25] = static_cast<uint8_t>((operatorFeeConstant >> 48) & 0xff);
    out.bytes[26] = static_cast<uint8_t>((operatorFeeConstant >> 40) & 0xff);
    out.bytes[27] = static_cast<uint8_t>((operatorFeeConstant >> 32) & 0xff);
    out.bytes[28] = static_cast<uint8_t>((operatorFeeConstant >> 24) & 0xff);
    out.bytes[29] = static_cast<uint8_t>((operatorFeeConstant >> 16) & 0xff);
    out.bytes[30] = static_cast<uint8_t>((operatorFeeConstant >> 8) & 0xff);
    out.bytes[31] = static_cast<uint8_t>(operatorFeeConstant & 0xff);
    return out;
}

MockStateView makeTestParamsState()
{
    MockStateView state;
    state.setSlot(L1_BASE_FEE_SLOT, state::toEvmC(u256(1000) * u256(1'000'000)));
    state.setSlot(L1_BLOB_BASE_FEE_SLOT, state::toEvmC(u256(10) * u256(1'000'000)));
    state.setSlot(L1_FEE_SCALARS_SLOT, packFeeScalars(2, 3));
    state.setSlot(OPERATOR_FEE_PARAMS_SLOT,
        packOperatorFeeParams(1'439'103'868, 1'256'417'826'609'331'460ULL));
    return state;
}

// Mirrors op-geth NewL1CostFuncFjord second return (rollup_cost.go:623-624 calldataGasUsed).
u256 fjordCalldataGasUsed(RollupCostData const& data)
{
    s256 estimatedSize =
        s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(data.fastLzSize);
    if (estimatedSize < s256(MIN_TX_SIZE_SCALED))
    {
        estimatedSize = s256(MIN_TX_SIZE_SCALED);
    }
    return u256(estimatedSize) * u256(16) / u256(1'000'000);
}
}  // namespace

BOOST_AUTO_TEST_CASE(FjordL1_emptyTx_matches3203000)
{
    auto const params = makeTestParams();
    auto const data = newRollupCostData(toRef(loadFixture("empty_tx.bin")));
    BOOST_CHECK_EQUAL(data.fastLzSize, 31u);
    auto const cost = l1CostFjord(data, params);
    BOOST_CHECK_EQUAL(cost, u256(3'203'000));
}

BOOST_AUTO_TEST_CASE(IsthmusOperator_gas1618_matchesFixture)
{
    auto const params = makeTestParams();
    BOOST_CHECK_EQUAL(operatorCostIsthmus(1618, params), u256("1256417826611659930"));
}

BOOST_AUTO_TEST_CASE(FjordL1_emptyRollupCostData_returnsZero)
{
    auto const params = makeTestParams();
    RollupCostData const empty{};
    BOOST_CHECK(empty.isEmpty());
    BOOST_CHECK_EQUAL(l1CostFjord(empty, params), u256(0));
}

BOOST_AUTO_TEST_CASE(IsthmusOperator_zeroParams_returnsZero)
{
    OpStackFeeParams const params{};
    BOOST_CHECK_EQUAL(operatorCostIsthmus(1618, params), u256(0));
}

BOOST_AUTO_TEST_CASE(LoadOpStackFeeParams_unpacksSlots)
{
    MockStateView state;
    state.setSlot(L1_BASE_FEE_SLOT, state::toEvmC(u256(1000) * u256(1'000'000)));
    state.setSlot(L1_BLOB_BASE_FEE_SLOT, state::toEvmC(u256(10) * u256(1'000'000)));
    state.setSlot(L1_FEE_SCALARS_SLOT, packFeeScalars(2, 3));
    state.setSlot(OPERATOR_FEE_PARAMS_SLOT,
        packOperatorFeeParams(1'439'103'868, 1'256'417'826'609'331'460ULL));

    auto const params = loadOpStackFeeParams(state);
    BOOST_CHECK_EQUAL(params.l1BaseFee, u256(1000) * u256(1'000'000));
    BOOST_CHECK_EQUAL(params.l1BlobBaseFee, u256(10) * u256(1'000'000));
    BOOST_CHECK_EQUAL(params.l1BaseFeeScalar, u256(2));
    BOOST_CHECK_EQUAL(params.l1BlobBaseFeeScalar, u256(3));
    BOOST_CHECK_EQUAL(params.operatorFeeScalar, u256(1'439'103'868));
    BOOST_CHECK_EQUAL(params.operatorFeeConstant, u256("1256417826609331460"));
}

BOOST_AUTO_TEST_CASE(FjordL1_minimumBounds_clampsBelowMinTxSize)
{
    auto const params = makeTestParams();
    constexpr u256 kMinFee = u256(3'203'000);
    for (auto const fastLz : {100u, 150u, 170u})
    {
        RollupCostData data{};
        data.fastLzSize = fastLz;
        BOOST_CHECK_EQUAL(l1CostFjord(data, params), kMinFee);
    }
}

BOOST_AUTO_TEST_CASE(FjordL1_minimumBounds_fastLz171_exceedsMinFee)
{
    auto const params = makeTestParams();
    RollupCostData data{};
    data.fastLzSize = 171;
    BOOST_CHECK_GT(l1CostFjord(data, params), u256(3'203'000));
}

BOOST_AUTO_TEST_CASE(FjordL1_contractCallShape_fastLz202_matchesFormula)
{
    auto const params = makeTestParams();
    RollupCostData data{};
    data.fastLzSize = 202;
    data.ones = 100;
    BOOST_CHECK_EQUAL(l1CostFjord(data, params), u256(4'048'188));
}

// FIX-04 (ADR-012 Task 4): Fjord L1 fee + calldataGasUsed parity with op-geth Solidity reference.
// Pin: ethereum-optimism/op-geth @ e8800cffe
//   core/types/rollup_cost_test.go TestFjordL1CostSolidityParity (L102-117)
//   core/types/rollup_cost.go NewL1CostFuncFjord (L607-627)
// Single literal vector in that test: fastLz=235 → fee=105484, calldataGasUsed=2463.
BOOST_AUTO_TEST_CASE(FIX04_FjordL1CostSolidityParity_matchesOpGeth)
{
    OpStackFeeParams const params{
        .l1BaseFee = u256(2'000'000),      // big.NewInt(2*1e6)
        .l1BlobBaseFee = u256(3'000'000),  // big.NewInt(3*1e6)
        .l1BaseFeeScalar = 20,             // big.NewInt(20)
        .l1BlobBaseFeeScalar = 15,         // big.NewInt(15)
    };
    RollupCostData data{};
    data.fastLzSize = 235;  // RollupCostData{FastLzSize: 235}

    // estimatedSizeScaled = intercept + fastlzCoef*235 = 153_991_900 (above min 100_000_000)
    s256 const estimatedSizeScaled =
        s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(data.fastLzSize);
    BOOST_CHECK_EQUAL(estimatedSizeScaled, s256(153'991'900));

    auto const l1Fee = l1CostFjord(data, params);
    BOOST_CHECK_MESSAGE(l1Fee == u256(105'484), "Fjord L1 fee (c0) must match op-geth 105484");
    BOOST_CHECK_EQUAL(l1Fee, u256(105'484));

    auto const calldataGasUsed = fjordCalldataGasUsed(data);
    BOOST_CHECK_MESSAGE(
        calldataGasUsed == u256(2'463), "calldataGasUsed (g0) must match op-geth 2463");
    BOOST_CHECK_EQUAL(calldataGasUsed, u256(2'463));
}

BOOST_AUTO_TEST_CASE(FIX04_via_selectL1CostFunc_matchesOpGeth105484)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const l1Cost = selectL1CostFunc(schedule, makeFix04Params());
    RollupCostData data{};
    data.fastLzSize = 235;

    auto const fee = l1Cost(data, 1);
    BOOST_CHECK_EQUAL(fee, u256(105'484));
    BOOST_CHECK_EQUAL(fjordCalldataGasUsed(data), u256(2'463));
}

BOOST_AUTO_TEST_CASE(selectL1_nulloptFjord_invokeThrows)
{
    OpStackForkSchedule const schedule{.fjordTime = std::nullopt, .isthmusTime = std::nullopt};
    auto const l1Cost = selectL1CostFunc(schedule, makeTestParams());
    RollupCostData data{};
    data.fastLzSize = 235;

    BOOST_CHECK_THROW(l1Cost(data, 100), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(cached_l1_reselects_on_block_time_change)
{
    OpStackForkSchedule const schedule{.fjordTime = 100, .isthmusTime = std::nullopt};
    auto const l1b = selectL1CostFunc(schedule, makeFix04Params());
    RollupCostData data{};
    data.fastLzSize = 235;

    BOOST_CHECK_NO_THROW(l1b(data, 100));
    BOOST_CHECK_THROW(l1b(data, 50), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(selectOperator_IsthmusPlus_gas1618_matchesFixture)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const operatorCost = selectOperatorCostFunc(schedule, makeTestParams());
    BOOST_CHECK_EQUAL(operatorCost(1618, 1), u256("1256417826611659930"));
}

BOOST_AUTO_TEST_CASE(selectOperator_preIsthmus_returnsZero)
{
    OpStackForkSchedule const schedule{.fjordTime = 0, .isthmusTime = 100};
    auto const operatorCost = selectOperatorCostFunc(schedule, makeTestParams());
    BOOST_CHECK_EQUAL(operatorCost(1618, 50), u256(0));
}

BOOST_AUTO_TEST_CASE(selectL1_preFjord_invokeThrows)
{
    OpStackForkSchedule const schedule{.fjordTime = 100, .isthmusTime = std::nullopt};
    auto const l1Cost = selectL1CostFunc(schedule, makeFix04Params());
    RollupCostData data{};
    data.fastLzSize = 235;

    BOOST_CHECK_THROW(l1Cost(data, 50), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(selectL1_emptyRollup_returnsZero)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const l1Cost = selectL1CostFunc(schedule, makeTestParams());
    RollupCostData const empty{};
    BOOST_CHECK_EQUAL(l1Cost(empty, 1), u256(0));
}

BOOST_AUTO_TEST_CASE(wireL1_matches_select_on_isthmus_plus)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const params = makeTestParams();
    auto const state = makeTestParamsState();
    RollupCostData data{};
    data.fastLzSize = 202;
    data.ones = 100;

    auto const selected = selectL1CostFunc(schedule, params);
    auto const wired = wireL1CostFuncWithState(schedule, state);
    BOOST_CHECK_EQUAL(wired(data, 1), selected(data, 1));
}

BOOST_AUTO_TEST_CASE(wireOperator_matches_select_on_isthmus_plus)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const params = makeTestParams();
    auto const state = makeTestParamsState();

    auto const selected = selectOperatorCostFunc(schedule, params);
    auto const wired = wireOperatorCostFuncWithState(schedule, state);
    BOOST_CHECK_EQUAL(wired(1618, 1), selected(1618, 1));
}

}  // namespace bcos::evm::test

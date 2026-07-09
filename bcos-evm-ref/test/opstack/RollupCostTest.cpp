#include <bcos-evm-ref/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <fstream>
#include <vector>

using namespace bcos::evmref::opstack;
using intx::operator""_u256;

namespace
{
std::vector<uint8_t> readFixture(const char* name)
{
    const std::string path = std::string(EVM_REF_OPSTACK_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "missing fixture: " << path;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

OpFeeParams feeParams(uint64_t l1Base, uint64_t blobBase, uint32_t baseScalar, uint32_t blobScalar,
    uint32_t opScalar = 0, uint64_t opConst = 0)
{
    return OpFeeParams{.l1_base_fee = intx::uint256{l1Base},
        .base_fee_scalar = baseScalar,
        .blob_base_fee_scalar = blobScalar,
        .blob_base_fee = intx::uint256{blobBase},
        .operator_fee_scalar = opScalar,
        .operator_fee_constant = opConst};
}

evmc::bytes_view view(const std::vector<uint8_t>& v)
{
    return {v.data(), v.size()};
}
}  // namespace

TEST(RollupCost, FlzCompressLenMatchesOpGethVectors)
{
    EXPECT_EQ(flzCompressLen({}), 0u);
    std::vector<uint8_t> ones(1000, 0x01);
    EXPECT_EQ(flzCompressLen(view(ones)), 21u);
    std::vector<uint8_t> zeros(1000, 0x00);
    EXPECT_EQ(flzCompressLen(view(zeros)), 21u);
    EXPECT_EQ(flzCompressLen(view(readFixture("empty_tx.bin"))), 31u);
    EXPECT_EQ(flzCompressLen(view(readFixture("contract_call_tx.bin"))), 202u);
}

TEST(RollupCost, EstimatedDaSizeFloorsToMinimum)
{
    EXPECT_EQ(estimatedDaSizeScaled(0), 100000000_u256);
    EXPECT_EQ(estimatedDaSizeScaled(64), 100000000_u256);
    EXPECT_EQ(estimatedDaSizeScaled(200), 124714400_u256);
}

TEST(RollupCost, EmptyEnvelopeIsZeroL1Cost)
{
    EXPECT_EQ(computeL1Cost(feeParams(1000000000, 10000000, 2, 3), {}), intx::uint256{0});
}

TEST(RollupCost, FjordL1CostEmptyTxMatches3203000)
{
    const auto env = readFixture("empty_tx.bin");
    EXPECT_EQ(computeL1Cost(feeParams(1000000000, 10000000, 2, 3), view(env)), 3203000_u256);
}

TEST(RollupCost, OperatorCostIsthmus)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    EXPECT_EQ(computeOperatorCost(p, 1000), intx::uint256{1000ull * 2000000 / 1000000 + 500});
}

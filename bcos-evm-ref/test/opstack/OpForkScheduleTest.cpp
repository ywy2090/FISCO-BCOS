#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <gtest/gtest.h>

using namespace bcos::evmref::opstack;

TEST(OpForkSchedule, IsthmusMapsToPrague)
{
    const auto& cfg = isthmusConfig();
    EXPECT_EQ(cfg.fork, OpFork::Isthmus);
    EXPECT_EQ(cfg.rev, EVMC_PRAGUE);
    EXPECT_TRUE(cfg.disable_prague_requests);
    EXPECT_TRUE(cfg.has_operator_fee);
}

TEST(OpForkSchedule, JovianAndKarstConfigs)
{
    const auto& j = jovianConfig();
    EXPECT_EQ(j.fork, OpFork::Jovian);
    EXPECT_EQ(j.rev, EVMC_PRAGUE);
    EXPECT_TRUE(j.has_operator_fee);
    EXPECT_TRUE(j.has_jovian_operator_formula);
    EXPECT_TRUE(j.has_da_footprint);
    EXPECT_TRUE(j.disable_prague_requests);
    EXPECT_NE(j.precompiles, nullptr);

    const auto& k = karstConfig();
    EXPECT_EQ(k.fork, OpFork::Karst);
    EXPECT_EQ(k.rev, j.rev);
    EXPECT_EQ(k.has_operator_fee, j.has_operator_fee);
    EXPECT_EQ(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    EXPECT_EQ(k.has_da_footprint, j.has_da_footprint);
    EXPECT_EQ(k.precompiles, j.precompiles);
}

TEST(OpForkSchedule, IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    EXPECT_FALSE(i.has_jovian_operator_formula);
    EXPECT_FALSE(i.has_da_footprint);
}

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

TEST(OpForkSchedule, PreIsthmusConfigsPinned)
{
    for (const auto* cfg : {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        EXPECT_EQ(cfg->rev, EVMC_CANCUN);
        EXPECT_EQ(cfg->precompiles, nullptr);  // pre-Isthmus 不复用 Isthmus 表
        EXPECT_TRUE(cfg->disable_prague_requests);
        EXPECT_FALSE(cfg->has_operator_fee);
        EXPECT_FALSE(cfg->has_jovian_operator_formula);
        EXPECT_FALSE(cfg->has_da_footprint);
    }
    EXPECT_EQ(ecotoneConfig().fork, OpFork::Ecotone);
    EXPECT_EQ(fjordConfig().fork, OpFork::Fjord);
    EXPECT_EQ(graniteConfig().fork, OpFork::Granite);
    EXPECT_EQ(holoceneConfig().fork, OpFork::Holocene);
    EXPECT_TRUE(ecotoneConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(fjordConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(graniteConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(holoceneConfig().has_ecotone_l1_formula);
}

TEST(OpForkSchedule, IsthmusPlusDisableEcotoneL1Formula)
{
    EXPECT_FALSE(isthmusConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(jovianConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(karstConfig().has_ecotone_l1_formula);
}

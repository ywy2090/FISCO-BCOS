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

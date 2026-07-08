#include <bcos-evm-ref/eth/EthTransition.h>
#include <gtest/gtest.h>
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>

TEST(Skeleton, HeadersAndLinkOk)
{
    evmone::test::TestState state;
    EXPECT_TRUE(state.empty());
}

#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <bcos-evm-ref/opstack/OpTransition.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

TEST(OpTransition, RoutesFeesToFourVaults)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 1000000,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto receipt = opTransition(ts, block, hashes, tx, isthmusConfig(), vm, props, fee, 1234);
    ASSERT_EQ(receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiff(ts, receipt.state_diff);

    EXPECT_GT(ts.at(OP_BASE_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
    EXPECT_GT(ts.at(OP_SEQUENCER_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_GT(ts.at(OP_OPERATOR_FEE_VAULT).balance, intx::uint256{0});
}

#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kFrom = 0x00000000000000000000000000000000000000cc_address;

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    return b;
}
}  // namespace

TEST(OpDeposit, SuccessMintsAndAdvancesNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.deposit_nonce, 5u);
    EXPECT_EQ(r.deposit_receipt_version, 1u);
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{100});
    EXPECT_EQ(ts.count(OP_L1_FEE_VAULT), 0u);
}

TEST(OpDeposit, EvmRevertKeepsMintAndChargesActualGas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kRevert = 0x00000000000000000000000000000000000000dd_address;
    ts[kRevert] = {.nonce = 0,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("60006000fd").value()};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kRevert,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_REVERT);
    EXPECT_LT(r.receipt.gas_used, 100000);
    EXPECT_GE(r.receipt.gas_used, 21000);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{100});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

TEST(OpDeposit, EntryFailureChargesFullGasLimitButKeepsMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{50},
        .value = intx::uint256{0},
        .gas_limit = 20999,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 20999);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{50});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

TEST(OpDeposit, SystemTxIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = true,
        .data = {}};
    EXPECT_THROW(runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234), std::runtime_error);
}

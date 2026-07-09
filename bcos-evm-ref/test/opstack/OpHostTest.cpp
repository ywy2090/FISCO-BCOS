#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <bcos-evm-ref/opstack/OpPrecompiles.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;

namespace
{
state::BlockInfo makeBlock()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = 0x4200000000000000000000000000000000000011_address;
    return b;
}
}  // namespace

TEST(OpHost, GetTxContextUsesConfiguredChainId)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = 0x00000000000000000000000000000000000000aa_address;
    OpHost host{EVMC_PRAGUE, vm, st, makeBlock(), hashes, tx, /*chainId=*/1234,
        &isthmusPrecompileOverrides()};
    const auto ctx = host.get_tx_context();
    EXPECT_EQ(intx::be::load<intx::uint256>(ctx.chain_id), intx::uint256{1234});
}

TEST(OpHost, GetTxContextGasPriceZeroForSystemCall)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    OpHost host{EVMC_PRAGUE, vm, st, makeBlock(), hashes, tx, 1234, &isthmusPrecompileOverrides()};
    EXPECT_EQ(intx::be::load<intx::uint256>(host.get_tx_context().tx_gas_price), intx::uint256{0});
}

TEST(OpHost, OverrideTableInterceptsP256)
{
    EXPECT_TRUE(
        isthmusPrecompileOverrides().contains(0x0000000000000000000000000000000000000100_address));
}

TEST(OpHost, CallToP256EmptyAccountIsNotSilentSuccess)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = 0x00000000000000000000000000000000000000aa_address;
    OpHost host{EVMC_PRAGUE, vm, st, makeBlock(), hashes, tx, 1234, &isthmusPrecompileOverrides()};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = 0x0000000000000000000000000000000000000100_address;
    msg.code_address = msg.recipient;
    msg.sender = tx.sender;
    msg.gas = 100000;
    const auto r = host.call(msg);
    EXPECT_LT(r.gas_left, msg.gas);
}

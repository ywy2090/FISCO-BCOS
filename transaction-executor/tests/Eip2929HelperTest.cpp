/*
 * Unit tests for Eip2929Util, Eip2929PrecompileWarm, and Eip2929TransactionPrewarm.
 */
#include "Eip2929TestHelpers.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip2929/Eip2929AccessState.h"
#include "bcos-evm/eth/eip2929/Eip2929PrecompileWarm.h"
#include "bcos-evm/eth/eip2929/Eip2929TransactionPrewarm.h"
#include "bcos-evm/eth/eip2929/Eip2929Util.h"
#include "bcos-executor/src/Common.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

using bcos::evm::Eip2929AccessState;
using bcos::executor::eip2929Enabled;
using bcos::executor::eip2929TransactionEntryWarmEnabled;
using bcos::evm::Eip2929TxPrewarmInput;
using bcos::executor::forEachActivePrecompileAddress;
using bcos::executor::warmEip2929AtTransactionEntry;
using bcos::executor::warmEip2930AccessListOnly;

namespace
{
evmc_address addrByte(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

evmc_address precompileAtLastByte(uint8_t lastByte)
{
    evmc_address a{};
    a.bytes[19] = lastByte;
    return a;
}

bool containsPrecompileLastByte(std::vector<evmc_address> const& addrs, uint8_t lastByte)
{
    auto const target = precompileAtLastByte(lastByte);
    return std::any_of(addrs.begin(), addrs.end(), [&](evmc_address const& a) {
        return std::memcmp(a.bytes, target.bytes, sizeof(a.bytes)) == 0;
    });
}

bool containsOsakaPrecompile(std::vector<evmc_address> const& addrs)
{
    return std::any_of(addrs.begin(), addrs.end(),
        [](evmc_address const& a) { return a.bytes[18] == 0x01 && a.bytes[19] == 0x00; });
}

bcos::evm_standard::RevisionConfig revWithEip2929On()
{
    bcos::evm_standard::RevisionConfig rev;
    rev.eip2929 = true;
    return rev;
}

bcos::evm_standard::RevisionConfig revWithEip2929Off()
{
    bcos::evm_standard::RevisionConfig rev;
    rev.eip2929 = false;
    return rev;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Eip2929Util)

BOOST_AUTO_TEST_CASE(enabled_requires_eip2929_revision_config)
{
    auto const revOn = revWithEip2929On();
    auto const revOff = revWithEip2929Off();
    BOOST_CHECK(!eip2929Enabled(revOff));
    BOOST_CHECK(eip2929Enabled(revOn));
}

BOOST_AUTO_TEST_CASE(transaction_entry_warm_gate)
{
    Eip2929AccessState state;
    auto const revOn = revWithEip2929On();
    auto const revOff = revWithEip2929Off();
    BOOST_CHECK(eip2929TransactionEntryWarmEnabled(0, revOn, &state));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(1, revOn, &state));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(0, revOn, nullptr));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(0, revOff, &state));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Eip2929PrecompileWarm)

BOOST_AUTO_TEST_CASE(berlin_includes_precompiles_1_through_9)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_BERLIN, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 9U);
    for (uint8_t i = 1; i <= 9; ++i)
    {
        BOOST_CHECK(containsPrecompileLastByte(addrs, i));
    }
    BOOST_CHECK(!containsPrecompileLastByte(addrs, 0x0a));
}

BOOST_AUTO_TEST_CASE(cancun_adds_point_evaluation_precompile)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_CANCUN, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 10U);
    BOOST_CHECK(containsPrecompileLastByte(addrs, 0x0a));
}

BOOST_AUTO_TEST_CASE(prague_adds_bls_precompile_range)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_PRAGUE, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 17U);
    for (uint8_t i = 0x0b; i <= 0x11; ++i)
    {
        BOOST_CHECK(containsPrecompileLastByte(addrs, i));
    }
}

BOOST_AUTO_TEST_CASE(osaka_adds_p256verify_precompile)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_OSAKA, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 18U);
    BOOST_CHECK(containsOsakaPrecompile(addrs));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Eip2929TransactionPrewarm)

BOOST_AUTO_TEST_CASE(warm_at_transaction_entry_covers_w1_create_coinbase_and_w2)
{
    Eip2929AccessState state;
    state.pushCheckpoint();

    evmc_address const listAddr = addrByte(0x55);
    evmc_bytes32 slot{};
    slot.bytes[31] = 0x09;
    bcos::executor::Eip2930AccessList accessList{{bcos::test::eip2929::addressFromEvmc(listAddr),
        {bcos::h256(slot.bytes, bcos::h256::SIZE)}}};

    Eip2929TxPrewarmInput input;
    input.revision = EVMC_CANCUN;
    input.origin = addrByte(0x01);
    input.callee = addrByte(0x02);
    input.createCodeAddress = addrByte(0x03);
    input.coinbase = addrByte(0x04);
    input.web3TypedTxKind = 2;
    input.accessList = &accessList;

    warmEip2929AtTransactionEntry(
        state, input, [](bcos::Address const& addr) { return bcos::toEvmC(addr); });

    state.rollbackCheckpoint();
    BOOST_CHECK(state.containsAddress(input.origin));
    BOOST_CHECK(state.containsAddress(*input.callee));
    BOOST_CHECK(state.containsAddress(*input.createCodeAddress));
    BOOST_CHECK(state.containsAddress(*input.coinbase));
    BOOST_CHECK(state.containsAddress(listAddr));
    BOOST_CHECK(state.containsStorage(listAddr, slot));
    BOOST_CHECK(state.containsAddress(precompileAtLastByte(1)));
    BOOST_CHECK(state.containsAddress(precompileAtLastByte(0x0a)));
}

BOOST_AUTO_TEST_CASE(create_entry_omits_callee_but_warms_code_address)
{
    Eip2929AccessState state;
    Eip2929TxPrewarmInput input;
    input.revision = EVMC_BERLIN;
    input.origin = addrByte(0x10);
    input.createCodeAddress = addrByte(0x20);

    warmEip2929AtTransactionEntry(state, input, [](bcos::Address const&) {
        BOOST_FAIL("access list converter must not run without W2 input");
        return addrByte(0);
    });

    BOOST_CHECK(state.containsAddress(input.origin));
    BOOST_CHECK(state.containsAddress(*input.createCodeAddress));
}

BOOST_AUTO_TEST_CASE(warm_eip2930_access_list_only_skips_legacy_kind)
{
    Eip2929AccessState state;
    bcos::executor::Eip2930AccessList list{
        {bcos::test::eip2929::addressFromEvmc(addrByte(0x77)), {}}};
    warmEip2930AccessListOnly(
        state, 0, list, [](bcos::Address const& addr) { return bcos::toEvmC(addr); });
    BOOST_CHECK(!state.containsAddress(addrByte(0x77)));

    warmEip2930AccessListOnly(
        state, 1, list, [](bcos::Address const& addr) { return bcos::toEvmC(addr); });
    BOOST_CHECK(state.containsAddress(addrByte(0x77)));
}

BOOST_AUTO_TEST_SUITE_END()

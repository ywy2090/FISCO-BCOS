#define BOOST_TEST_MODULE EipPrecompileRevisionGateTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>
#include <set>
#include <vector>

namespace bcos::evm::test
{
using namespace fixtures;

namespace
{
evmc_address precompileAddress(uint8_t lowByte, uint8_t highByte = 0x00)
{
    evmc_address addr{};
    addr.bytes[18] = highByte;
    addr.bytes[19] = lowByte;
    return addr;
}

bcos::evm_standard::RevisionConfig warmEnabledCfg(bcos::evm_standard::RevisionConfig cfg)
{
    cfg.warm_access = true;
    return cfg;
}

void warmPrecompilesOnly(state::State& state, bcos::evm_standard::RevisionConfig const& cfg)
{
    state::Transaction tx{};
    state::BlockInfo block{};
    state::TransactionProperties props{};
    props.warmDestination = false;
    props.warmCoinbase = false;
    execution::warmTransactionEntry(state, cfg, tx, block, props);
}

std::array<uint8_t, 20> toAddressKey(evmc_address const& addr)
{
    std::array<uint8_t, 20> key{};
    std::memcpy(key.data(), addr.bytes, sizeof(addr.bytes));
    return key;
}
}  // namespace

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_rejects_prague_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_prague_accepts_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = true};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_reads_eip2537_bool_not_revision)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = false};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_accepts_legacy_precompile)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const addr = precompileAddress(0x04);
    BOOST_CHECK(precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_p256_requires_osaka_and_eip7212)
{
    auto const addr = precompileAddress(0x00, 0x01);
    bcos::evm_standard::RevisionConfig pragueCfg{.revision = EVMC_PRAGUE};
    BOOST_CHECK(!precompiled::isActivePrecompile(pragueCfg, addr));

    bcos::evm_standard::RevisionConfig osakaOff{.revision = EVMC_OSAKA, .eip7212 = false};
    BOOST_CHECK(!precompiled::isActivePrecompile(osakaOff, addr));

    bcos::evm_standard::RevisionConfig osakaOn{.revision = EVMC_OSAKA, .eip7212 = true};
    BOOST_CHECK(precompiled::isActivePrecompile(osakaOn, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_shanghai_rejects_point_evaluation)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_SHANGHAI};
    auto const addr = precompileAddress(0x0a);
    BOOST_CHECK(!precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_accepts_point_evaluation)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const addr = precompileAddress(0x0a);
    BOOST_CHECK(precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(fisco_mask_bls_not_warmed_when_eip2537_off)
{
    bcos::evm_standard::RevisionConfig cfg =
        warmEnabledCfg({.revision = EVMC_PRAGUE, .eip2537 = false});
    auto const bls = precompileAddress(0x0b);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    warmPrecompilesOnly(state, cfg);

    BOOST_CHECK(!precompiled::isActivePrecompile(cfg, bls));
    BOOST_CHECK(!state.is_address_warm(bls));
}

BOOST_AUTO_TEST_CASE(fisco_mask_bls_warmed_when_eip2537_on)
{
    bcos::evm_standard::RevisionConfig cfg =
        warmEnabledCfg({.revision = EVMC_PRAGUE, .eip2537 = true});
    auto const bls = precompileAddress(0x0b);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    warmPrecompilesOnly(state, cfg);

    BOOST_CHECK(precompiled::isActivePrecompile(cfg, bls));
    BOOST_CHECK(state.is_address_warm(bls));
}

BOOST_AUTO_TEST_CASE(p256_not_warmed_when_eip7212_off)
{
    bcos::evm_standard::RevisionConfig cfg =
        warmEnabledCfg({.revision = EVMC_OSAKA, .eip7212 = false});
    auto const p256 = precompileAddress(0x00, 0x01);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    warmPrecompilesOnly(state, cfg);

    BOOST_CHECK(!precompiled::isActivePrecompile(cfg, p256));
    BOOST_CHECK(!state.is_address_warm(p256));
}

BOOST_AUTO_TEST_CASE(forEachActivePrecompile_matches_isActivePrecompile)
{
    bcos::evm_standard::RevisionConfig cfg =
        warmEnabledCfg(bcos::evm_standard::revisionConfigFromRevision(EVMC_PRAGUE));

    std::set<std::array<uint8_t, 20>> enumerated;
    precompiled::forEachActivePrecompile(
        cfg, [&enumerated](evmc_address const& addr) { enumerated.insert(toAddressKey(addr)); });

    std::vector<evmc_address> candidates;
    for (uint8_t i = 1; i <= 0x11; ++i)
    {
        candidates.push_back(precompileAddress(i));
    }
    candidates.push_back(precompileAddress(0x00, 0x01));
    candidates.push_back(precompileAddress(0x42));

    for (auto const& candidate : candidates)
    {
        auto const key = toAddressKey(candidate);
        BOOST_CHECK_EQUAL(
            precompiled::isActivePrecompile(cfg, candidate), enumerated.contains(key));
    }
}

BOOST_AUTO_TEST_CASE(cancun_call_0x0b_not_precompile_dispatch)
{
#ifdef ETH_STATE_FIXTURES_DIR
    auto const path = std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stBLS_add.json";
#else
    auto const path = std::filesystem::path("fixtures/state/imported/stBLS_add.json");
#endif
    auto fixture = loadFixture(path);
    state::test::InMemoryEvmStateReader view;
    for (auto const& [addr, acct] : fixture.preState)
        view.insert_account(addr, acct);

    int64_t const initialGas = fixture.tx.gasLimit;
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = initialGas;
    msg.sender = fixture.tx.from;
    msg.recipient = *fixture.tx.to;
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = &view;
    input.vm = &vm;
    input.message = msg;
    input.blockInfo = fixture.block;
    input.revisionConfig.revision = EVMC_CANCUN;

    auto output = executeMessage(input);
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.result.gas_left, initialGas);
    BOOST_CHECK_EQUAL(output.result.output_size, size_t(0));
}

}  // namespace bcos::evm::test

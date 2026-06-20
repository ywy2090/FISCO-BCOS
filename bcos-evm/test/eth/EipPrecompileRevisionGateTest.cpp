#define BOOST_TEST_MODULE EipPrecompileRevisionGateTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

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
}  // namespace

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_rejects_prague_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_CANCUN, cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_prague_accepts_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(precompiled::isActivePrecompile(EVMC_PRAGUE, cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_accepts_legacy_precompile)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const addr = precompileAddress(0x04);
    BOOST_CHECK(precompiled::isActivePrecompile(EVMC_CANCUN, cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_p256_requires_osaka_and_eip7212)
{
    auto const addr = precompileAddress(0x00, 0x01);
    bcos::evm_standard::RevisionConfig pragueCfg{.revision = EVMC_PRAGUE};
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_PRAGUE, pragueCfg, addr));

    bcos::evm_standard::RevisionConfig osakaOff{.revision = EVMC_OSAKA, .eip7212 = false};
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_OSAKA, osakaOff, addr));

    bcos::evm_standard::RevisionConfig osakaOn{.revision = EVMC_OSAKA, .eip7212 = true};
    BOOST_CHECK(precompiled::isActivePrecompile(EVMC_OSAKA, osakaOn, addr));
}

BOOST_AUTO_TEST_CASE(cancun_call_0x0b_not_precompile_dispatch)
{
#ifdef ETH_STATE_FIXTURES_DIR
    auto const path = std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stBLS_add.json";
#else
    auto const path = std::filesystem::path("fixtures/state/imported/stBLS_add.json");
#endif
    auto fixture = loadFixture(path);
    state::test::InMemoryStateView view;
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

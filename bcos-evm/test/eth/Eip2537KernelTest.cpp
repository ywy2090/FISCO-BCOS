#define BOOST_TEST_MODULE Eip2537KernelTest

#include "bcos-evm/eth/executeMessage.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

BOOST_AUTO_TEST_CASE(stBLS_add_precompile_0x0b_via_executeMessage)
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

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = fixture.tx.gasLimit;
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
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.eip2537 = true;

    auto output = executeMessage(input);
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    bcos::bytes actual(
        output.result.output_data, output.result.output_data + output.result.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output), "BLS output mismatch");
}

}  // namespace bcos::evm::test

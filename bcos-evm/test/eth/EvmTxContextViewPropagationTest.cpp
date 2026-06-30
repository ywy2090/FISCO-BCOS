#define BOOST_TEST_MODULE EvmTxContextViewPropagationTest

#include "bcos-evm/eth/core/ChainExtendedPrecompileDispatch.h"
#include "bcos-evm/eth/execution/EvmCallFrame.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/host/EthHost.hpp"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{

struct IdentityChainPort final : ChainExtendedPrecompileDispatch
{
    ChainExtendedPrecompileDispatch* topLevelPort{nullptr};
    ChainExtendedPrecompileDispatch* nestedPort{nullptr};

    std::optional<execution::CallTargetDescriptor> classifyTarget(state::State&,
        evmc_address const&, evmc_message const&, execution::FrameScope scope) override
    {
        if (scope == execution::FrameScope::TopLevel)
        {
            topLevelPort = this;
        }
        else
        {
            nestedPort = this;
        }
        return std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision, evmc_message const&) override
    {
        return std::nullopt;
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const&) const override {}
};

evmc_tx_context makeTxContext()
{
    evmc_tx_context ctx{};
    ctx.block_number = 1;
    ctx.block_gas_limit = 30'000'000;
    return ctx;
}

}  // namespace

BOOST_AUTO_TEST_CASE(eth_host_nested_call_shares_chain_port_pointer)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    IdentityChainPort port;
    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_CANCUN;

    state::EthHost host(state, makeTxContext(), cfg, vm, {}, nullptr, true, &port);

    evmc_message nested{};
    nested.kind = EVMC_CALL;
    nested.depth = 1;
    nested.gas = 100'000;
    nested.sender.bytes[19] = 0x01;
    nested.recipient.bytes[19] = 0x03;
    nested.code_address = nested.recipient;

    (void)host.call(nested);

    BOOST_REQUIRE(port.nestedPort != nullptr);
    BOOST_CHECK_EQUAL(port.nestedPort, &port);
}

BOOST_AUTO_TEST_CASE(top_level_frame_context_shares_chain_port_pointer)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    IdentityChainPort port;
    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_CANCUN;

    evmc_message top{};
    top.kind = EVMC_CALL;
    top.depth = 0;
    top.gas = 200'000;
    top.sender.bytes[19] = 0x01;
    top.recipient.bytes[19] = 0x02;
    top.code_address = top.recipient;

    state::EthHost host(state, makeTxContext(), cfg, vm, {}, nullptr, true, &port);
    execution::FrameExecutionEnv frameCtx{
        state, vm, cfg, nullptr, top.sender, host.execution_address_ref(), false, &port};
    (void)execution::runCallFrame(frameCtx, top, execution::FrameScope::TopLevel, host);

    BOOST_REQUIRE(port.topLevelPort != nullptr);
    BOOST_CHECK_EQUAL(port.topLevelPort, &port);
}

BOOST_AUTO_TEST_CASE(opstack_adapter_propagates_through_execute_message)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    OpStackChainCallTargetAdapter chainAdapter(&state, 0, makeIsthmusPlusForkSchedule(), 0);
    evmc::VM vm{evmc_create_evmone()};

    state.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);

    bytes calldata{0x09, 0x89, 0x99, 0xbe};
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    InnerExecuteInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.chainPort = &chainAdapter;

    auto output = innerExecute(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_REVERT);
    BOOST_CHECK(input.chainPort == &chainAdapter);
}

}  // namespace bcos::evm::test

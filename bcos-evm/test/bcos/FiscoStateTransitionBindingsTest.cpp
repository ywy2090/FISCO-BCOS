#define BOOST_TEST_MODULE FiscoStateTransitionBindingsTest

#include "bcos-evm/bcos/FiscoStateTransitionBindings.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
class MockAuthPort final : public AuthPort
{
public:
    std::optional<EVMCResult> checkAuth(evmc_message const&) override
    {
        evmc_result fail{};
        fail.status_code = EVMC_REJECTED;
        return EVMCResult(fail, protocol::TransactionStatus::PermissionDenied);
    }
    void createAuthTable(evmc_message const&, std::string_view) override {}
};
}  // namespace

BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623_when_web3_and_flag_enabled)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    message.gas = 100'000;

    FiscoMessageRequest input;
    input.web3Tx = true;
    input.revisionConfig.eth().eip7623 = true;

    FiscoMessageResult output;

    FiscoStateTransitionBindings::Context bindingsCtx{
        input, output, false, true /* eip7623Enabled */};

    auto policy = FiscoStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);
    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicGasMode::FloorDataGas));
}

BOOST_AUTO_TEST_CASE(pre_execute_auth_sets_early_exit)
{
    state::test::InMemoryStateView stateView;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    evmc_message message{};
    message.gas = 50'000;

    FiscoMessageRequest input;
    input.revisionConfig.enable_auth_check = true;
    MockAuthPort authPort;
    input.authPort = &authPort;

    FiscoMessageResult output;

    StateTransitionContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    FiscoStateTransitionBindings::Context bindingsCtx{input, output, false, false};
    auto policy = FiscoStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);
    policy.onPreCheckRules(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(StateTransitionExitKind::RulesRejected));
}

BOOST_AUTO_TEST_CASE(prepare_message_create_sets_recipient_for_legacy_tx)
{
    state::test::InMemoryStateView stateView;
    crypto::Keccak256 hashImpl;

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.sender.bytes[19] = 0xAA;

    FiscoMessageRequest input;
    input.web3Tx = false;
    input.hashImpl = &hashImpl;
    input.blockInfo.number = 42;
    input.contextID = 1;
    input.seq = 2;
    input.nonce = 0;

    FiscoMessageResult output;

    StateTransitionContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};

    FiscoStateTransitionBindings::Context bindingsCtx{input, output, false, false};
    auto policy = FiscoStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);
    policy.onNormalizeMessage(ctx);

    BOOST_CHECK(ctx.message.kind == EVMC_CREATE);
    BOOST_CHECK(std::memcmp(ctx.message.recipient.bytes, ctx.message.code_address.bytes,
                    sizeof(ctx.message.recipient.bytes)) == 0);
    bool allZero = true;
    for (auto b : ctx.message.recipient.bytes)
    {
        if (b != 0)
        {
            allZero = false;
            break;
        }
    }
    BOOST_CHECK(!allZero);
}

BOOST_AUTO_TEST_CASE(bind_wires_error_policy_from_bindings_context)
{
    crypto::Keccak256 hashImpl;
    FiscoMessageRequest input;
    input.hashImpl = &hashImpl;
    input.revisionConfig.fix_revert_logs = true;

    FiscoMessageResult output;

    FiscoStateTransitionBindings::Context bindingsCtx{input, output, true, false};
    auto bindings = FiscoStateTransitionBindings::bind(bindingsCtx);

    BOOST_CHECK(bindings.errorPolicy.fixErrorHandling);
    BOOST_CHECK(bindings.errorPolicy.fixRevertLogs);
    BOOST_CHECK_EQUAL(bindings.errorPolicy.hashImpl, &hashImpl);
}

}  // namespace bcos::evm::test

#define BOOST_TEST_MODULE FiscoOrchestrationProfileTest

#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/pipeline/DebitIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryEvmStateReader.h"
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
    state::test::InMemoryEvmStateReader stateView;
    evmc_message message{};
    message.gas = 100'000;

    FiscoExecutionRequest input;
    input.web3Tx = true;
    input.revisionConfig.eth().eip7623 = true;

    FiscoExecutionResult output;

    FiscoOrchestrationProfile::BindingsContext bindingsCtx{
        input, output, false, true /* eip7623Enabled */};

    auto policy = FiscoOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);
    BOOST_CHECK_EQUAL(static_cast<int>(policy.intrinsicGasPolicy().mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(pre_execute_auth_sets_early_exit)
{
    state::test::InMemoryEvmStateReader stateView;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    evmc_message message{};
    message.gas = 50'000;

    FiscoExecutionRequest input;
    input.revisionConfig.enable_auth_check = true;
    MockAuthPort authPort;
    input.authPort = &authPort;

    FiscoExecutionResult output;

    TxPipelineContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    FiscoOrchestrationProfile::BindingsContext bindingsCtx{input, output, false, false};
    auto policy = FiscoOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);
    policy.checkTransactionRules(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::RulesRejected));
}

BOOST_AUTO_TEST_CASE(prepare_message_create_sets_recipient_for_legacy_tx)
{
    state::test::InMemoryEvmStateReader stateView;
    crypto::Keccak256 hashImpl;

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.sender.bytes[19] = 0xAA;

    FiscoExecutionRequest input;
    input.web3Tx = false;
    input.hashImpl = &hashImpl;
    input.blockInfo.number = 42;
    input.contextID = 1;
    input.seq = 2;
    input.nonce = 0;

    FiscoExecutionResult output;

    TxPipelineContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};

    FiscoOrchestrationProfile::BindingsContext bindingsCtx{input, output, false, false};
    auto policy = FiscoOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);
    policy.setupMessage(ctx);

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
    FiscoExecutionRequest input;
    input.hashImpl = &hashImpl;
    input.revisionConfig.fix_revert_logs = true;

    FiscoExecutionResult output;

    FiscoOrchestrationProfile::BindingsContext bindingsCtx{input, output, true, false};
    auto bindings = FiscoOrchestrationProfile::bind(bindingsCtx);

    BOOST_CHECK(bindings.errorPolicy.fixErrorHandling);
    BOOST_CHECK(bindings.errorPolicy.fixRevertLogs);
    BOOST_CHECK_EQUAL(bindings.errorPolicy.hashImpl, &hashImpl);
}

}  // namespace bcos::evm::test

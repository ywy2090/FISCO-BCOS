#define BOOST_TEST_MODULE FiscoPipelineHookBinderTest

#include "bcos-evm/bcos/FiscoPipelineHookBinder.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/OrchestrationContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryStateView.h"
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

    FiscoExecutionRequest input;
    input.web3Tx = true;
    input.revisionConfig.eth().eip7623 = true;

    FiscoExecutionResult output;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = nullptr;  // hook test 不执行 extension
    FiscoVmHostPolicy extension(false, std::move(deps));

    FiscoPipelineHookBinder::HookBindingContext session{
        input, output, extension, false, true /* eip7623Enabled */};

    auto hooks = FiscoPipelineHookBinder::buildHooks(session);
    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(pre_execute_auth_sets_early_exit)
{
    state::test::InMemoryStateView stateView;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    evmc_message message{};
    message.gas = 50'000;

    FiscoExecutionRequest input;
    input.revisionConfig.enable_auth_check = true;
    MockAuthPort authPort;
    input.authPort = &authPort;

    FiscoExecutionResult output;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = nullptr;
    FiscoVmHostPolicy extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    FiscoPipelineHookBinder::HookBindingContext session{input, output, extension, false, false};
    auto hooks = FiscoPipelineHookBinder::buildHooks(session);
    hooks.preExecute(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.exitKind),
        static_cast<int>(OrchestrationExitKind::PreExecuteRejected));
}

BOOST_AUTO_TEST_CASE(prepare_message_create_sets_recipient_for_legacy_tx)
{
    state::test::InMemoryStateView stateView;
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
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = nullptr;
    FiscoVmHostPolicy extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};

    FiscoPipelineHookBinder::HookBindingContext session{input, output, extension, false, false};
    auto hooks = FiscoPipelineHookBinder::buildHooks(session);
    hooks.prepareMessage(ctx);

    BOOST_CHECK(ctx.message.kind == EVMC_CREATE);
    BOOST_CHECK(std::memcmp(ctx.message.recipient.bytes, ctx.message.code_address.bytes,
                    sizeof(ctx.message.recipient.bytes)) == 0);
    // legacy: recipient 非全零（deriveMessage 已运行）
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

}  // namespace bcos::evm::test

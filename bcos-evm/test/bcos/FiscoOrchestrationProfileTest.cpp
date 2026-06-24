#define BOOST_TEST_MODULE FiscoOrchestrationProfileTest

#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoHostExtension.h"
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

    ExecuteViaHostInput input;
    input.web3Tx = true;
    input.revisionConfig.eth().eip7623 = true;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;  // hook test 不执行 extension
    FiscoHostExtension extension(false, std::move(deps));

    FiscoOrchestrationProfile::Session session{
        input, output, extension, false, true /* eip7623Enabled */};

    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
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

    ExecuteViaHostInput input;
    input.revisionConfig.enable_auth_check = true;
    MockAuthPort authPort;
    input.authPort = &authPort;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;
    FiscoHostExtension extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    FiscoOrchestrationProfile::Session session{input, output, extension, false, false};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
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

    ExecuteViaHostInput input;
    input.web3Tx = false;
    input.hashImpl = &hashImpl;
    input.blockInfo.number = 42;
    input.contextID = 1;
    input.seq = 2;
    input.nonce = 0;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;
    FiscoHostExtension extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};

    FiscoOrchestrationProfile::Session session{input, output, extension, false, false};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
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

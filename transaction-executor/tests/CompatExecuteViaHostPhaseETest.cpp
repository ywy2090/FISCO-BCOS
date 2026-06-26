/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE Phase E: address routing, FiscoPolicy ladder, precompile feature gate.
 *  @file CompatExecuteViaHostPhaseETest.cpp
 */

#include "../../bcos-executor/test/unittest/evmone/compat/CompatTestFixture.h"
#include "ExecuteViaHostEip2929Harness.h"
#include "SelfdestructCompatBytecode.h"
#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos/adapters/InMemoryChainPrecompileAdapter.h"
#include "helpers/InMemoryEvmStateReader.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledManager.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
using compat::CompatFeatureProfile;

evmc_address addressFromSuffix(uint64_t suffix)
{
    evmc_address address{};
    for (int i = 19; i >= 0 && suffix > 0; --i)
    {
        address.bytes[i] = static_cast<uint8_t>(suffix & 0xff);
        suffix >>= 8U;
    }
    return address;
}

bcostars::protocol::BlockHeaderImpl makeHeader(uint32_t version)
{
    auto holder = std::make_shared<bcostars::BlockHeader>();
    bcostars::protocol::BlockHeaderImpl header([holder]() { return holder.get(); });
    header.setVersion(version);
    header.setNumber(1);
    header.calculateHash(crypto::Keccak256{});
    return header;
}

bcos::chain_policy::FiscoRevisionConfig revisionConfigFrom(
    ledger::Features const& features, uint32_t blockVersion)
{
    bcos::chain_policy::FiscoPolicy policy(features, false, false);
    return policy.computeRevisionConfig(makeHeader(blockVersion));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CompatExecuteViaHostPhaseE)

BOOST_AUTO_TEST_CASE(TE_FC_E_P_address_routing_prefix_overlap)
{
    namespace addr = compat::compat_addr;
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bcos::evm::PrecompiledManager manager(executor::GlobalHashImpl::g_hashImpl);

    BOOST_CHECK(bcos::evm::isBLSPrecompileAddress(addr::BLS_G1ADD));
    BOOST_CHECK(bcos::evm::isP256verifyPrecompileAddress(addr::P256VERIFY));
    BOOST_CHECK(!bcos::evm::isBLSPrecompileAddress(addr::SYSCONFIG));
    BOOST_CHECK(!bcos::evm::isP256verifyPrecompileAddress(addr::SYSCONFIG));

    auto const bls = addressFromSuffix(0x0b);
    auto const p256 = addressFromSuffix(0x0100);
    auto const sys = addressFromSuffix(0x1000);

    auto cancunFeatures = CompatFeatureProfile::cancunOnly();
    cancunFeatures.set(ledger::Features::Flag::bugfix_precompiled_feature_gate);
    auto pragueFeatures = CompatFeatureProfile::pragueEnabled();
    pragueFeatures.set(ledger::Features::Flag::bugfix_precompiled_feature_gate);

    auto const cancunRev = revisionConfigFrom(
        cancunFeatures, static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
    auto const pragueRev = revisionConfigFrom(
        pragueFeatures, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    BOOST_REQUIRE(manager.getPrecompiled(bls) != nullptr);
    BOOST_REQUIRE(manager.getPrecompiled(p256) != nullptr);
    BOOST_REQUIRE(manager.getPrecompiled(sys) != nullptr);

    BOOST_CHECK(manager.getPrecompiled(bls, cancunRev, cancunFeatures) == nullptr);
    BOOST_CHECK(manager.getPrecompiled(p256, pragueRev, pragueFeatures) == nullptr);
    BOOST_CHECK(manager.getPrecompiled(sys, cancunRev, cancunFeatures) != nullptr);

    bool blsCallbackCalled = false;
    bool sysCallbackCalled = false;
    auto callback = [&](evmc_revision /*rev*/,
                        evmc_message const& message) -> std::optional<evmc_result> {
        if (std::memcmp(message.recipient.bytes, bls.bytes, sizeof(bls.bytes)) == 0)
        {
            blsCallbackCalled = true;
            return std::nullopt;
        }
        if (std::memcmp(message.recipient.bytes, sys.bytes, sizeof(sys.bytes)) == 0)
        {
            sysCallbackCalled = true;
            evmc_result result{};
            result.status_code = EVMC_SUCCESS;
            result.gas_left = message.gas;
            return result;
        }
        return std::nullopt;
    };
    bcos::evm::test::InMemoryChainPrecompileAdapter port(std::move(callback));
    bcos::evm::state::test::InMemoryEvmStateReader baseView;
    bcos::evm::state::State state(baseView);
    bcos::evm::FiscoChainCallTargetAdapter adapter(state, port);

    evmc_message blsMsg{};
    blsMsg.kind = EVMC_CALL;
    blsMsg.gas = 50'000;
    blsMsg.recipient = bls;
    blsMsg.code_address = bls;
    BOOST_CHECK(
        !adapter.classifyTarget(state, bls, blsMsg, bcos::evm::execution::FrameScope::TopLevel)
             .has_value());
    BOOST_CHECK(!adapter.dispatch(EVMC_CANCUN, blsMsg).has_value());
    BOOST_CHECK(!blsCallbackCalled);

    evmc_message sysMsg{};
    sysMsg.kind = EVMC_CALL;
    sysMsg.gas = 50'000;
    sysMsg.recipient = sys;
    sysMsg.code_address = sys;
    BOOST_REQUIRE(
        adapter.classifyTarget(state, sys, sysMsg, bcos::evm::execution::FrameScope::TopLevel)
            .has_value());
    auto sysResult = adapter.dispatch(EVMC_CANCUN, sysMsg);
    BOOST_REQUIRE(sysResult.has_value());
    BOOST_CHECK(sysCallbackCalled);
    BOOST_CHECK_EQUAL(sysResult->status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(TE_FC_E_R_fisco_policy_schedule_ladder)
{
    BOOST_TEST_MESSAGE(
        "FC-R: legacy BlockContext vmSchedule.enablePairs maps to V3_2 PARIS pre-floor; "
        "FiscoPolicy floors eth().revision at EVMC_CANCUN.");

    auto const londonV30 = revisionConfigFrom(CompatFeatureProfile::legacyLondon(),
        static_cast<uint32_t>(protocol::BlockVersion::V3_0_VERSION));
    BOOST_CHECK_EQUAL(londonV30.eth().revision, EVMC_CANCUN);
    BOOST_CHECK(!londonV30.eth().eip2537);
    BOOST_CHECK(!londonV30.eth().eip7212);

    auto const londonV32 = revisionConfigFrom(CompatFeatureProfile::legacyLondon(),
        static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
    BOOST_CHECK_EQUAL(londonV32.eth().revision, EVMC_CANCUN);
    BOOST_CHECK(!londonV32.eth().eip2537);

    auto const cancunV32 = revisionConfigFrom(CompatFeatureProfile::cancunOnly(),
        static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
    BOOST_CHECK_EQUAL(cancunV32.eth().revision, EVMC_CANCUN);
    BOOST_CHECK(cancunV32.eth().eip5656);
    BOOST_CHECK(!cancunV32.eth().eip2537);

    auto const pragueMax = revisionConfigFrom(CompatFeatureProfile::pragueEnabled(),
        static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
    BOOST_CHECK_EQUAL(pragueMax.eth().revision, EVMC_PRAGUE);
    BOOST_CHECK(pragueMax.eth().eip2537);
    BOOST_CHECK(!pragueMax.eth().eip7212);

    auto const osakaMax = revisionConfigFrom(CompatFeatureProfile::osakaEnabled(),
        static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
    BOOST_CHECK_EQUAL(osakaMax.eth().revision, EVMC_OSAKA);
    BOOST_CHECK(osakaMax.eth().eip7212);
}

BOOST_AUTO_TEST_CASE(TE_FC_E_P_bls_gated_without_prague)
{
    namespace addr = compat::compat_addr;
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bcos::evm::PrecompiledManager manager(executor::GlobalHashImpl::g_hashImpl);

    auto features = CompatFeatureProfile::cancunOnly();
    features.set(ledger::Features::Flag::bugfix_precompiled_feature_gate);
    auto const rev =
        revisionConfigFrom(features, static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));

    auto const* gated = manager.getPrecompiled(addressFromSuffix(0x0b), rev, features);
    BOOST_CHECK(gated == nullptr);

    features.set(ledger::Features::Flag::feature_evm_prague);
    auto const pragueRev =
        revisionConfigFrom(features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
    auto const* enabled = manager.getPrecompiled(addressFromSuffix(0x0b), pragueRev, features);
    BOOST_REQUIRE(enabled != nullptr);
}

BOOST_AUTO_TEST_CASE(TE_FC_E_P_p256_gated_without_osaka)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bcos::evm::PrecompiledManager manager(executor::GlobalHashImpl::g_hashImpl);

    auto features = CompatFeatureProfile::pragueEnabled();
    features.set(ledger::Features::Flag::bugfix_precompiled_feature_gate);
    auto const rev =
        revisionConfigFrom(features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    BOOST_CHECK(manager.getPrecompiled(addressFromSuffix(0x0100), rev, features) == nullptr);

    features.set(ledger::Features::Flag::feature_evm_osaka);
    auto const osakaRev =
        revisionConfigFrom(features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
    BOOST_REQUIRE(manager.getPrecompiled(addressFromSuffix(0x0100), osakaRev, features) != nullptr);
}

BOOST_AUTO_TEST_CASE(TE_FC_E_S_bls_without_prague_via_execute_via_host)
{
    Eip2929ExecuteViaHostFixture fixture;
    auto features = CompatFeatureProfile::cancunOnly();
    features.set(ledger::Features::Flag::bugfix_precompiled_feature_gate);
    fixture.ledgerConfig.setFeatures(features);

    evmc_address sender{};
    sender.bytes[19] = 0x01;
    evmc_address bls = addressFromSuffix(0x0b);
    fixture.fund(sender);

    Eip2929ExecuteViaHostFixture::CompatHostShim shim(
        fixture, fixture.revisionFromFeatures(features), sender, bls, EVMC_CALL);
    shim.mutableMessage().input_data = nullptr;
    shim.mutableMessage().input_size = 0;
    bytes input(256, 0);
    shim.mutableMessage().input_data = input.data();
    shim.mutableMessage().input_size = input.size();

    task::syncWait([&]() -> task::Task<void> {
        co_await shim.prepare();
        auto result = co_await shim.execute();
        BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
        BOOST_TEST_MESSAGE(
            "Fisco gate: fiscoExecute dispatches BLS via EthPrecompiles when code is empty; "
            "PrecompiledManager gate applies only to >=0x1000 FiscoChainCallTargetAdapter path.");
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_same_tx_create_destroy_eth_reference)
{
    namespace sd = bcos::evm::test::selfdestruct_compat;

    Eip2929ExecuteViaHostFixture fixture;
    auto features = CompatFeatureProfile::pragueEnabled();
    fixture.ledgerConfig.setFeatures(features);
    auto const pragueRev =
        revisionConfigFrom(features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    evmc_address sender{};
    sender.bytes[19] = 0x01;
    fixture.fund(sender, bcos::u256(0x1000000));

    bytes initCode;
    boost::algorithm::unhex(sd::kSelfdestructInitCode, std::back_inserter(initCode));

    Eip2929ExecuteViaHostFixture::CompatHostShim shim(
        fixture, pragueRev, sender, evmc_address{}, EVMC_CREATE, nullptr, 0, 2'000'000);
    shim.mutableMessage().input_data = initCode.data();
    shim.mutableMessage().input_size = initCode.size();

    task::syncWait([&]() -> task::Task<void> {
        co_await shim.prepare();
        auto const created = shim.mutableMessage().recipient;
        auto result = co_await shim.execute();

        BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);

        auto const post = fixture.stateView.get_account(created);
        bool const destroyed = !post.has_value() || post->code.empty();
        BOOST_CHECK_MESSAGE(destroyed,
            "SD-C Eth reference: same-tx CREATE+init SELFDESTRUCT should destroy contract "
            "(EIP-6780 exception); FISCO path retains code via allowSelfdestruct=false.");
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

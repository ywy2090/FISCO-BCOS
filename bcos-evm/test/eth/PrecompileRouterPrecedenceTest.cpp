/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router precedence tests.
 */

#define BOOST_TEST_MODULE PrecompileRouterPrecedenceTest

#include "bcos-evm/eth/precompiled/EthPrecompiles.hpp"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <optional>

namespace bcos::evm::test
{
namespace
{
evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}

class ChainFirstExtension : public state::VmHostPolicy
{
public:
    std::optional<evmc_result> tryChainPrecompile(
        evmc_revision /*revision*/, evmc_message const& /*message*/) override
    {
        evmc_result r{};
        r.status_code = EVMC_SUCCESS;
        r.gas_left = 1000;
        static uint8_t out[] = {0xca, 0xfe};
        r.output_data = out;
        r.output_size = sizeof(out);
        return r;
    }
};

using Outcome = bcos::evm::precompiled::PrecompileDispatchOutcome;

bcos::evm::precompiled::PrecompileRouterOutput dispatchBuiltinOnly(state::State& state,
    bcos::evm_standard::RevisionConfig const& revision, evmc_message const& message,
    evmc_address const& target)
{
    return bcos::evm::precompiled::dispatchPrecompile({.state = state,
        .revision = revision,
        .extension = nullptr,
        .message = message,
        .target = target,
        .skipValueTransfer = false});
}
}  // namespace

BOOST_AUTO_TEST_CASE(chain_precompile_wins_over_active_builtin_with_empty_code)
{
    auto const sender = precompileAddress(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> identityInput{0xde, 0xad, 0xbe, 0xef};

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);
    state.set_balance(sender, 1'000'000);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.input_data = identityInput.data();
    message.input_size = identityInput.size();

    bcos::evm_standard::RevisionConfig revision;
    revision.revision = EVMC_PRAGUE;
    ChainFirstExtension extension;

    auto output = precompiled::dispatchPrecompile({.state = state,
        .revision = revision,
        .extension = &extension,
        .message = message,
        .target = identity,
        .skipValueTransfer = false});

    BOOST_REQUIRE_EQUAL(static_cast<int>(output.outcome),
        static_cast<int>(precompiled::PrecompileDispatchOutcome::Dispatched));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.result.gas_left, 1000);
    BOOST_REQUIRE_EQUAL(output.result.output_size, 2);
    BOOST_REQUIRE(output.result.output_data != nullptr);
    BOOST_CHECK_EQUAL(output.result.output_data[0], 0xca);
    BOOST_CHECK_EQUAL(output.result.output_data[1], 0xfe);
    BOOST_CHECK_NE(output.result.output_data[0], identityInput[0]);
}

BOOST_AUTO_TEST_CASE(inactive_bls_at_cancun_router_empty_account_success)
{
    auto const sender = precompileAddress(0x01);
    auto const bls = precompileAddress(0x0b);
    std::array<uint8_t, 256> input{};

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = bls;
    message.code_address = bls;
    message.input_data = input.data();
    message.input_size = input.size();

    bcos::evm_standard::RevisionConfig revision{.revision = EVMC_CANCUN};
    BOOST_REQUIRE(!bcos::evm::precompiled::isActivePrecompile(revision, bls));

    auto output = dispatchBuiltinOnly(state, revision, message, bls);

    BOOST_REQUIRE_EQUAL(
        static_cast<int>(output.outcome), static_cast<int>(Outcome::EmptyAccountSuccess));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.result.gas_left, message.gas);
    BOOST_REQUIRE_EQUAL(output.result.output_size, 0u);
}

BOOST_AUTO_TEST_CASE(inactive_point_evaluation_at_shanghai_router_empty_account_success)
{
    auto const sender = precompileAddress(0x01);
    auto const pointEval = precompileAddress(0x0a);

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = pointEval;
    message.code_address = pointEval;

    bcos::evm_standard::RevisionConfig revision{.revision = EVMC_SHANGHAI};
    BOOST_REQUIRE(!bcos::evm::precompiled::isActivePrecompile(revision, pointEval));

    auto output = dispatchBuiltinOnly(state, revision, message, pointEval);

    BOOST_REQUIRE_EQUAL(
        static_cast<int>(output.outcome), static_cast<int>(Outcome::EmptyAccountSuccess));
    BOOST_REQUIRE_EQUAL(output.result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(inactive_p256_at_prague_router_empty_account_success)
{
    evmc_address p256{};
    p256.bytes[18] = 0x01;
    p256.bytes[19] = 0x00;

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = precompileAddress(0x01);
    message.recipient = p256;
    message.code_address = p256;

    bcos::evm_standard::RevisionConfig revision{.revision = EVMC_PRAGUE};
    BOOST_REQUIRE(!bcos::evm::precompiled::isActivePrecompile(revision, p256));

    auto output = dispatchBuiltinOnly(state, revision, message, p256);

    BOOST_REQUIRE_EQUAL(
        static_cast<int>(output.outcome), static_cast<int>(Outcome::EmptyAccountSuccess));
    BOOST_REQUIRE_EQUAL(output.result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(active_identity_at_cancun_router_dispatches_builtin)
{
    auto const sender = precompileAddress(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> identityInput{0xde, 0xad, 0xbe, 0xef};

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.input_data = identityInput.data();
    message.input_size = identityInput.size();

    bcos::evm_standard::RevisionConfig revision{.revision = EVMC_CANCUN};
    BOOST_REQUIRE(bcos::evm::precompiled::isActivePrecompile(revision, identity));

    auto output = dispatchBuiltinOnly(state, revision, message, identity);

    BOOST_REQUIRE_EQUAL(static_cast<int>(output.outcome), static_cast<int>(Outcome::Dispatched));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.result.output_size, identityInput.size());
    BOOST_REQUIRE(output.result.output_data != nullptr);
    BOOST_CHECK_EQUAL(output.result.output_data[0], identityInput[0]);
    BOOST_CHECK_LT(output.result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(inactive_bls_router_skips_dispatch_but_execute_module_has_no_gate)
{
    auto const bls = precompileAddress(0x0b);
    bcos::evm_standard::RevisionConfig revision{.revision = EVMC_CANCUN};
    BOOST_REQUIRE(!bcos::evm::precompiled::isActivePrecompile(revision, bls));

    bcos::bytes input(64, 0);
    auto direct = bcos::evm::precompiled::EthPrecompiles::dispatch(
        bls, bcos::bytesConstRef(input.data(), input.size()), 500'000, EVMC_CANCUN, revision);
    BOOST_REQUIRE(direct.has_value());
    BOOST_CHECK_EQUAL(direct->status, EVMC_PRECOMPILE_FAILURE);

    input.assign(256, 0);

    state::test::InMemoryEvmStateReader baseView;
    state::State state(baseView);
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = precompileAddress(0x01);
    message.recipient = bls;
    message.code_address = bls;
    message.input_data = input.data();
    message.input_size = input.size();

    auto routed = dispatchBuiltinOnly(state, revision, message, bls);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(routed.outcome), static_cast<int>(Outcome::EmptyAccountSuccess));
    BOOST_REQUIRE_EQUAL(routed.result.gas_left, message.gas);
}

}  // namespace bcos::evm::test

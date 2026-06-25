/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router precedence tests.
 */

#define BOOST_TEST_MODULE PrecompileRouterPrecedenceTest

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

}  // namespace bcos::evm::test

/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FrameRouting → ExecutionFrame routing characterization (M1 matrix).
 */

#define BOOST_TEST_MODULE FrameTargetRoutingCharacterizationTest

#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::test
{
enum class RoutingCallMode
{
    Call,
    StaticCall,
};

namespace
{
struct CallOutcome
{
    evmc_status_code status{};
    int64_t gasLeft{};
    bool precompileHit{false};
};

struct DenyDelegatePrecompilePolicy : state::EvmHostHooks
{
    bool allowDelegateCallToPrecompile() override { return false; }
};

struct FrameTestHost
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm::RevisionConfig cfg{};
    std::optional<state::EthHost> host;

    explicit FrameTestHost(state::State& state, bcos::evm::RevisionConfig revisionCfg)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = revisionCfg;
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, nullptr);
    }

    state::EthHost& ethHost() { return *host; }
};

evmc_address precompileWithHigh(uint8_t lowByte, uint8_t highByte = 0x00)
{
    evmc_address addr{};
    addr.bytes[18] = highByte;
    addr.bytes[19] = lowByte;
    return addr;
}

bcos::evm::RevisionConfig cfgForSuffix(uint8_t lowByte, uint8_t highByte)
{
    bcos::evm::RevisionConfig cfg{.eip2929 = true, .eip7702 = true};
    if (highByte == 0x01 && lowByte == 0x00)
    {
        cfg.revision = EVMC_OSAKA;
        cfg.eip7212 = true;
        return cfg;
    }
    if (lowByte >= 0x0b && lowByte <= 0x11)
    {
        cfg.revision = EVMC_PRAGUE;
        cfg.eip2537 = true;
        return cfg;
    }
    if (lowByte == 0x0a)
    {
        cfg.revision = EVMC_CANCUN;
        return cfg;
    }
    cfg.revision = EVMC_PRAGUE;
    return cfg;
}

CallOutcome runFrame(state::State& state, bcos::evm::RevisionConfig const& cfg,
    evmc_message message, execution::FrameScope scope, state::EvmHostHooks* extension = nullptr)
{
    FrameTestHost fixture(state, cfg);
    if (scope == execution::FrameScope::Nested)
    {
        message.depth = 1;
    }
    execution::CallFrameContext frameCtx{state, fixture.vm, fixture.cfg, extension,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runCallFrame(frameCtx, message, scope, fixture.ethHost());
    return {.status = fr.result.status_code,
        .gasLeft = fr.result.gas_left,
        .precompileHit = fr.precompileHit};
}

void applyCallMode(evmc_message& message, RoutingCallMode mode)
{
    message.kind = EVMC_CALL;
    message.flags = mode == RoutingCallMode::StaticCall ? EVMC_STATIC : 0;
}

evmc_message makePlainPrecompileMessage(RoutingCallMode mode, evmc_address sender,
    evmc_address precompile, std::array<uint8_t, 4> const& inputBytes)
{
    evmc_message message{};
    applyCallMode(message, mode);
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = precompile;
    message.code_address = precompile;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();
    return message;
}

evmc_message makeDelegated7702PrecompileMessage(RoutingCallMode mode, evmc_address sender,
    evmc_address authority, evmc_address codeHint, std::array<uint8_t, 4> const& inputBytes)
{
    evmc_message message{};
    applyCallMode(message, mode);
    message.flags |= EVMC_DELEGATED;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = authority;
    message.code_address = codeHint;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();
    return message;
}

const std::vector<uint8_t> kSuffixLowBytes = {0x01, 0x04, 0x0a, 0x0b, 0x00};
const std::vector<uint8_t> kSuffixHighBytes = {0x00, 0x00, 0x00, 0x00, 0x01};

const std::vector<bool> kNestedScopes = {false, true};

const std::vector<RoutingCallMode> kRoutingCallModes = {
    RoutingCallMode::Call, RoutingCallMode::StaticCall};

execution::FrameScope scopeFromNestedFlag(bool nested)
{
    return nested ? execution::FrameScope::Nested : execution::FrameScope::TopLevel;
}
}  // namespace

BOOST_AUTO_TEST_CASE(plain_precompile_routing_matrix)
{
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const sender = addressFromLastByte(0x01);

    for (size_t i = 0; i < kSuffixLowBytes.size(); ++i)
    {
        auto const lowByte = kSuffixLowBytes[i];
        auto const highByte = kSuffixHighBytes[i];
        auto const precompile = precompileWithHigh(lowByte, highByte);
        auto const cfg = cfgForSuffix(lowByte, highByte);

        for (bool nested : kNestedScopes)
        {
            auto const scope = scopeFromNestedFlag(nested);
            for (auto callMode : kRoutingCallModes)
            {
                state::test::InMemoryStateView view;
                state::State state(view);
                state.set_balance(sender, 1'000'000);
                auto message = makePlainPrecompileMessage(callMode, sender, precompile, inputBytes);
                auto const outcome = runFrame(state, cfg, message, scope);
                BOOST_REQUIRE(outcome.precompileHit);
                if (lowByte == 0x04 && highByte == 0x00)
                {
                    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(delegated7702_precompile_routing_matrix)
{
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const sender = addressFromLastByte(0x01);

    for (size_t i = 0; i < kSuffixLowBytes.size(); ++i)
    {
        auto const lowByte = kSuffixLowBytes[i];
        auto const highByte = kSuffixHighBytes[i];
        auto const precompile = precompileWithHigh(lowByte, highByte);
        auto const cfg = cfgForSuffix(lowByte, highByte);

        for (bool nested : kNestedScopes)
        {
            auto const scope = scopeFromNestedFlag(nested);
            for (auto callMode : kRoutingCallModes)
            {
                evmc_message message{};
                if (callMode == RoutingCallMode::Call)
                {
                    message = makeDelegated7702PrecompileMessage(
                        callMode, sender, precompile, precompile, inputBytes);
                }
                else
                {
                    message = makeDelegated7702PrecompileMessage(
                        callMode, sender, addressFromLastByte(0x02), precompile, inputBytes);
                }

                state::test::InMemoryStateView view;
                state::State state(view);
                state.set_balance(sender, 1'000'000);
                auto const outcome = runFrame(state, cfg, message, scope);
                BOOST_REQUIRE(outcome.precompileHit);
                if (lowByte == 0x04 && highByte == 0x00 && callMode == RoutingCallMode::Call)
                {
                    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_SUCCESS);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(delegated_delegatecall_precompile_policy_rejected_top_level)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    DenyDelegatePrecompilePolicy policy;
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto const outcome = runFrame(state, cfg, message, execution::FrameScope::TopLevel, &policy);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE(!outcome.precompileHit);
}

BOOST_AUTO_TEST_CASE(delegated_delegatecall_precompile_policy_rejected_nested)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    DenyDelegatePrecompilePolicy policy;
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto const outcome = runFrame(state, cfg, message, execution::FrameScope::Nested, &policy);
    BOOST_REQUIRE_EQUAL(outcome.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE(!outcome.precompileHit);
}

}  // namespace bcos::evm::test

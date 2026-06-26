/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared helpers for ExecutionFrame / PrecompileRouter depth parity tests.
 */

#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <array>
#include <cstring>
#include <optional>

namespace bcos::evm::test
{
struct FrameBalanceOutcome
{
    evmc_status_code status{};
    int64_t gasLeft{};
    bcos::u256 senderBalance{};
    bcos::u256 recipientBalance{};
};

inline evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

inline evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}

inline state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

inline evmc_uint256be weiValue(uint8_t value)
{
    evmc_uint256be out{};
    out.bytes[31] = value;
    return out;
}

inline evmc_address balanceTarget(evmc_message const& msg)
{
    return std::memcmp(
               msg.code_address.bytes, evmc_address{}.bytes, sizeof(evmc_address{}.bytes)) != 0 ?
               msg.code_address :
               msg.recipient;
}

inline ExecuteMessageInput makeBaseInput(state::State& state, evmc_message const& message)
{
    static evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.warm_access = true;
    input.txProps.warmDestination = true;
    return input;
}

inline FrameBalanceOutcome runDepth0(state::State& state, evmc_message const& message)
{
    auto output = executeMessage(makeBaseInput(state, message));
    return {.status = output.result.status_code,
        .gasLeft = output.result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

struct Depth1HostFixture
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{};
    std::optional<state::EthHost> host;

    explicit Depth1HostFixture(state::State& state)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = {.revision = EVMC_PRAGUE, .warm_access = true};
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false);
    }

    state::EthHost& ethHost() { return *host; }
};

inline FrameBalanceOutcome runDepth1(state::State& state, evmc_message message)
{
    Depth1HostFixture fixture(state);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

inline evmc_message valueTransferMessage(evmc_address sender, evmc_address recipient,
    evmc_uint256be value, std::array<uint8_t, 4> const& inputBytes)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = value;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();
    return message;
}
}  // namespace bcos::evm::test

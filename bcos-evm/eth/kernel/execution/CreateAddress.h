/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief CREATE / CREATE2 contract address prediction (geth: CreateAddress / CreateAddress2).
 * @file CreateAddress.h
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

/// CREATE2 address = keccak256(0xff ++ sender ++ salt ++ keccak256(initCode))[12:32].
evmc_address predictCreate2Address(
    evmc_address const& sender, evmc_bytes32 const& salt, bcos::bytesConstRef initCode) noexcept;

/// Predict deployed contract address from message fields and sender nonce (CREATE / CREATE2).
evmc_address predictCreateAddress(
    state::State& state, evmc_message const& message, bcos::bytesConstRef initCode) noexcept;

}  // namespace bcos::evm::execution

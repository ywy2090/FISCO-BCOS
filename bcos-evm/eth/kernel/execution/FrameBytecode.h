/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Frame bytecode loading: initcode for CREATE, account code + EIP-7702 delegation chase.
 * @file FrameBytecode.h
 *
 * Pipeline position (EvmCallFrame::runVm, step 7):
 *   routeFrameMessage → classifyCallTarget → … → loadFrameBytecode → evmone::execute
 *
 * Complements FrameRouting: routing normalizes recipient/code_address and derives
 * executionAddress; this module reads the bytes that evmone will interpret. For EIP-7702,
 * routing handles EVMC_DELEGATED message flags; here we chase delegation designators stored
 * in account code (get_code(executionAddress) → parseDelegationTarget → get_code(delegate)).
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::evm
{
struct RevisionConfig;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

/// Load executable bytecode for one frame.
///
/// @param executionAddress Frame execution key from routeFrameMessage (not re-derived here).
/// @return CREATE/CREATE2: msg input as initcode; CALL: account code, following 7702 designator
///         to delegate contract code when revisionConfig.eip7702 is set.
bcos::bytes loadFrameBytecode(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message const& msg, evmc_address executionAddress);

}  // namespace bcos::evm::execution

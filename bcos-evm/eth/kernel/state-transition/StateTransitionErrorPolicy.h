/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Chain-specific failure mapping for stateTransitionExecute.
 * @file StateTransitionErrorPolicy.h
 *
 * Virtual table paired with StateTransitionHooks. Kernel calls these on
 * intrinsic-gas failure, C++ exceptions, and post-EVM result normalization.
 * Implementations: EthStateTransitionErrorPolicy, FiscoStateTransitionErrorPolicy,
 * OpStackStateTransitionErrorPolicy.
 */

#pragma once

#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include <exception>

namespace bcos::evm
{

class StateTransitionContext;

/// Chain-specific error mapping for stateTransitionExecute early-exit and exception paths.
struct StateTransitionErrorPolicy
{
    virtual ~StateTransitionErrorPolicy() = default;

    virtual void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicDebitFailure failure) const = 0;

    virtual void onException(
        StateTransitionContext& ctx, std::exception_ptr exceptionPtr) const = 0;

    /// Post-EVM execution result normalization (included-vmerr, CREATE address, revert logs, etc.).
    virtual void onFinalizeGasUsed(StateTransitionContext& ctx) const { (void)ctx; }

    /// Optional post-pipeline normalization (e.g. gas_left clamp on error paths).
    virtual void onComplete(StateTransitionContext& ctx) const { (void)ctx; }
};

}  // namespace bcos::evm

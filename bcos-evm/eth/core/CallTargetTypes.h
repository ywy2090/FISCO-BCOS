/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Call-target classification types shared by core seam and kernel execution.
 * @file CallTargetTypes.h
 */

#pragma once

#include <evmc/evmc.h>

namespace bcos::evm::execution
{

enum class CallTargetKind
{
    EvmContract,
    BuiltinPrecompile,
    ChainPrecompile,
    EmptyAccount,
    PolicyRejected,
};

enum class WarmPolicy
{
    Never,
    TxEntryAlways,
    TxEntryIfStatic,
    FrameEntryOnly,  // CREATE warm-pin; set by routeFrameMessage, not consumed by enumerate
};

/// Tx-entry warm set includes TxEntryAlways (builtin) and TxEntryIfStatic (fixed predeploys).
inline constexpr bool isTxEntryWarm(WarmPolicy policy) noexcept
{
    return policy == WarmPolicy::TxEntryAlways || policy == WarmPolicy::TxEntryIfStatic;
}

struct CallTargetDescriptor
{
    CallTargetKind kind{CallTargetKind::EvmContract};
    evmc_address dispatchAddress{};
    WarmPolicy warmPolicy{WarmPolicy::Never};
    evmc_message routed{};
};

}  // namespace bcos::evm::execution

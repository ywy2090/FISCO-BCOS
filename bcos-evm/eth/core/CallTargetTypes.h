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

/// Dispatch route for a classified call target.
enum class CallTargetRoute
{
    EvmContract,
    BuiltinPrecompile,
    ChainPrecompile,
    EmptyAccount,
};

/// Frame admission after target classification (orthogonal to CallTargetRoute).
enum class CallTargetAdmission
{
    Ok,
    DenyDelegateCallPrecompile,
};

/// EIP-2929 address warm schedule for a classified call target.
enum class AccessWarmSchedule
{
    AtFirstAccess,
    AtTxPrepare,
    AtTxPrepareIfStatic,
    AtFrameOpen,  // CREATE warm-pin; set by routeFrameMessage, not consumed by enumerate
};

/// Targets enumerated at transaction entry (prepareState / EIP-2929 tx warm set).
inline constexpr bool isEnumeratedAtTxPrepare(AccessWarmSchedule schedule) noexcept
{
    return schedule == AccessWarmSchedule::AtTxPrepare ||
           schedule == AccessWarmSchedule::AtTxPrepareIfStatic;
}

struct CallTargetDescriptor
{
    CallTargetRoute route{CallTargetRoute::EvmContract};
    CallTargetAdmission admission{CallTargetAdmission::Ok};
    evmc_address dispatchAddress{};
    AccessWarmSchedule accessWarm{AccessWarmSchedule::AtFirstAccess};
    evmc_message routed{};
};

}  // namespace bcos::evm::execution

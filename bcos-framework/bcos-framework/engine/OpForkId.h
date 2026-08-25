/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/engine/Types.h>

#include <cstdint>

namespace bcos::engine
{
/// Lightweight OP fork identity for Engine API profile selection (no EVMC types).
enum class OpForkId : uint8_t
{
    Isthmus = 0,
    Jovian = 1,
    Karst = 2,
};

/// Engine API method versions permitted for a fork at a given timestamp.
struct EngineApiProfile
{
    ApiVersion forkchoiceUpdated{};
    ApiVersion getPayload{};
    ApiVersion newPayload{};
};
}  // namespace bcos::engine

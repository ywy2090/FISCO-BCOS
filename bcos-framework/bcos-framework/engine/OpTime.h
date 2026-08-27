/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

namespace bcos::engine
{
/// FISCO block headers store timestamps in internal milliseconds; OP fork schedule activations
/// are Unix seconds. Convert exactly once at Engine/scheduler boundaries.
inline uint64_t unixSecondsFromInternalMillis(uint64_t ms)
{
    return ms / 1000;
}
}  // namespace bcos::engine

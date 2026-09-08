/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpForkId.h
 * @brief Lightweight OP fork identity and Engine API profile types.
 */
#pragma once

#include <bcos-framework/engine/Types.h>

#include <cstdint>
#include <variant>

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

enum class OpForkResolutionError : uint8_t
{
    UnsupportedTimestamp,
    InconsistentExecutionConfig,
};

struct EngineForkContext
{
    OpForkId forkId{};
    EngineApiProfile api{};
    bool hasDaFootprint = false;
};

using EngineForkResolution = std::variant<EngineForkContext, OpForkResolutionError>;
}  // namespace bcos::engine

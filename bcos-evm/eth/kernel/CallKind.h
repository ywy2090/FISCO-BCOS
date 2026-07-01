/*
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
 * @brief EVMC call-kind predicates shared by pipeline and execution layers.
 * @file CallKind.h
 */

#pragma once

#include <evmc/evmc.h>

namespace bcos::evm::execution
{
inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

/// DELEGATECALL / CALLCODE execute foreign code in the caller's own storage and
/// balance context and MUST NOT move balance between accounts. evmone still forwards
/// a (possibly non-zero) value in evmc_message.value — DELEGATECALL inherits the
/// parent frame's apparent value, CALLCODE carries the explicit stack value — but
/// go-ethereum's core/vm/evm.go DelegateCall/CallCode perform no Transfer. Host-side
/// value settlement uses this predicate to skip the transfer for those kinds.
inline bool isValueTransferSkippedKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_DELEGATECALL || kind == EVMC_CALLCODE;
}
}  // namespace bcos::evm::execution

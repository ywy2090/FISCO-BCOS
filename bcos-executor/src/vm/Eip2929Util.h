/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @brief EIP-2929 feature gating helpers (revision + feature flag).
 * @file Eip2929Util.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-framework/ledger/Features.h"
#include <evmc/evmc.h>

namespace bcos::executor
{

struct Eip2929AccessState;

/// Legacy bcos-executor gate: Berlin+ revision and feature_evm_eip2929 flag.
inline bool eip2929Enabled(evmc_revision revision, ledger::Features const& features) noexcept
{
    return revision >= EVMC_BERLIN && features.get(ledger::Features::Flag::feature_evm_eip2929);
}

/// True when warm access semantics apply in RevisionConfig (executeViaHost / bcos-evm kernel).
inline bool eip2929Enabled(const bcos::evm::RevisionConfig& rev) noexcept
{
    return rev.eip2929;
}

/// Top-level transaction entry (W1/W2) prewarm applies only at CALL depth 0.
inline bool eip2929TransactionEntryWarmEnabled(
    int64_t level, const bcos::evm::RevisionConfig& rev, Eip2929AccessState const* state) noexcept
{
    return level == 0 && state != nullptr && eip2929Enabled(rev);
}

}  // namespace bcos::executor

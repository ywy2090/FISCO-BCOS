/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Test-only state transition adapter over executeMessage().
 * @file Transition.hpp
 *
 * Semantic baseline: ywy2090/evmone v0.21.0 (ref 3585c2cb) test/state/ — production code
 * here is self-authored; diff against that tag when upgrading evmone or reviewing vectors.
 */

#pragma once

#include "bcos-evm/eth/policy/VmHostPolicy.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/EvmStateReader.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "state/BloomFilter.hpp"
#include <evmc/evmc.hpp>
#include <vector>

namespace bcos::evm::state
{
struct TransactionReceipt
{
    evmc_status_code status{EVMC_SUCCESS};
    int64_t gasUsed{0};
    int64_t gasRefund{0};
    bcos::bytes output;
    std::vector<LogEntry> logs;
    BloomFilter logsBloom;
};

TransactionReceipt transition(const EvmStateReader& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, const Transaction& tx, evmc_revision rev, evmc::VM& vm,
    const TransactionProperties& tx_props, VmHostPolicy* ext = nullptr);
}  // namespace bcos::evm::state

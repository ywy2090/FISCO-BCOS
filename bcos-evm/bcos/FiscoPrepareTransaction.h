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
 * @brief FISCO transaction-entry warm set wrapper.
 * @file FiscoPrepareTransaction.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/kernel/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{

struct FiscoPrepareTransactionInput
{
    bcos::evm_standard::RevisionConfig revisionConfig{};
    state::TransactionProperties properties{};
    const Eip2930AccessList* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
    std::optional<evmc_address> createCodeAddress{};
};

inline void prepareTransaction(state::State& state, const state::Transaction& transaction,
    const state::BlockInfo& blockInfo, const FiscoPrepareTransactionInput& input = {})
{
    execution::warmTransactionEntry(state, input.revisionConfig, nullptr, transaction, blockInfo,
        input.properties, input.accessList, input.web3TypedTxKind, input.createCodeAddress);
}

}  // namespace bcos::evm

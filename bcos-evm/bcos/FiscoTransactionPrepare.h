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
 * @brief Thin prepare wrapper for transaction-entry warm set.
 * @file FiscoTransactionPrepare.h
 */

#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/execution/warmTransactionEntry.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.h>

namespace bcos::evm
{

struct FiscoTransactionPrepareInput
{
    evmc_revision revision{EVMC_CANCUN};
    state::TransactionProperties properties{};
    const Eip2930AccessList* accessList{nullptr};
};

inline void prepareTransaction(state::State& state, const state::Transaction& transaction,
    const state::BlockInfo& blockInfo, const FiscoTransactionPrepareInput& input = {})
{
    execution::warmTransactionEntry(
        state, input.revision, transaction, blockInfo, input.properties, input.accessList);
}

}  // namespace bcos::evm

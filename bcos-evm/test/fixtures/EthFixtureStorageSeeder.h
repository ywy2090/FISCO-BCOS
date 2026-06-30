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
 * @brief Seeds executor MutableStorage from EthStateFixtureLoader pre-state.
 * @file EthFixtureStorageSeeder.h
 */

#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"

namespace bcos::executor_v1
{
using MutableStorage = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
    storage2::memory_storage::ORDERED>;
}  // namespace bcos::executor_v1

namespace bcos::evm::test::fixtures
{

inline task::Task<void> seedPreState(executor_v1::MutableStorage& storage,
    FixtureCase const& fixture, crypto::Hash::Ptr const& hashImpl)
{
    for (auto const& [address, account] : fixture.preState)
    {
        ledger::account::EVMAccount evmAccount(storage, address, false);
        if (!co_await evmAccount.exists())
        {
            co_await evmAccount.create();
        }
        co_await evmAccount.setBalance(account.balance);
        co_await evmAccount.setNonce(std::to_string(account.nonce));
        if (!account.code.empty())
        {
            auto const codeHash =
                hashImpl->hash(bcos::bytesConstRef(account.code.data(), account.code.size()));
            co_await evmAccount.setCode(account.code, account.abi, codeHash);
        }
    }

    // Executor E2E builds protocol transactions without unsigned EIP-7702 tuples.
    // Pre-apply delegation so delegated CALL smoke matches kernel fixture adapters.
    for (auto const& authorization : fixture.authorizations)
    {
        if (!fixture.authorizationListPresent || authorization.yParity.has_value() ||
            !authorization.signatureR.empty() || !authorization.signatureS.empty())
        {
            continue;
        }
        if (state::isZeroAddress(authorization.authority) ||
            state::isZeroAddress(authorization.address))
        {
            continue;
        }

        auto const delegation = addressToDelegation(authorization.address);
        ledger::account::EVMAccount authorityAccount(storage, authorization.authority, false);
        if (!co_await authorityAccount.exists())
        {
            co_await authorityAccount.create();
        }
        auto const codeHash =
            hashImpl->hash(bcos::bytesConstRef(delegation.data(), delegation.size()));
        co_await authorityAccount.setCode(delegation, {}, codeHash);
    }
}

}  // namespace bcos::evm::test::fixtures

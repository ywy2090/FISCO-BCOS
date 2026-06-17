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
 * @brief evm precompiled contract wrapper
 * @file PrecompiledContract.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include "PrecompiledRegistrar.h"
#include <evmc/evmc.h>
#include <memory>

namespace bcos
{
namespace evm
{
class PrecompiledContract
{
public:
    using Ptr = std::shared_ptr<PrecompiledContract>;
    PrecompiledContract() = default;
    PrecompiledContract(PrecompiledPricer const& _cost, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0);

    PrecompiledContract(unsigned _base, unsigned _word, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0);

    /// modexp (0x05): revision-aware gas via calcModexpGas (EIP-198/2565/7883).
    static PrecompiledContract modexp(
        PrecompiledExecutor const& exec, u256 const& startingBlock = 0);

    bigint cost(bytesConstRef _in) const;
    bigint cost(bytesConstRef _in, evmc_revision revision) const;
    std::pair<bool, bytes> execute(bytesConstRef _in) const;

    u256 const& startingBlock() const;

private:
    PrecompiledPricer m_cost;
    RevisionAwarePricer m_revisionAwareCost;
    PrecompiledExecutor m_execute;
    u256 m_startingBlock = 0;
};
}  // namespace evm
}  // namespace bcos

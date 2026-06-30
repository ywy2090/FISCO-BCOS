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
 * @brief evm precompiled contract wrapper
 * @file PrecompiledContract.cpp
 *
 * Delegates cost()/execute() to stored functors; modexp uses revision-aware path.
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "bcos-evm/eth/precompiled/PrecompiledContract.h"
#include "bcos-evm/eth/precompiled/ModexpGas.h"

using namespace bcos;

namespace bcos::evm
{
PrecompiledContract::PrecompiledContract(
    PrecompiledPricer const& _cost, PrecompiledExecutor const& _exec, u256 const& _startingBlock)
  : m_cost(_cost), m_execute(_exec), m_startingBlock(_startingBlock)
{}

PrecompiledContract::PrecompiledContract(
    unsigned _base, unsigned _word, PrecompiledExecutor const& _exec, u256 const& _startingBlock)
  : PrecompiledContract(
        [=](bytesConstRef _in) -> bigint {
            bigint size = _in.size();
            bigint base = _base;
            bigint word = _word;
            return base + (size + 31) / 32 * word;
        },
        _exec, _startingBlock)
{}

PrecompiledContract PrecompiledContract::modexp(
    PrecompiledExecutor const& exec, u256 const& startingBlock)
{
    PrecompiledContract contract;
    contract.m_execute = exec;
    contract.m_startingBlock = startingBlock;
    contract.m_revisionAwareCost = [](bytesConstRef input, evmc_revision revision) {
        return calcModexpGas(input, revision);
    };
    return contract;
}

bigint PrecompiledContract::cost(bytesConstRef _in) const
{
    if (m_revisionAwareCost)
    {
        // Fallback for callers without revision; Berlin/EIP-2565 minimum post-fork pricing.
        return m_revisionAwareCost(_in, EVMC_BERLIN);
    }
    return m_cost(_in);
}

bigint PrecompiledContract::cost(bytesConstRef _in, evmc_revision revision) const
{
    if (m_revisionAwareCost)
    {
        return m_revisionAwareCost(_in, revision);
    }
    return m_cost(_in);
}

std::pair<bool, bytes> PrecompiledContract::execute(bytesConstRef _in) const
{
    return m_execute(_in);
}

u256 const& PrecompiledContract::startingBlock() const
{
    return m_startingBlock;
}

}  // namespace bcos::evm

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
 * @brief evm precompiled registrar
 * @file PrecompiledRegistrar.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "bcos-evm/ethereum/precompiled/PrecompiledRegistrar.h"
#include <boost/throw_exception.hpp>

using namespace bcos;

namespace bcos::executor
{
PrecompiledRegistrar* PrecompiledRegistrar::get()
{
    static PrecompiledRegistrar instance;
    return &instance;
}

PrecompiledExecutor PrecompiledRegistrar::registerExecutor(
    std::string const& _name, PrecompiledExecutor const& _exec)
{
    return (get()->m_execs[_name] = _exec);
}

void PrecompiledRegistrar::unregisterExecutor(std::string const& _name)
{
    get()->m_execs.erase(_name);
}

PrecompiledPricer PrecompiledRegistrar::registerPricer(
    std::string const& _name, PrecompiledPricer const& _exec)
{
    return (get()->m_pricers[_name] = _exec);
}

void PrecompiledRegistrar::unregisterPricer(std::string const& _name)
{
    get()->m_pricers.erase(_name);
}

PrecompiledExecutor const& PrecompiledRegistrar::executor(std::string const& _name)
{
    auto const it = get()->m_execs.find(_name);
    if (it == get()->m_execs.end())
    {
        BOOST_THROW_EXCEPTION(ExecutorNotFound());
    }
    return it->second;
}

PrecompiledPricer const& PrecompiledRegistrar::pricer(std::string const& _name)
{
    const auto it = get()->m_pricers.find(_name);
    if (it == get()->m_pricers.end())
    {
        BOOST_THROW_EXCEPTION(PricerNotFound());
    }
    return it->second;
}

void PrecompiledRegistrar::registerAllBuiltins()
{
    // Stub: explicit registration of the 16 Ethereum precompiles + p256verify will be added here.
    // Each builtin implementation will be pulled in via its own header (e.g. sha256.hpp,
    // modexp.hpp, bls.hpp, etc.) and registered with registerExecutor/registerPricer.
    //
    // ETH_REGISTER_PRECOMPILED macros continue to work via static-init for now.
}

}  // namespace bcos::executor

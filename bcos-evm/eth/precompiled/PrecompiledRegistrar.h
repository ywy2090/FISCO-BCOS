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
 * @file PrecompiledRegistrar.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace bcos
{
namespace executor
{
using PrecompiledExecutor = std::function<std::pair<bool, bytes>(bytesConstRef)>;
using PrecompiledPricer = std::function<bigint(bytesConstRef)>;
using RevisionAwarePricer = std::function<bigint(bytesConstRef, evmc_revision)>;

DERIVE_BCOS_EXCEPTION(ExecutorNotFound);
DERIVE_BCOS_EXCEPTION(PricerNotFound);

class PrecompiledRegistrar
{
public:
    /// Get the executor object for @a _name function or @throw ExecutorNotFound if not found.
    static PrecompiledExecutor const& executor(std::string const& _name);

    /// Get the price calculator object for @a _name function or @throw PricerNotFound if not found.
    static PrecompiledPricer const& pricer(std::string const& _name);

    /// Register an executor. In general just use ETH_REGISTER_PRECOMPILED.
    static PrecompiledExecutor registerExecutor(
        std::string const& _name, PrecompiledExecutor const& _exec);
    /// Unregister an executor. Shouldn't generally be necessary.
    static void unregisterExecutor(std::string const& _name);

    /// Register a pricer. In general just use ETH_REGISTER_PRECOMPILED_PRICER.
    static PrecompiledPricer registerPricer(
        std::string const& _name, PrecompiledPricer const& _exec);
    /// Unregister a pricer. Shouldn't generally be necessary.
    static void unregisterPricer(std::string const& _name);

    /// Stub for explicit 16+1 precompile registration (replaces ETH_REGISTER_PRECOMPILED macros).
    /// Builtin implementations will be registered once their headers are included.
    static void registerAllBuiltins();

private:
    static PrecompiledRegistrar* get();

    std::unordered_map<std::string, PrecompiledExecutor> m_execs;
    std::unordered_map<std::string, PrecompiledPricer> m_pricers;
};

// [deprecated] These macros will be removed after registerAllBuiltins() is fully wired.
#define ETH_REGISTER_PRECOMPILED(Name)                                                        \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name(bytesConstRef _in); \
    static bcos::executor::PrecompiledExecutor __eth_registerPrecompiledFactory##Name =       \
        ::bcos::executor::PrecompiledRegistrar::registerExecutor(                             \
            #Name, &__eth_registerPrecompiledFunction##Name);                                 \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name
#define ETH_REGISTER_PRECOMPILED_PRICER(Name)                                    \
    static bigint __eth_registerPricerFunction##Name(bytesConstRef _in);         \
    static bcos::executor::PrecompiledPricer __eth_registerPricerFactory##Name = \
        ::bcos::executor::PrecompiledRegistrar::registerPricer(                  \
            #Name, &__eth_registerPricerFunction##Name);                         \
    static bigint __eth_registerPricerFunction##Name

}  // namespace executor
}  // namespace bcos

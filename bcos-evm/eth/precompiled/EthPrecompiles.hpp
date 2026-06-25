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
 * @brief Builtin ethereum precompile dispatcher (0x01..0x11).
 * @file EthPrecompiles.hpp
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <optional>

namespace bcos::evm::precompiled
{
struct EthPrecompileResult
{
    evmc_status_code status{EVMC_SUCCESS};
    int64_t gasCost{0};
    bcos::bytes output;
};

class EthPrecompiles
{
public:
    static bool isAddressInRange(const evmc_address& address) noexcept;

    static std::optional<EthPrecompileResult> dispatch(const evmc_address& address,
        bcos::bytesConstRef input, int64_t msgGas, evmc_revision revision,
        bcos::evm_standard::RevisionConfig const& cfg);

    static std::optional<evmc::Result> tryDispatchInCall(const evmc_address& address,
        const evmc_message& msg, evmc_revision revision,
        bcos::evm_standard::RevisionConfig const& cfg);
};
}  // namespace bcos::evm::precompiled

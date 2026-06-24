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
 * @file ExecutionFrame.cpp
 */

#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/state/EthHost.hpp"

namespace bcos::evm::execution
{

FrameResult runExecutionFrame(
    FrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    (void)ctx;
    (void)message;
    (void)scope;
    (void)host;
    evmc_result raw{};
    raw.status_code = EVMC_INTERNAL_ERROR;
    return FrameResult{.result = evmc::Result(raw)};
}

}  // namespace bcos::evm::execution

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
 * @file OpStackPipelineInternals.h
 */

#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

inline EVMCResult makeOutOfGasLimitResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
}

inline EVMCResult makeInternalErrorResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_INTERNAL_ERROR;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::Unknown);
}

}  // namespace bcos::evm

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
 * @brief EVM kernel entry: tx warm/7702 setup, runCallFrame, finalize (geth innerExecute).
 * @file InnerExecute.h
 */

#pragma once

#include "bcos-evm/eth/kernel/InnerExecuteTypes.h"

namespace bcos::evm
{

InnerExecuteOutput innerExecute(InnerExecuteInput input);

}  // namespace bcos::evm

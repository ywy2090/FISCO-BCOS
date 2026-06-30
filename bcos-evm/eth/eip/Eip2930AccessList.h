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
 * @brief EIP-2930 access list type for eth gas settlement.
 * @file Eip2930AccessList.h
 */

#pragma once

#include "bcos-utilities/FixedBytes.h"
#include <utility>
#include <vector>

namespace bcos::evm
{
/// EIP-2930 access list: 20-byte address + storage keys (h256).
using Eip2930AccessList = std::vector<std::pair<h160, std::vector<h256>>>;
}  // namespace bcos::evm

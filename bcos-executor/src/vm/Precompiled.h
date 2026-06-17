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
 * @brief evm precompiled (compatibility header)
 * @file Precompiled.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include "bcos-evm/bcos/Precompiled.h"
#include "bcos-evm/ethereum/precompiled/PrecompiledContract.h"
#include "bcos-evm/ethereum/precompiled/PrecompiledRegistrar.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <cstdint>

namespace bcos
{
namespace crypto
{
// sha2 - sha256 replace Hash.h begin
h256 sha256(bytesConstRef _input) noexcept;

h160 ripemd160(bytesConstRef _input);

/// Calculates the compression function F used in the BLAKE2 cryptographic hashing algorithm
/// Throws exception in case input data has incorrect size.
/// @param _rounds       the number of rounds
/// @param _stateVector  the state vector - 8 unsigned 64-bit little-endian words
/// @param _t0, _t1      offset counters - unsigned 64-bit little-endian words
/// @param _lastBlock    the final block indicator flag
/// @param _messageBlock the message block vector - 16 unsigned 64-bit little-endian words
/// @returns             updated state vector with unchanged encoding (little-endian)
bytes blake2FCompression(uint32_t _rounds, bytesConstRef _stateVector, bytesConstRef _t0,
    bytesConstRef _t1, bool _lastBlock, bytesConstRef _messageBlock);

std::pair<bool, bytes> ecRecover(bytesConstRef _in);
}  // namespace crypto
}  // namespace bcos

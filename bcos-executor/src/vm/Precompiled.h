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
#include "EthBuiltinRegistry.h"
#include "PrecompiledContract.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include "transaction-executor/bcos-transaction-executor/adapters/Precompiled.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bcos::executor
{
using PrecompiledContract = evm::PrecompiledContract;
using PrecompiledExecutor = evm::PrecompiledExecutor;
using PrecompiledPricer = evm::PrecompiledPricer;

/// Legacy name-based lookup for executor unit tests (forwards to EthBuiltinRegistry).
struct PrecompiledRegistrar
{
    static uint16_t suffixFromName(std::string_view name)
    {
        static const std::unordered_map<std::string, uint16_t> table = {
            {"ecrecover", 0x0001},
            {"sha256", 0x0002},
            {"ripemd160", 0x0003},
            {"identity", 0x0004},
            {"modexp", 0x0005},
            {"ecadd", 0x0006},
            {"ecmul", 0x0007},
            {"alt_bn128_pairing_product", 0x0008},
            {"blake2_compression", 0x0009},
            {"point_evaluation", 0x000a},
            {"bls12_g1add", 0x000b},
            {"bls12_g1msm", 0x000c},
            {"bls12_g2add", 0x000d},
            {"bls12_g2msm", 0x000e},
            {"bls12_pairing_check", 0x000f},
            {"bls12_map_fp_to_g1", 0x0010},
            {"bls12_map_fp2_to_g2", 0x0011},
            {"p256verify", 0x0100},
        };
        auto const it = table.find(std::string(name));
        if (it == table.end())
        {
            throw std::invalid_argument("Unknown precompiled name: " + std::string(name));
        }
        return it->second;
    }

    static evm::PrecompiledPricer const& pricer(std::string_view name)
    {
        return evm::builtinPricerBySuffix(suffixFromName(name));
    }

    static evm::PrecompiledExecutor const& executor(std::string_view name)
    {
        return evm::builtinExecutorBySuffix(suffixFromName(name));
    }
};
}  // namespace bcos::executor

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

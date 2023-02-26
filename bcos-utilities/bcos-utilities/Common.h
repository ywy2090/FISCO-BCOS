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
 */

#pragma once

#include "Log.h"
#include "RefDataContainer.h"
#include <boost/algorithm/string.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bcos
{
using namespace boost::multiprecision::literals;

// vector of byte data
using byte = uint8_t;
using bytes = std::vector<byte>;
using bytesPointer = std::shared_ptr<std::vector<byte>>;
using bytesConstPtr = std::shared_ptr<const bytes>;
using bytesRef = RefDataContainer<byte>;
using bytesConstRef = RefDataContainer<byte const>;

// Numeric types.
using bigint = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>>;

// unsigned int256
using u256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int256
using s256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int160
using u160 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<160, 160,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int160
using s160 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<160, 160,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int256
using u512 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<512, 512,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int256
using s512 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<512, 512,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;

// Map types.
using BytesMap = std::map<bytes, bytes>;
// Fixed-length string types.
using string32 = std::array<char, 32>;
// Map types.
using HexMap = std::map<bytes, bytes>;

// Null/Invalid values for convenience.
extern bytes const NullBytes;
u256 constexpr Invalid256 =
    0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff_cppui256;


using Mutex = std::mutex;
using RecursiveMutex = std::recursive_mutex;
using SharedMutex = boost::shared_mutex;

using Guard = std::lock_guard<std::mutex>;
using UniqueGuard = std::unique_lock<std::mutex>;
using RecursiveGuard = std::lock_guard<std::recursive_mutex>;
using ReadGuard = boost::shared_lock<boost::shared_mutex>;
using UpgradableGuard = boost::upgrade_lock<boost::shared_mutex>;
using UpgradeGuard = boost::upgrade_to_unique_lock<boost::shared_mutex>;
using WriteGuard = boost::unique_lock<boost::shared_mutex>;

template <size_t n>
inline u256 exp10()
{
    return exp10<n - 1>() * u256(10);
}

template <>
inline u256 exp10<0>()
{
    return u256(1);
}

//------------ Type interprets and Convertions----------------
/// Interprets @a _u as a two's complement signed number and returns the resulting s256.
inline s256 u2s(u256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    /// get the +/- symbols
    if (boost::multiprecision::bit_test(_u, 255))
        return s256(-(c_end - _u));
    else
        return s256(_u);
}

/// @returns the two's complement signed representation of the signed number _u.
inline u256 s2u(s256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    if (_u >= 0)
        return u256(_u);
    else
        return u256(c_end + _u);
}

inline bool isalNumStr(std::string const& _stringData)
{
    for (auto ch : _stringData)
    {
        if (isalnum(ch))
        {
            continue;
        }
        return false;
    }
    return true;
}

inline double calcAvgRate(uint64_t _data, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto avgRate = (double)_data * 8 * 1000 / 1024 / 1024 / _intervalMS;
        return avgRate;
    }
    return 0;
}

inline uint32_t calcAvgQPS(uint64_t _requestCount, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto qps = _requestCount * 1000 / _intervalMS;
        return qps;
    }
    return 0;
}

/// Get the current time in seconds since the epoch in UTC(ms)
uint64_t utcTime();
uint64_t utcSteadyTime();

/// Get the current time in seconds since the epoch in UTC(us)
uint64_t utcTimeUs();
uint64_t utcSteadyTimeUs();

// get the current data time
std::string getCurrentDateTime();

struct Exception;
// callback when throw exceptions
void errorExit(std::stringstream& _exitInfo, Exception const& exception);

void pthread_setThreadName(std::string const& _n);

}  // namespace bcos

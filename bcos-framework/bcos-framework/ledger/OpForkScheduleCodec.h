/**
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
 * @file OpForkScheduleCodec.h
 * @brief Canonical OP fork-schedule codec (Isthmus+; Karst after Jovian).
 */
#pragma once

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/throw_exception.hpp>

// Canonical fork-schedule codec aligned with op-reth / op-node. Karst is a named
// activation after Jovian; baseline remains isthmus|jovian.

namespace bcos::ledger
{
struct OpForkActivationRecord
{
    std::string forkName;
    uint64_t timestamp = 0;
};

DERIVE_BCOS_EXCEPTION(InvalidOpForkSchedule);

inline constexpr std::string_view c_legacyIsthmusCanonical = "0:isthmus";
inline constexpr std::string_view c_legacyJovianCanonical = "0:jovian";

[[nodiscard]] inline std::string_view legacyOpForkScheduleCanonical(bool jovianActive) noexcept
{
    return jovianActive ? c_legacyJovianCanonical : c_legacyIsthmusCanonical;
}

[[noreturn]] inline void throwInvalidOpForkSchedule(std::string_view msg)
{
    BOOST_THROW_EXCEPTION(InvalidOpForkSchedule() << errinfo_comment(std::string(msg)));
}

namespace detail
{
inline constexpr int forkOrder(std::string_view forkName)
{
    if (forkName == "isthmus")
        return 4;
    if (forkName == "jovian")
        return 5;
    if (forkName == "karst")
        return 6;
    return -1;
}

inline bool isAllowedBaseline(std::string_view forkName)
{
    return forkName == "isthmus" || forkName == "jovian";
}

inline std::string trimAscii(std::string_view input)
{
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())) != 0)
        input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())) != 0)
        input.remove_suffix(1);
    return std::string(input);
}

inline uint64_t parseTimestamp(std::string_view token)
{
    if (token.empty())
        throwInvalidOpForkSchedule("empty timestamp");
    uint64_t value = 0;
    for (const char ch : token)
    {
        if (ch < '0' || ch > '9')
            throwInvalidOpForkSchedule("invalid timestamp");
        const auto digit = static_cast<uint64_t>(ch - '0');
        // Pre-multiply guard: `next < value` misses wraps that land above value.
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            throwInvalidOpForkSchedule("timestamp overflow");
        value = value * 10 + digit;
    }
    return value;
}

inline std::string normalizeForkName(std::string_view token)
{
    auto forkName = trimAscii(token);
    for (char& ch : forkName)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return forkName;
}

inline void validateScheduleRecords(std::span<const OpForkActivationRecord> activations)
{
    if (activations.empty())
        throwInvalidOpForkSchedule("empty schedule");

    if (activations.front().timestamp != 0)
        throwInvalidOpForkSchedule("missing timestamp-0 baseline");

    const auto& baseline = activations.front().forkName;
    if (!isAllowedBaseline(baseline))
        throwInvalidOpForkSchedule("invalid baseline fork");

    bool hasJovian = baseline == "jovian";
    bool hasKarst = false;
    int previousOrder = -1;
    uint64_t previousTimestamp = 0;
    std::vector<std::string_view> seenForks;

    for (const auto& activation : activations)
    {
        const int order = forkOrder(activation.forkName);
        if (order < 0)
            throwInvalidOpForkSchedule("unknown or pre-Isthmus fork");

        if (activation.timestamp < previousTimestamp)
            throwInvalidOpForkSchedule("timestamps out of order");
        if (activation.timestamp == previousTimestamp && !activations.empty() &&
            &activation != &activations.front())
            throwInvalidOpForkSchedule("duplicate timestamp");

        if (order <= previousOrder)
            throwInvalidOpForkSchedule("forks out of protocol order");

        const auto forkView = std::string_view{activation.forkName};
        if (std::find(seenForks.begin(), seenForks.end(), forkView) != seenForks.end())
            throwInvalidOpForkSchedule("duplicate fork");
        seenForks.push_back(forkView);

        if (activation.forkName == "jovian")
            hasJovian = true;
        if (activation.forkName == "karst")
            hasKarst = true;

        previousOrder = order;
        previousTimestamp = activation.timestamp;
    }

    if (hasKarst && baseline == "isthmus" && !hasJovian)
        throwInvalidOpForkSchedule("Jovian activation is required before Karst");
}

inline std::string serializeScheduleRecords(std::span<const OpForkActivationRecord> activations)
{
    std::string canonical;
    for (std::size_t index = 0; index < activations.size(); ++index)
    {
        if (index != 0)
            canonical.push_back(',');
        canonical.append(std::to_string(activations[index].timestamp));
        canonical.push_back(':');
        canonical.append(activations[index].forkName);
    }
    return canonical;
}
}  // namespace detail

inline constexpr std::size_t c_maxOpForkActivations = 8;
inline constexpr std::size_t c_maxOpForkScheduleBytes = 512;

inline std::vector<OpForkActivationRecord> parseOpForkSchedule(std::string_view canonical)
{
    const auto trimmed = detail::trimAscii(canonical);
    if (trimmed.empty())
        throwInvalidOpForkSchedule("empty schedule");
    if (trimmed.size() > c_maxOpForkScheduleBytes)
        throwInvalidOpForkSchedule("schedule too long");

    std::vector<OpForkActivationRecord> activations;
    std::string_view remaining{trimmed};
    while (!remaining.empty())
    {
        if (activations.size() >= c_maxOpForkActivations)
            throwInvalidOpForkSchedule("too many activations");

        const auto comma = remaining.find(',');
        const auto entry = remaining.substr(0, comma);
        const auto colon = entry.find(':');
        if (colon == std::string_view::npos)
            throwInvalidOpForkSchedule("invalid activation entry");

        OpForkActivationRecord record;
        record.timestamp = detail::parseTimestamp(entry.substr(0, colon));
        record.forkName = detail::normalizeForkName(entry.substr(colon + 1));
        activations.push_back(std::move(record));

        if (comma == std::string_view::npos)
            break;
        remaining.remove_prefix(comma + 1);
        if (remaining.empty())
            throwInvalidOpForkSchedule("trailing comma");
    }

    detail::validateScheduleRecords(activations);
    return activations;
}

inline std::string canonicalOpForkSchedule(std::span<const OpForkActivationRecord> activations)
{
    detail::validateScheduleRecords(activations);
    return detail::serializeScheduleRecords(activations);
}

inline crypto::HashType keccakOpForkScheduleHash(std::string_view canonical)
{
    const auto activations = parseOpForkSchedule(canonical);
    const auto normalized = detail::serializeScheduleRecords(activations);
    return crypto::keccak256Hash(
        bytesConstRef(reinterpret_cast<const byte*>(normalized.data()), normalized.size()));
}
}  // namespace bcos::ledger

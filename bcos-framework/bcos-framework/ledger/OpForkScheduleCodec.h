#pragma once

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ledger
{
struct OpForkActivationRecord
{
    std::string forkName;
    uint64_t timestamp = 0;
};

class InvalidOpForkSchedule : public std::invalid_argument
{
public:
    using std::invalid_argument::invalid_argument;
};

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
        throw InvalidOpForkSchedule("empty timestamp");
    uint64_t value = 0;
    for (const char ch : token)
    {
        if (ch < '0' || ch > '9')
            throw InvalidOpForkSchedule("invalid timestamp");
        const auto digit = static_cast<uint64_t>(ch - '0');
        // Pre-multiply guard: `next < value` misses wraps that land above value.
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            throw InvalidOpForkSchedule("timestamp overflow");
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
        throw InvalidOpForkSchedule("empty schedule");

    if (activations.front().timestamp != 0)
        throw InvalidOpForkSchedule("missing timestamp-0 baseline");

    const auto& baseline = activations.front().forkName;
    if (!isAllowedBaseline(baseline))
        throw InvalidOpForkSchedule("invalid baseline fork");

    bool hasJovian = baseline == "jovian";
    bool hasKarst = false;
    int previousOrder = -1;
    uint64_t previousTimestamp = 0;
    std::vector<std::string_view> seenForks;

    for (const auto& activation : activations)
    {
        const int order = forkOrder(activation.forkName);
        if (order < 0)
            throw InvalidOpForkSchedule("unknown or pre-Isthmus fork");

        if (activation.timestamp < previousTimestamp)
            throw InvalidOpForkSchedule("timestamps out of order");
        if (activation.timestamp == previousTimestamp && !activations.empty() &&
            &activation != &activations.front())
            throw InvalidOpForkSchedule("duplicate timestamp");

        if (order <= previousOrder)
            throw InvalidOpForkSchedule("forks out of protocol order");

        const auto forkView = std::string_view{activation.forkName};
        if (std::find(seenForks.begin(), seenForks.end(), forkView) != seenForks.end())
            throw InvalidOpForkSchedule("duplicate fork");
        seenForks.push_back(forkView);

        if (activation.forkName == "jovian")
            hasJovian = true;
        if (activation.forkName == "karst")
            hasKarst = true;

        previousOrder = order;
        previousTimestamp = activation.timestamp;
    }

    if (hasKarst && baseline == "isthmus" && !hasJovian)
        throw InvalidOpForkSchedule("Jovian activation is required before Karst");
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

inline constexpr std::size_t kMaxOpForkActivations = 8;
inline constexpr std::size_t kMaxOpForkScheduleBytes = 512;

inline std::vector<OpForkActivationRecord> parseOpForkSchedule(std::string_view canonical)
{
    const auto trimmed = detail::trimAscii(canonical);
    if (trimmed.empty())
        throw InvalidOpForkSchedule("empty schedule");
    if (trimmed.size() > kMaxOpForkScheduleBytes)
        throw InvalidOpForkSchedule("schedule too long");

    std::vector<OpForkActivationRecord> activations;
    std::string_view remaining{trimmed};
    while (!remaining.empty())
    {
        if (activations.size() >= kMaxOpForkActivations)
            throw InvalidOpForkSchedule("too many activations");

        const auto comma = remaining.find(',');
        const auto entry = remaining.substr(0, comma);
        const auto colon = entry.find(':');
        if (colon == std::string_view::npos)
            throw InvalidOpForkSchedule("invalid activation entry");

        OpForkActivationRecord record;
        record.timestamp = detail::parseTimestamp(entry.substr(0, colon));
        record.forkName = detail::normalizeForkName(entry.substr(colon + 1));
        activations.push_back(std::move(record));

        if (comma == std::string_view::npos)
            break;
        remaining.remove_prefix(comma + 1);
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

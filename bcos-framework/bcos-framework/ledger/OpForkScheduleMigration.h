#pragma once

#include "OpForkScheduleCodec.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bcos::ledger
{

class InvalidOpForkScheduleMigration : public std::invalid_argument
{
public:
    using std::invalid_argument::invalid_argument;
};

/// Convert a block header timestamp (ms) to the unix-seconds freeze boundary used by
/// `validateOpForkScheduleMigration`. Callers choose which head to freeze against; the
/// migrate CLI currently passes the ledger tip (not Engine safe/finalized).
[[nodiscard]] inline uint64_t opForkScheduleSafeHeadSeconds(uint64_t headerTimestampMs)
{
    return headerTimestampMs / 1000;
}

namespace detail
{
[[nodiscard]] inline std::vector<OpForkActivationRecord> opForkSchedulePastActivations(
    std::span<const OpForkActivationRecord> activations, uint64_t safeHeadTimestampSeconds)
{
    std::vector<OpForkActivationRecord> past;
    for (const auto& activation : activations)
    {
        if (activation.timestamp <= safeHeadTimestampSeconds)
        {
            past.push_back(activation);
        }
    }
    return past;
}

[[nodiscard]] inline bool opForkActivationRecordsEqual(
    std::span<const OpForkActivationRecord> left, std::span<const OpForkActivationRecord> right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].timestamp != right[index].timestamp ||
            left[index].forkName != right[index].forkName)
        {
            return false;
        }
    }
    return true;
}
}  // namespace detail

inline void validateOpForkScheduleMigration(
    std::string_view oldCanonical, std::string_view newCanonical, uint64_t safeHeadTimestampSeconds)
{
    const auto oldActivations = parseOpForkSchedule(oldCanonical);
    const auto newActivations = parseOpForkSchedule(newCanonical);

    const auto oldPast =
        detail::opForkSchedulePastActivations(oldActivations, safeHeadTimestampSeconds);
    const auto newPast =
        detail::opForkSchedulePastActivations(newActivations, safeHeadTimestampSeconds);

    if (!detail::opForkActivationRecordsEqual(oldPast, newPast))
    {
        throw InvalidOpForkScheduleMigration(
            "op fork schedule migration rewrites history or is not future-only");
    }
}

}  // namespace bcos::ledger

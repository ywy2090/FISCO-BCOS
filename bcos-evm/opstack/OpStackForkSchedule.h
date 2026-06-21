#pragma once
#include <cstdint>
#include <optional>

namespace bcos::evm
{

struct OpStackForkSchedule
{
    std::optional<uint64_t> fjordTime;
    std::optional<uint64_t> isthmusTime;
};

inline bool isOpStackForkActive(std::optional<uint64_t> forkTime, uint64_t blockTime)
{
    return forkTime.has_value() && *forkTime <= blockTime;
}

inline OpStackForkSchedule makeIsthmusPlusForkSchedule()
{
    return OpStackForkSchedule{.fjordTime = 0, .isthmusTime = 0};
}

inline bool isOpStackFjord(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.fjordTime, blockTime);
}

inline bool isOpStackIsthmus(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.isthmusTime, blockTime);
}

}  // namespace bcos::evm

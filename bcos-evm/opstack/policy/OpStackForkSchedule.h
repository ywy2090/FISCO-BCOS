/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OP Stack fork activation schedule (Fjord / Isthmus / Jovian).
 * @file OpStackForkSchedule.h
 *
 * Temporal policy complement to OpStackConstants.h: numeric literals live there;
 * this file records when each OP Stack hardfork becomes active (Unix block timestamp).
 * Mirrors op-geth chain config FjordTime / IsthmusTime / JovianTime. Fee planners,
 * predeploy dispatch, and settlement query isOpStack*() at execution time.
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bcos::evm
{

/// Activation timestamps for OP Stack hardforks. std::nullopt = fork not configured.
struct OpStackForkSchedule
{
    std::optional<uint64_t> fjordTime;    // Fjord L1 data fee (FastLZ linear regression)
    std::optional<uint64_t> isthmusTime;  // Isthmus operator fee, EIP-7702, blob schedule changes
    std::optional<uint64_t> jovianTime;   // Jovian operator fee formula + daFootprint receipt field
};

/// True when forkTime is set and blockTime has reached or passed the activation timestamp.
inline bool isOpStackForkActive(std::optional<uint64_t> forkTime, uint64_t blockTime)
{
    return forkTime.has_value() && *forkTime <= blockTime;
}

// --- Test / default schedules (all configured forks active from genesis) ---

/// Fjord + Isthmus active at blockTime >= 0; Jovian inactive. Default for most unit tests.
inline OpStackForkSchedule makeIsthmusPlusForkSchedule()
{
    return OpStackForkSchedule{.fjordTime = 0, .isthmusTime = 0};
}

/// Fjord + Isthmus + Jovian active at blockTime >= 0.
inline OpStackForkSchedule makeJovianPlusForkSchedule()
{
    return OpStackForkSchedule{.fjordTime = 0, .isthmusTime = 0, .jovianTime = 0};
}

// --- Per-fork activation queries ---

inline bool isOpStackFjord(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.fjordTime, blockTime);
}

inline bool isOpStackIsthmus(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.isthmusTime, blockTime);
}

inline bool isOpStackJovian(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.jovianTime, blockTime);
}

}  // namespace bcos::evm

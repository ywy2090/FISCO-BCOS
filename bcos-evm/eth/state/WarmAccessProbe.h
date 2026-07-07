/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Temporary EIP-2929 warm/cold access counters (EEST_WARM_PROBE=1).
 * @file WarmAccessProbe.h
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace bcos::evm::state
{

/// Tx-scoped counters for debugging EIP-2929 warm/cold parity (remove after 6780 gas fix).
struct WarmAccessProbe
{
    uint64_t warmUpCold{0};
    uint64_t warmUpWarm{0};
    uint64_t warmUpNoJournalCold{0};
    uint64_t warmUpNoJournalWarm{0};
    uint64_t pinWarmNewInsert{0};
    uint64_t pinWarmAlreadyWarm{0};
    uint64_t hostAccessCold{0};
    uint64_t hostAccessWarm{0};

    static bool enabled() noexcept
    {
        // Thread-safe function-local static init: the mutable read-modify-write form
        // was a data race when called from concurrent tx execution.
        static const bool cached = std::getenv("EEST_WARM_PROBE") != nullptr;
        return cached;
    }

    static WarmAccessProbe& instance() noexcept
    {
        static WarmAccessProbe probe;
        return probe;
    }

    void reset() noexcept { *this = {}; }

    void print(std::ostream& out) const
    {
        out << "=== EEST_WARM_PROBE ===\n"
            << "warm_up_address cold=" << warmUpCold << " warm=" << warmUpWarm << "\n"
            << "warm_up_no_journal cold=" << warmUpNoJournalCold << " warm=" << warmUpNoJournalWarm
            << "\n"
            << "pin_warm_create new=" << pinWarmNewInsert << " already_warm=" << pinWarmAlreadyWarm
            << "\n"
            << "host access_account cold=" << hostAccessCold << " warm=" << hostAccessWarm << "\n"
            << "host_cold_delta_vs_warm_up="
            << (warmUpCold > hostAccessCold ? warmUpCold - hostAccessCold :
                                              hostAccessCold - warmUpCold)
            << "\n";
    }
};

}  // namespace bcos::evm::state

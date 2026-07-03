/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execute gas/refund/floor math for OpStack (no state mutation).
 * @file OpStackPostExecuteGas.h
 */

#pragma once

#include "bcos-evm/eth/gas/TopLevelGasSettlement.h"
#include <algorithm>
#include <cstdint>

namespace bcos::evm
{
struct GasSettlement
{
    uint64_t gasLeft{0};
    uint64_t refund{0};
    uint64_t gasRemaining{0};
    uint64_t gasUsed{0};
    uint64_t maxUsedGas{0};
};

inline GasSettlement postExecuteGasSettlement(
    uint64_t gasLimit, uint64_t gasLeft, uint64_t stateRefund, uint64_t floorDataGas)
{
    GasSettlement settlement{};
    settlement.gasLeft = std::min(gasLeft, gasLimit);

    auto const peakGasUsed = gasLimit - settlement.gasLeft;
    settlement.refund = static_cast<uint64_t>(gas::effectiveRefundEip3529(
        static_cast<int64_t>(stateRefund), static_cast<int64_t>(peakGasUsed)));

    settlement.gasUsed = gas::settleTopLevelTransactionGas(static_cast<int64_t>(gasLimit),
        static_cast<int64_t>(gasLeft), static_cast<int64_t>(stateRefund),
        static_cast<int64_t>(floorDataGas));
    settlement.gasRemaining = gasLimit - settlement.gasUsed;
    settlement.maxUsedGas = std::max(peakGasUsed, settlement.gasUsed);

    return settlement;
}
}  // namespace bcos::evm

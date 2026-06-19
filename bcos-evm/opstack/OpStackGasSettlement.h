#pragma once

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
    settlement.refund = std::min(stateRefund, peakGasUsed / 5);

    settlement.gasRemaining = settlement.gasLeft + settlement.refund;
    if (settlement.gasRemaining > gasLimit)
    {
        settlement.gasRemaining = gasLimit;
    }

    settlement.gasUsed = gasLimit - settlement.gasRemaining;
    settlement.maxUsedGas = peakGasUsed;

    if (floorDataGas > 0 && settlement.gasUsed < floorDataGas)
    {
        auto const floorUsed = std::min(floorDataGas, gasLimit);
        settlement.gasUsed = floorUsed;
        settlement.gasRemaining = gasLimit - floorUsed;
        settlement.maxUsedGas = std::max(settlement.maxUsedGas, floorUsed);
    }
    else
    {
        settlement.maxUsedGas = std::max(settlement.maxUsedGas, floorDataGas);
    }

    return settlement;
}
}  // namespace bcos::evm

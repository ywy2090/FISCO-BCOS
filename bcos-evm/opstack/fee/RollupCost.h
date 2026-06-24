#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

// Mirrors op-geth types.RollupCostData (core/types/rollup_cost.go).
struct RollupCostData
{
    uint64_t zeroes{0};
    uint64_t ones{0};
    uint64_t fastLzSize{0};

    bool isEmpty() const noexcept { return zeroes == 0 && ones == 0 && fastLzSize == 0; }
};

// Returns the length of the data after FastLZ compression (Fjord L1 cost input).
uint32_t flzCompressLen(bcos::bytesConstRef data);

RollupCostData newRollupCostData(bcos::bytesConstRef serializedTx);

}  // namespace bcos::evm

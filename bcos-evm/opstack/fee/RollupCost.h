#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

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

// Fjord 线性回归估算 tx 在 DA batch 中的字节数（放大 1e6）。中间量用 s256：
// L1_COST_INTERCEPT 为负，fastLzSize 小时中间和为负，随后由 MIN_TX_SIZE_SCALED 兜底。
bcos::s256 estimatedDASizeScaled(RollupCostData const& data) noexcept;

// = estimatedDASizeScaled / ESTIMATED_DA_SIZE_DIVISOR（对齐 op-geth RollupCostData.EstimatedDASize）。
uint64_t estimatedDASize(RollupCostData const& data) noexcept;

}  // namespace bcos::evm

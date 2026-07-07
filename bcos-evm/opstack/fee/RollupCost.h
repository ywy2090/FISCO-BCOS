/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Fjord+ L1 data-availability size estimation from serialized transactions.
 * @file RollupCost.h
 *
 * Mirrors op-geth rollup/cost/rollup_cost.go (RollupCostData, FlzCompressLen).
 * zeroes/ones are retained for parity; Fjord L1 fee uses fastLzSize only.
 *
 * Core formulas (constants in OpStackConstants.h):
 *   fastLzSize           = FlzCompressLen(serializedTx)
 *   estimatedDASizeScaled = max(L1_COST_INTERCEPT + L1_COST_FASTLZ_COEF * fastLzSize,
 *                               MIN_TX_SIZE_SCALED)
 *   estimatedDASize      = estimatedDASizeScaled / ESTIMATED_DA_SIZE_DIVISOR
 *
 * estimatedDASizeScaled is kept in s256 because L1_COST_INTERCEPT is negative; small
 * fastLzSize can yield a negative intermediate sum before the MIN_TX_SIZE_SCALED floor.
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

/// Byte statistics and FastLZ compressed length for one serialized L2 transaction.
struct RollupCostData
{
    uint64_t zeroes{0};      ///< Count of 0x00 bytes in serialized tx
    uint64_t ones{0};        ///< Count of non-zero bytes in serialized tx
    uint64_t fastLzSize{0};  ///< FlzCompressLen(serializedTx); Fjord L1 fee input

    bool isEmpty() const noexcept { return zeroes == 0 && ones == 0 && fastLzSize == 0; }
};

/// Simulated FastLZ-compressed output length (Fjord DA regression input).
uint32_t flzCompressLen(bcos::bytesConstRef data);

/// Build RollupCostData from the canonical serialized transaction bytes.
RollupCostData newRollupCostData(bcos::bytesConstRef serializedTx);

/// estimatedDASizeScaled — see file header formula block.
bcos::s256 estimatedDASizeScaled(RollupCostData const& data) noexcept;

/// estimatedDASize — see file header formula block (op-geth RollupCostData.EstimatedDASize).
uint64_t estimatedDASize(RollupCostData const& data) noexcept;

}  // namespace bcos::evm

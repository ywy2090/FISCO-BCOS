/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Per-transaction mutable fee state for the settlement pipeline.
 * @file OpStackFeeSidecar.h
 *
 * Populated by OpStackFeeSettlement::buyGas before EVM execution; consumed by
 * finalizeNormal (floorDataGas) and planOpStackPostSettlement (pre-debit amounts).
 *
 * Fields:
 *   effectiveGasPrice — min(gasFeeCap, baseFee + gasTipCap) at buyGas time
 *   baseFee           — block base fee snapshot at debit time
 *   l1CostCharged     — L1 data fee pre-debited (Fjord formula)
 *   operatorCostLimit — operator fee ceiling pre-debited; actual charge in refundGas
 *   floorDataGas      — Regolith+ min data gas; raises maxUsedGas for operator fee
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

struct OpStackFeeSidecar
{
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
    uint64_t floorDataGas{0};
};

}  // namespace bcos::evm

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Single TE gate for EIP-1559 fork + fee-market semantics.
 * @file Eip1559Gate.h
 *
 * Do not read cfg.eip1559 outside this header in eth/ production code (CI gate).
 * Typed-tx allowance, fee-cap pricing, and post-execute refund all route through here.
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"

namespace bcos::evm::gas
{

/// Fork gate: EIP-2718 type-0x02 typed tx allowed.
inline bool isEip1559TypedTxAllowed(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return cfg.eip1559;
}

/// Fee market active: tip/fee cap vs baseFee pricing (London+).
inline bool isEip1559FeeMarketActive(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return cfg.eip1559;
}

/// Post-execution gas refund gate under fee market (EIP-3529 accounting).
inline bool isEip1559GasRefundEnabled(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return cfg.eip1559;
}

}  // namespace bcos::evm::gas

/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Map revision/block/fee caps to FeeInputs (kernel-neutral; no apply types).
 * @file FeeInputsMapping.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"

namespace bcos::evm::gas
{

struct FeeCapsView
{
    bcos::u256 gasPrice;
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    uint8_t web3TypedTxKind{0};
    bool hasExplicitFeeCaps{false};
};

inline FeeInputs toFeeInputs(bcos::evm_standard::RevisionConfig const& revision,
    state::BlockInfo const& blockInfo, FeeCapsView const& caps, int64_t gasLimit) noexcept
{
    return FeeInputs{
        .revision = revision,
        .baseFee = blockInfo.baseFee,
        .gasLimit = gasLimit,
        .gasPrice = caps.gasPrice,
        .gasTipCap = caps.gasTipCap,
        .gasFeeCap = caps.gasFeeCap,
        .web3TypedTxKind = caps.web3TypedTxKind,
        .hasExplicitFeeCaps = caps.hasExplicitFeeCaps,
    };
}

}  // namespace bcos::evm::gas

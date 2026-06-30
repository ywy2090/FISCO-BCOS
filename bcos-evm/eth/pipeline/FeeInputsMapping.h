/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief map orchestration fields to FeeInputs (convenience; core stays State-free).
 * @file FeeInputsMapping.h
 */

#pragma once

#include "bcos-evm/eth/apply/ApplyReferenceMessage.h"
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

inline FeeInputs toFeeInputs(EthReferenceRequest const& input, int64_t gasLimit) noexcept
{
    return toFeeInputs(input.revisionConfig, input.blockInfo,
        FeeCapsView{input.gasPrice, input.gasTipCap, input.gasFeeCap, input.web3TypedTxKind,
            input.hasExplicitFeeCaps},
        gasLimit);
}

}  // namespace bcos::evm::gas

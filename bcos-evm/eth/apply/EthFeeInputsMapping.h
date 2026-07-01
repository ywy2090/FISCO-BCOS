/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Eth apply-layer projection of EthMessageRequest into FeeInputs.
 * @file EthFeeInputsMapping.h
 */

#pragma once

#include "bcos-evm/eth/apply/EthMessage.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"

namespace bcos::evm::gas
{

inline FeeInputs toFeeInputs(EthMessageRequest const& input, int64_t gasLimit) noexcept
{
    return toFeeInputs(input.revisionConfig, input.blockInfo,
        FeeCapsView{input.gasPrice, input.gasTipCap, input.gasFeeCap, input.web3TypedTxKind,
            input.hasExplicitFeeCaps},
        gasLimit);
}

}  // namespace bcos::evm::gas

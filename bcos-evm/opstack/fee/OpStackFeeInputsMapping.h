/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack apply-layer projection of OpStackMessageRequest into FeeInputs.
 * @file OpStackFeeInputsMapping.h
 */

#pragma once

#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"

namespace bcos::evm::gas
{

/// Legacy gasPrice for FeeCapsView: ctx.gasPrice is unset until after buyGas; request fee caps
/// carry the adapter-parsed legacy gasPrice (Eth uses input.gasPrice directly).
inline bcos::u256 legacyGasPriceForFeeCaps(OpStackSettlementProjection const& view) noexcept
{
    auto const& ctx = view.pipelineContext();
    if (ctx.gasPrice != 0)
    {
        return ctx.gasPrice;
    }
    auto const feeCap = view.gasFeeCap();
    if (feeCap != 0)
    {
        return feeCap;
    }
    return view.gasTipCap();
}

inline FeeCapsView toOpStackFeeCapsView(OpStackSettlementProjection const& view) noexcept
{
    return FeeCapsView{legacyGasPriceForFeeCaps(view), view.gasTipCap(), view.gasFeeCap(),
        view.web3TypedTxKind(), view.hasGasFeeCap()};
}

inline FeeInputs toFeeInputs(OpStackSettlementProjection const& view, int64_t gasLimit) noexcept
{
    auto const& ctx = view.pipelineContext();
    return toFeeInputs(ctx.revisionConfig, view.blockInfo(), toOpStackFeeCapsView(view), gasLimit);
}

}  // namespace bcos::evm::gas

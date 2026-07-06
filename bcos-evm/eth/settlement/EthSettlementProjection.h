/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Unified read-only facade for Eth fee planning inputs.
 * @file EthSettlementProjection.h
 */

#pragma once
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/settlement/EthFeeSidecar.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"

namespace bcos::evm
{
struct EthSettlementProjection
{
    StateTransitionContext& ctx;
    EthMessageRequest const& input;
    EthFeeSidecar& sidecar;

    StateTransitionContext& pipelineContext() noexcept { return ctx; }
    bool isCall() const noexcept { return input.isCall; }
    bcos::u256 gasTipCap() const noexcept { return input.gasTipCap; }
    bcos::u256 gasFeeCap() const noexcept { return input.gasFeeCap; }
    bcos::u256 gasPriceLegacy() const noexcept { return input.gasPrice; }
    bcos::u256 blobGasFeeCap() const noexcept { return input.blobGasFeeCap; }
    std::vector<bcos::h256> const& blobVersionedHashes() const noexcept
    {
        return input.blobVersionedHashes;
    }
    uint8_t web3TypedTxKind() const noexcept { return input.web3TypedTxKind; }
    bool hasExplicitFeeCaps() const noexcept { return input.hasExplicitFeeCaps; }
    state::BlockInfo const& blockInfo() const noexcept { return input.blockInfo; }
    bcos::u256 txValue() const noexcept { return input.txValue; }
    int64_t gasLimit() const noexcept { return ctx.originalGasLimit; }
};
}  // namespace bcos::evm

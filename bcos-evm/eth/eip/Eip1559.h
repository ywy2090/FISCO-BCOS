/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief EIP-1559 effective gas price and balance debit helpers.
 * @file Eip1559.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-utilities/Common.h"

namespace bcos::evm::gas
{

inline bool isEip1559GasCapsTx(uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx,
    bcos::evm::RevisionConfig const& cfg) noexcept
{
    if (!isEip1559FeeMarketActive(cfg))
    {
        return false;
    }
    if (web3TypedTxKind == bcos::evm::toWeb3TypedTxKindValue(bcos::evm::Web3TypedTxKind::EIP1559) ||
        web3TypedTxKind == bcos::evm::toWeb3TypedTxKindValue(bcos::evm::Web3TypedTxKind::EIP7702))
    {
        return true;
    }
    return hasExplicitFeeCapsFromTx;
}

inline bcos::u256 resolveEffectiveGasPrice(
    bcos::u256 const& gasTipCap, bcos::u256 const& gasFeeCap, bcos::u256 const& baseFee) noexcept
{
    auto const tipPlusBase = gasTipCap + baseFee;
    return gasFeeCap < tipPlusBase ? gasFeeCap : tipPlusBase;
}

inline bcos::u256 tipPerGas(bcos::u256 const& effectiveGasPrice, bcos::u256 const& baseFee) noexcept
{
    return effectiveGasPrice > baseFee ? effectiveGasPrice - baseFee : bcos::u256{0};
}

struct GasCaps
{
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    bool isEip1559Caps{false};
};

inline GasCaps normalizeGasCaps(bcos::u256 gasPrice, bcos::u256 gasTipCap, bcos::u256 gasFeeCap,
    uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx,
    bcos::evm::RevisionConfig const& cfg) noexcept
{
    GasCaps caps{};
    caps.isEip1559Caps = isEip1559GasCapsTx(web3TypedTxKind, hasExplicitFeeCapsFromTx, cfg);
    if (caps.isEip1559Caps)
    {
        caps.gasTipCap = gasTipCap;
        caps.gasFeeCap = gasFeeCap;
    }
    else
    {
        caps.gasTipCap = gasPrice;
        caps.gasFeeCap = gasPrice;
    }
    return caps;
}

inline bcos::u256 maxBalanceGasDebit(int64_t gasLimit, GasCaps const& caps) noexcept
{
    auto const price = caps.isEip1559Caps ? caps.gasFeeCap : caps.gasTipCap;
    return bcos::u256(gasLimit) * price;
}

}  // namespace bcos::evm::gas

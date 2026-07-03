/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Fee sidecar for Eth settlement coordinator.
 * @file EthFeeSidecar.h
 */

#pragma once
#include <bcos-utilities/Common.h>

namespace bcos::evm
{
struct EthFeeSidecar
{
    bcos::u256 effectiveGasPrice{0};
    int64_t penaltyGasUsed{0};  // buyGas 余额不足时 = penalty / effectiveGasPrice
};
}  // namespace bcos::evm

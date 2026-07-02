/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief GasPriceOracle.sol function selectors.
 * @file GasPriceOracleSelectors.h
 *
 * Selectors ending in the L1Block proxy group are forwarded to L1BlockPredeploy::dispatchGetter
 * without re-executing L1Block bytecode (see GasPriceOraclePredeploy::dispatchL1BlockProxyGetter).
 */

#pragma once

#include <cstdint>

namespace bcos::evm::gpo
{
// --- fee estimation ---
inline constexpr uint32_t kGetL1Fee = 0x49948e0e;
inline constexpr uint32_t kGetL1GasUsed = 0xde26c4a1;
inline constexpr uint32_t kGetOperatorFee = 0x275aedd2;

// --- L2 gas price / fork flags ---
inline constexpr uint32_t kGasPrice = 0xfe173b97;
inline constexpr uint32_t kBaseFee = 0x6ef25c3a;
inline constexpr uint32_t kDecimals = 0x313ce567;
inline constexpr uint32_t kIsEcotone = 0x4ef6e224;
inline constexpr uint32_t kIsFjord = 0x960e3a23;
inline constexpr uint32_t kIsIsthmus = 0xb54501bc;
inline constexpr uint32_t kIsJovian = 0x105d0b81;
inline constexpr uint32_t kOverhead = 0x0c18c162;
inline constexpr uint32_t kScalar = 0xf45e65d8;

// --- proxied to L1Block getters ---
inline constexpr uint32_t kBlobBaseFee = 0xf8206140;
inline constexpr uint32_t kL1BaseFee = 0x519b4bd3;
inline constexpr uint32_t kBaseFeeScalar = 0xc5985918;
inline constexpr uint32_t kBlobBaseFeeScalar = 0x68d5dca6;
}  // namespace bcos::evm::gpo

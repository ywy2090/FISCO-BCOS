/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Flat gas costs for Ethereum builtin precompiles (0x01–0x0a, 0x0100).
 * @file PrecompileGas.h
 *
 * Values match geth params/protocol_params.go. Fork-sensitive BN128 costs live here;
 * EIP-2537 MSM discount tables remain in Eip2537Gas.h; modexp in ModexpGas.h.
 */

#pragma once

#include <cstdint>

namespace bcos::evm::precompiled::gas
{

inline constexpr int64_t ECRECOVER = 3'000;

inline constexpr int64_t SHA256_WORD = 60;
inline constexpr int64_t SHA256_BASE = 12;
inline constexpr int64_t RIPEMD160_WORD = 600;
inline constexpr int64_t RIPEMD160_BASE = 120;
inline constexpr int64_t IDENTITY_WORD = 15;
inline constexpr int64_t IDENTITY_BASE = 3;

inline constexpr int64_t BN_ADD_ISTANBUL = 150;
inline constexpr int64_t BN_ADD_LEGACY = 500;
inline constexpr int64_t BN_MUL_ISTANBUL = 6'000;
inline constexpr int64_t BN_MUL_LEGACY = 40'000;
inline constexpr int64_t BN_PAIRING_BASE_ISTANBUL = 45'000;
inline constexpr int64_t BN_PAIRING_BASE_LEGACY = 100'000;
inline constexpr int64_t BN_PAIRING_PAIR_ISTANBUL = 34'000;
inline constexpr int64_t BN_PAIRING_PAIR_LEGACY = 80'000;
inline constexpr int64_t BN_PAIRING_INPUT_BYTES = 192;

inline constexpr int64_t POINT_EVALUATION = 50'000;

inline constexpr int64_t P256_VERIFY = 6'900;

}  // namespace bcos::evm::precompiled::gas

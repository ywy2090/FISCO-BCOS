/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas and EIP-7823 validation.
 *  @file ModexpGas.h
 *
 *  Precompile 0x05 computes base^exp mod mod (EIP-198). This module holds pricing and
 *  input policy only; execution lives in `EthPrecompiles.cpp` (`executeModexp` → evmone).
 *
 *  Calldata layout (96-byte header + payload):
 *    [32 baseLen][32 expLen][32 modLen][base][exp][mod]
 *
 *  Gas schedules (geth: `params/protocol_params.go` / `BigModExp`):
 *    EIP-198   — revision < Berlin
 *    EIP-2565  — Berlin .. Osaka-1 (min gas 200)
 *    EIP-7883  — Osaka+ (min gas 500)
 *
 *  EIP-7823 (Osaka+): when `RevisionConfig.eip7823`, each field length must be ≤ 1024.
 *  `shouldRejectModexpEip7823` is wired as `RejectFn` on the 0x05 table entry.
 *
 *  Filename stays `ModexpGas` because one module spans multiple EIPs.
 */
#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <cstddef>
#include <string_view>

namespace bcos::evm
{

/// Parsed modexp header lengths; `overflow` when any header exceeds uint64.
struct ModexpLengths
{
    bool overflow = false;
    size_t baseLen = 0;
    size_t expLen = 0;
    size_t modLen = 0;
};

/// EIP-7823 MUST bound (geth `bigModExp` / Besu modexp upperBound).
constexpr size_t MODEXP_MAX_FIELD_LEN_EIP7823 = 1024;

/// Fixed 96-byte header: baseLen | expLen | modLen (each 32-byte big-endian).
constexpr size_t MODEXP_HEADER_SIZE = 96;
constexpr size_t MODEXP_LEN_FIELD_SIZE = 32;
constexpr size_t MODEXP_BASE_LEN_OFFSET = 0;
constexpr size_t MODEXP_EXP_LEN_OFFSET = 32;
constexpr size_t MODEXP_MOD_LEN_OFFSET = 64;

/// Minimum modexp gas after EIP-2565 / EIP-7883 formulas.
constexpr int64_t MODEXP_MIN_GAS_EIP2565 = 200;
constexpr int64_t MODEXP_MIN_GAS_EIP7883 = 500;

/// Final gas divisors in legacy (EIP-198) and Berlin (EIP-2565) schedules.
constexpr int64_t MODEXP_GAS_DIVISOR_EIP198 = 20;
constexpr int64_t MODEXP_GAS_DIVISOR_EIP2565 = 3;

/// EIP-198 `multComplexity` piecewise thresholds on max(baseLen, modLen).
constexpr size_t MODEXP_EIP198_COMPLEXITY_SMALL_MAX = 64;
constexpr size_t MODEXP_EIP198_COMPLEXITY_MID_MAX = 1024;

/// EIP-7883: complexity = 16 when max(base, mod) ≤ 32 bytes.
constexpr size_t MODEXP_EIP7883_SMALL_INPUT_MAX = 32;
constexpr int64_t MODEXP_EIP7883_SMALL_COMPLEXITY = 16;
constexpr int64_t MODEXP_EIP7883_EXP_HEAD_BITS = 256;
constexpr int64_t MODEXP_EIP7883_EXP_BIT_MULTIPLIER = 16;

/// Parse the three 32-byte big-endian length fields at the start of modexp input.
ModexpLengths parseModexpLengths(bcos::bytesConstRef input);

/// Profile gate for EIP-7823 reject (Osaka+ in `EthChainPolicy`; independent of gas schedule).
inline bool modexpEip7823Enabled(const bcos::evm::RevisionConfig& rev) noexcept
{
    return rev.eip7823;
}

/// True when input satisfies EIP-7823 bounds at @p revision (always true pre-Osaka).
bool validateModexpEip7823(bcos::bytesConstRef input, evmc_revision revision);

/// True when 0x05 call must fail before execution (wrong address, flag off, or invalid input).
bool shouldRejectModexpEip7823(evmc_address const& addr, bcos::bytesConstRef input,
    const bcos::evm::RevisionConfig& rev, evmc_revision revision) noexcept;

bool shouldRejectModexpEip7823(std::string_view addr, bcos::bytesConstRef input,
    const bcos::evm::RevisionConfig& rev, evmc_revision revision) noexcept;

/// Select EIP-198 / 2565 / 7883 from @p revision; uses bigint to avoid overflow.
bcos::bigint calcModexpGas(bcos::bytesConstRef input, evmc_revision revision);

/// Legacy EIP-198 pricing exposed for `bcos-executor` registry unit tests only.
bcos::bigint calcModexpGasEip198Public(bcos::bytesConstRef input);

}  // namespace bcos::evm

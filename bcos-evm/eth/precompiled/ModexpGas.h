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

#include "bcos-evm/eth/RevisionConfig.h"
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

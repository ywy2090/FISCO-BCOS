/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Canonical Ethereum protocol gas constants (Yellow Paper / EIP literals).
 * @file ProtocolGas.h
 *
 * Header-only constants shared by intrinsic gas, CREATE deposit, and txpool prechecks.
 * Fork-gated behavior lives under eth/eip/ and RevisionConfig; this file holds numeric literals
 * only.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace bcos::evm::gas
{

// Numeric literals only — fork gating (when a constant applies) lives in eth/eip/*Gate.h
// and RevisionConfig. Values here match go-ethereum params/protocol_params.go.

// --- Transaction & calldata (EIP-2028 / legacy intrinsic) ---

/// Minimum gas charged for any transaction (EIP-1559 txs still pay this via intrinsic debit).
inline constexpr int64_t TX_BASE_GAS = 21'000;

/// Additional CREATE/CREATE2 overhead on top of TX_BASE_GAS.
inline constexpr int64_t CREATE_BASE_GAS = 32'000;

/// Initcode word cost for CREATE intrinsic (EIP-3860 uses the same per-word rate at deposit time).
inline constexpr int64_t INITCODE_WORD_GAS = 2;

/// EIP-2930 access-list address warm cost (debited once per listed address).
inline constexpr int64_t ACCESS_LIST_ADDRESS_COST = 2'400;

/// EIP-2930 access-list storage-key warm cost (debited per listed slot).
inline constexpr int64_t ACCESS_LIST_STORAGE_KEY_COST = 1'900;

/// Calldata zero-byte intrinsic cost (legacy + EIP-7623 "normal" component).
inline constexpr int64_t ZERO_BYTE_INTRINSIC_COST = 4;

/// Calldata non-zero-byte intrinsic cost (legacy + EIP-7623 "normal" component).
inline constexpr int64_t NONZERO_BYTE_INTRINSIC_COST = 16;

// --- Contract creation limits & runtime deposit ---

/// EIP-170 max deployed runtime code size (24 KiB).
inline constexpr size_t MAX_CODE_SIZE_EIP170 = 0x6000;

/// Gas charged per byte when persisting returned runtime code (CREATE path).
inline constexpr int64_t CODE_DEPOSIT_GAS_PER_BYTE = 200;

/// EIP-3529: max gas refund = gasUsed / REFUND_QUOTIENT (was /2 pre-London).
inline constexpr int64_t REFUND_QUOTIENT_EIP3529 = 5;

}  // namespace bcos::evm::gas

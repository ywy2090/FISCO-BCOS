/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief L1Block.sol function selectors (keccak256(sig)[0:4]).
 * @file L1BlockSelectors.h
 *
 * Values aligned with op-geth contracts-bedrock L1Block.sol ABI. Setters revert with
 * NotDepositor() unless msg.sender == OP_DEPOSITOR_ACCOUNT (sequencer deposit path).
 */

#pragma once

#include <cstdint>

namespace bcos::evm::l1block
{
// --- setters (OP_DEPOSITOR_ACCOUNT only) ---
// Isthmus+ deposit tx payload
inline constexpr uint32_t kSetL1BlockValuesIsthmus = 0x098999be;
// Jovian+ adds daFootprintGasScalar
inline constexpr uint32_t kSetL1BlockValuesJovian = 0x3db6be2b;
// NotDepositor() custom revert selector
inline constexpr uint32_t kNotDepositor = 0x3cc50b45;

// --- L1 state getters (read OpStackConstants storage slots) ---
inline constexpr uint32_t kNumber = 0x8381f58a;
inline constexpr uint32_t kTimestamp = 0xb80777ea;
inline constexpr uint32_t kBasefee = 0x5cf24969;
inline constexpr uint32_t kHash = 0x09bd5a60;
inline constexpr uint32_t kSequenceNumber = 0x64ca23ef;
inline constexpr uint32_t kBlobBaseFeeScalar = 0x68d5dca6;
inline constexpr uint32_t kBaseFeeScalar = 0xc5985918;
inline constexpr uint32_t kBatcherHash = 0xe81b2c6d;
// Slot 5 — Bedrock only, unused after Ecotone
inline constexpr uint32_t kL1FeeOverhead = 0x8b239f73;
// Slot 6 — legacy scalar, unused after Ecotone
inline constexpr uint32_t kL1FeeScalar = 0x9e8c4966;
inline constexpr uint32_t kBlobBaseFee = 0xf8206140;
inline constexpr uint32_t kOperatorFeeScalar = 0x4d5d9a2a;
inline constexpr uint32_t kOperatorFeeConstant = 0x16d3bc7f;
inline constexpr uint32_t kDaFootprintGasScalar = 0xfe3d5710;

// Ecotone+ alias getters (same storage as kBasefee / kBlobBaseFee)
inline constexpr uint32_t kL1BaseFee = 0x519b4bd3;
inline constexpr uint32_t kL1BlobBaseFee = 0x84189161;

// --- chain config / gas token metadata (synthetic defaults for non-custom-gas-token chains) ---
inline constexpr uint32_t kDepositorAccount = 0xe591b282;
inline constexpr uint32_t kIsCustomGasToken = 0x21326849;
inline constexpr uint32_t kGasPayingToken = 0x4397dfef;
inline constexpr uint32_t kGasPayingTokenName = 0xd8444715;
inline constexpr uint32_t kGasPayingTokenSymbol = 0x550fcdc9;
inline constexpr uint32_t kVersion = 0x54fd4d50;

inline constexpr uint32_t kIsFeatureEnabled = 0x47af267b;
}  // namespace bcos::evm::l1block

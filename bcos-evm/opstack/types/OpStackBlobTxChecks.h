/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack blob tx intent and versioned-hash precheck helpers.
 * @file OpStackBlobTxChecks.h
 *
 * Blob fee math (at buyGas, see OpStackPreDebitPlan):
 *   blobGasUsed  = blobCount * OP_BLOB_GAS_PER_BLOB
 *   blobDebit    = blobGasUsed * blobBaseFee
 *
 * Precheck rules:
 *   hasBlobTxIntent — EIP-4844 type, non-empty versioned hashes, or non-zero blobGasFeeCap
 *   isValidVersionedHash — first byte must be 0x01 (KZG commitment version, EIP-4844)
 */

#pragma once
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include <bcos-utilities/FixedBytes.h>

namespace bcos::evm
{
/// True when the request carries blob-transaction semantics (type-3 or blob fields set).
inline bool hasBlobTxIntent(OpStackMessageRequest const& input) noexcept
{
    return input.web3TypedTxKind == toWeb3TypedTxKindValue(Web3TypedTxKind::EIP4844) ||
           !input.blobVersionedHashes.empty() || input.blobGasFeeCap > 0;
}

/// EIP-4844 versioned hash: byte 0 must be the KZG commitment version (0x01).
inline bool isValidVersionedHash(bcos::h256 const& h) noexcept
{
    return h[0] == 0x01;
}
}  // namespace bcos::evm

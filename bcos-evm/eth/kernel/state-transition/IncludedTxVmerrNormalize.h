/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Included-tx vmerr normalization (ADR-015 + Gap 40 receipt parity).
 * @file IncludedTxVmerrNormalize.h
 */

#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <bcos-protocol/TransactionStatus.h>

namespace bcos::evm
{

inline bool isTopLevelIncludedTxVmError(evmc_status_code status, int32_t depth) noexcept
{
    if (depth != 0)
    {
        return false;  // Only top-level tx vmerr is included in block (ADR-015).
    }

    switch (status)
    {
    case EVMC_SUCCESS:
    case EVMC_REVERT:
    case EVMC_INSUFFICIENT_BALANCE:
    case EVMC_INTERNAL_ERROR:
        return false;
    default:
        return true;
    }
}

/// Backfill receipt status when EVMCResult was built with Unknown before normalization.
inline protocol::TransactionStatus receiptStatusForIncludedTxVmerr(
    protocol::TransactionStatus current, evmc_status_code code) noexcept
{
    if (current != protocol::TransactionStatus::Unknown)
    {
        return current;
    }

    switch (code)
    {
    case EVMC_OUT_OF_GAS:
        return protocol::TransactionStatus::OutOfGas;
    case EVMC_INSUFFICIENT_BALANCE:
        return protocol::TransactionStatus::NotEnoughCash;
    case EVMC_STACK_OVERFLOW:
        return protocol::TransactionStatus::OutOfStack;
    case EVMC_STACK_UNDERFLOW:
        return protocol::TransactionStatus::StackUnderflow;
    case EVMC_INVALID_INSTRUCTION:
    case EVMC_UNDEFINED_INSTRUCTION:
        return protocol::TransactionStatus::BadInstruction;
    default:
        return protocol::TransactionStatus::Unknown;
    }
}

/// Settlement: normalize status_code so TE applyStateDiff / refundGas treat included vmerr as
/// executed. Receipt: preserve failure TransactionStatus for geth receiptsRoot / RPC parity
/// (Gap 40).
inline void normalizeIncludedTxVmerr(EVMCResult& result, int32_t depth) noexcept
{
    if (!isTopLevelIncludedTxVmError(result.status_code, depth))
    {
        return;
    }
    auto const originalCode = result.status_code;
    result.status = receiptStatusForIncludedTxVmerr(result.status, originalCode);
    result.status_code = EVMC_SUCCESS;
}

// EIP-7702: delegation indicators persist when execution reverts; settlement uses SUCCESS
// status_code while receipt keeps RevertInstruction (geth failed receipt, committed auth state).
inline void normalizeSetCodeTransactionVmerr(
    EVMCResult& result, int32_t depth, bool authorizationListPresent) noexcept
{
    if (depth != 0 || !authorizationListPresent || result.status_code != EVMC_REVERT)
    {
        return;
    }
    result.status_code = EVMC_SUCCESS;
}

}  // namespace bcos::evm

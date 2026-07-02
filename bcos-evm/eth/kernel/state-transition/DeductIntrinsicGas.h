/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Kernel intrinsic gas debit before EVM entry.
 * @file DeductIntrinsicGas.h
 *
 * Mutates evmc_message.gas in place. Mode is selected by each chain's
 * StateTransitionHooks::getIntrinsicGasParams() — not hard-coded in the pipeline.
 *
 * Modes:
 *   None          — skip debit (inner calls, Fisco non-tx paths)
 *   AuthOnly      — EIP-7702 auth tuple gas only (partial pre-Osaka paths)
 *   Eip7623       — full tx intrinsic + calldata floor (Eth reference)
 *   OpStackEntry  — L2 entry intrinsic (no EIP-7623 calldata floor split)
 */

#pragma once

#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

/// Which intrinsic-gas rules apply for this top-level transition.
enum class IntrinsicDebitMode
{
    None,
    AuthOnly,
    Eip7623,
    OpStackEntry
};

/// Failure reason when deductIntrinsicGas rejects before EVM entry.
enum class IntrinsicDebitFailure
{
    None,
    GasLimitMinimum,
    CalldataOutOfGas,
    AuthTupleOutOfGas,
    OpStackIntrinsicOutOfGas
};

struct DeductIntrinsicGasParams
{
    IntrinsicDebitMode mode{IntrinsicDebitMode::None};
    bool authorizationListPresent{false};
    uint64_t authTupleCount{0};
    Eip2930AccessList const* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
};

struct DeductIntrinsicGasOutcome
{
    bool ok{false};
    IntrinsicDebitFailure failure{IntrinsicDebitFailure::None};
    int64_t gasLeftOnFailure{0};
    int64_t debitAmount{0};
};

/// Debit intrinsic gas from message.gas. On failure, message.gas is unchanged.
inline DeductIntrinsicGasOutcome deductIntrinsicGas(
    evmc_message& message, DeductIntrinsicGasParams const& policy)
{
    DeductIntrinsicGasOutcome outcome{};
    outcome.ok = true;
    outcome.failure = IntrinsicDebitFailure::None;

    switch (policy.mode)
    {
    case IntrinsicDebitMode::None:
        return outcome;

    case IntrinsicDebitMode::AuthOnly:
    {
        if (!policy.authorizationListPresent || policy.authTupleCount == 0)
        {
            return outcome;
        }
        int64_t const authCost = gas::calcAuthTupleIntrinsicGas(policy.authTupleCount);
        if (message.gas < authCost)
        {
            outcome.ok = false;
            outcome.failure = IntrinsicDebitFailure::AuthTupleOutOfGas;
            outcome.gasLeftOnFailure = message.gas;
            return outcome;
        }
        message.gas -= authCost;
        outcome.debitAmount = authCost;
        return outcome;
    }

    case IntrinsicDebitMode::Eip7623:
    {
        auto const calldataRef = bcos::bytesConstRef(message.input_data, message.input_size);
        auto const calldataGas = gas::calcEip7623CalldataGas(calldataRef);
        auto const intrinsic =
            gas::computeTxIntrinsicGas(message, policy.accessList, policy.web3TypedTxKind);
        int64_t const authCost = policy.authorizationListPresent ?
                                     gas::calcAuthTupleIntrinsicGas(policy.authTupleCount) :
                                     0;

        // Floor check: gas limit must cover intrinsic minimum (incl. auth tuples).
        if (message.gas < intrinsic.gasLimitMinimumWithAuth(authCost))
        {
            outcome.ok = false;
            outcome.failure = IntrinsicDebitFailure::GasLimitMinimum;
            outcome.gasLeftOnFailure = message.gas;
            return outcome;
        }
        // Calldata must be fully affordable before preExecutionDebit (EIP-7623).
        if (message.gas < calldataGas)
        {
            outcome.ok = false;
            outcome.failure = IntrinsicDebitFailure::CalldataOutOfGas;
            outcome.gasLeftOnFailure = message.gas;
            return outcome;
        }

        int64_t const preDebit = intrinsic.preExecutionDebit();
        int64_t const totalDebit = preDebit + authCost;
        message.gas -= preDebit;
        if (authCost > 0)
        {
            message.gas -= authCost;
        }
        outcome.debitAmount = totalDebit;
        return outcome;
    }

    case IntrinsicDebitMode::OpStackEntry:
    {
        auto const intrinsic =
            gas::computeTxIntrinsicGas(message, policy.accessList, policy.web3TypedTxKind);
        int64_t const authCost = gas::calcAuthTupleIntrinsicGas(policy.authTupleCount);
        int64_t const preDebit = intrinsic.preExecutionDebit();
        int64_t const totalDebit = preDebit + authCost;

        if (message.gas < totalDebit)
        {
            outcome.ok = false;
            outcome.failure = IntrinsicDebitFailure::OpStackIntrinsicOutOfGas;
            outcome.gasLeftOnFailure = message.gas;
            return outcome;
        }
        message.gas -= totalDebit;
        outcome.debitAmount = totalDebit;
        return outcome;
    }
    }
}

}  // namespace bcos::evm

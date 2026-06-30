#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

enum class IntrinsicDebitMode
{
    None,
    AuthOnly,
    Eip7623,
    OpStackEntry
};

enum class IntrinsicDebitFailure
{
    None,
    GasLimitMinimum,
    CalldataOutOfGas,
    AuthTupleOutOfGas,
    OpStackIntrinsicOutOfGas
};

struct IntrinsicGasDebitParams
{
    IntrinsicDebitMode mode{IntrinsicDebitMode::None};
    bool authorizationListPresent{false};
    uint64_t authTupleCount{0};
    Eip2930AccessList const* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
};

struct IntrinsicGasDebitOutcome
{
    bool ok{false};
    IntrinsicDebitFailure failure{IntrinsicDebitFailure::None};
    int64_t gasLeftOnFailure{0};
    int64_t debitAmount{0};
};

inline IntrinsicGasDebitOutcome deductIntrinsicGas(
    evmc_message& message, IntrinsicGasDebitParams const& policy)
{
    IntrinsicGasDebitOutcome outcome{};
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

        if (message.gas < intrinsic.gasLimitMinimumWithAuth(authCost))
        {
            outcome.ok = false;
            outcome.failure = IntrinsicDebitFailure::GasLimitMinimum;
            outcome.gasLeftOnFailure = message.gas;
            return outcome;
        }
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

[[deprecated("Use deductIntrinsicGas")]] inline IntrinsicGasDebitOutcome debitIntrinsicGas(
    evmc_message& message, IntrinsicGasDebitParams const& policy)
{
    return deductIntrinsicGas(message, policy);
}

}  // namespace bcos::evm

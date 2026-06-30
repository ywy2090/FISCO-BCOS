#pragma once

#include <cstdint>

namespace bcos::evm
{

/// Gas bookkeeping across stateTransitionExecute.
/// Separates intrinsic debit from EVM opcode consumption.
struct IntrinsicGasAccounting
{
    /// message.gas immediately before deductIntrinsicGas (after preCheck hooks).
    int64_t gasBeforeIntrinsicDebit{0};

    /// debitOutcome.debitAmount on successful deductIntrinsicGas; 0 otherwise.
    int64_t intrinsicGasDebited{0};

    /// message.gas right after successful intrinsic debit.
    int64_t gasAfterIntrinsicDebit{0};

    /// message.gas immediately before onInvokeInnerExecute (after onTuneInnerExecuteInput).
    /// -1 when pipeline never reached EVM entry.
    int64_t gasAtEvmEntry{-1};

    bool intrinsicDebitAttempted{false};
    bool intrinsicDebitSucceeded{false};
    bool reachedEvmEntry{false};
};

}  // namespace bcos::evm

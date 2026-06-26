#pragma once

#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementView.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

struct OpStackTxFeeLedger;

/// Normal L2 fee deep module (ADR-021 Appendix A PR2): buyGas + post-pipeline ADR-025 tree.
struct OpStackNormalFeeSettlement
{
    OpStackTxFeeLedger& ledger;

    /// Debits sender via ledger. On failure runs abort contract and fills @p output; returns false.
    task::Task<bool> buyGas(
        OpStackSettlementView view, GasPoolHooks const& gasPool, OpStackExecutionResult& output);

    /// Pre-execution reject → abort; else commit, refund, gas pool, receipt meta, stateDiff.
    task::Task<void> completeAfterPipeline(OpStackSettlementView view,
        OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool,
        OpStackExecutionResult& output);
};

}  // namespace bcos::evm

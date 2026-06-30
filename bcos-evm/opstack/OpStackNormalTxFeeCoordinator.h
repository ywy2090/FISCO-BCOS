#pragma once

#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackFee.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

struct OpStackFeeSettlement;

/// Normal L2 fee deep module: buyGas + post-pipeline tree.
struct OpStackNormalTxFeeCoordinator
{
    OpStackFeeSettlement& ledger;

    /// Debits sender via ledger. On failure runs abort contract and fills @p output; returns false.
    task::Task<bool> buyGas(
        OpStackSettlementFacade view, GasPoolHooks const& gasPool, OpStackExecutionResult& output);

    /// Pre-execution reject → abort; else commit, refund, gas pool, receipt meta, stateDiff.
    task::Task<void> completeAfterPipeline(OpStackSettlementFacade view,
        OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool,
        OpStackExecutionResult& output);
};

}  // namespace bcos::evm

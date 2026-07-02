#pragma once

// Orchestrates normal (non-deposit) L2 tx fee lifecycle in applyOpStackMessage:
// buyGas → stateTransitionExecute → completeAfterPipeline.

#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include "bcos-evm/opstack/settlement/OpStackTxFinalize.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

struct OpStackFeeSettlement;

/// Normal L2 tx fee coordinator: wraps OpStackFeeSettlement + OpStackTxFinalize.
struct OpStackNormalTxFeeCoordinator
{
    OpStackFeeSettlement& ledger;

    /// Debits sender via ledger. On failure runs abort contract and fills @p output; returns false.
    task::Task<bool> buyGas(OpStackSettlementProjection view, GasPoolHooks const& gasPool,
        OpStackMessageResult& output);

    /// Pre-execution reject → abort; else commit, refund, gas pool, receipt meta, stateDiff.
    task::Task<void> completeAfterPipeline(OpStackSettlementProjection view,
        OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool,
        OpStackMessageResult& output);
};

}  // namespace bcos::evm

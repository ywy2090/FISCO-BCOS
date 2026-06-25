#pragma once

#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{

struct OpStackFeeContext;
struct OpStackTxFeeLedger;

struct GasPoolHooks
{
    std::function<bool(uint64_t)> subGas;
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> returnGas;
};

struct OpStackSettlementResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx, OpStackFeeContext const& feeCtx, TxPipelineExitKind exitKind);

OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx, TxPipelineExitKind exitKind, evmc_status_code evmStatus);

task::Task<OpStackSettlementResult> settleNormal(TxPipelineContext& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool);

task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext& ctx,
    TxPipelineExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);

}  // namespace bcos::evm

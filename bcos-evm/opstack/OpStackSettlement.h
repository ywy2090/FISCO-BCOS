#pragma once

#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackFeeSidecar.h"
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{

struct OpStackFeeParams;
struct OpStackExecutionRequest;
struct OpStackExecutionResult;
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

bool isNormalPreExecutionReject(TxPipelineExitKind exitKind) noexcept;

void abortNormalAfterBuyGas(TxPipelineContext& ctx, GasPoolHooks const& gasPool,
    OpStackExecutionResult& output, int64_t originalGasLimit);

void projectNormalReceiptMeta(OpStackExecutionResult& output, OpStackSettlementView& view,
    OpStackFeeParams const& feeParams, OpStackSettlementResult const& settled);

OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx, OpStackFeeSidecar const& sidecar, TxPipelineExitKind exitKind);

OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx, TxPipelineExitKind exitKind, evmc_status_code evmStatus);

task::Task<OpStackSettlementResult> settleNormal(OpStackSettlementView view,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool);

task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext& ctx,
    TxPipelineExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);

/// ADR-025: abort (Intrinsic/GasAfford reject) or commit + settle + receipt meta projection.
task::Task<void> completeNormalTxAfterPipeline(OpStackSettlementView view,
    OpStackExecutionRequest& input, OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool,
    OpStackExecutionResult& output);

}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include <functional>

namespace bcos::evm
{

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

OpStackSettlementResult finalizeNormal(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool);

}  // namespace bcos::evm

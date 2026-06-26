#pragma once

#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackPrecheckPolicy.h"
#include <optional>

namespace bcos::evm::test
{

inline void populateTestFeeContext(OpStackFeeContext& feeCtx, OpStackExecutionRequest const& input)
{
    feeCtx.m_isDepositTx = isDepositTx(input);
    feeCtx.m_skipNonceChecks = input.skipNonceChecks;
    feeCtx.m_skipTransactionChecks = input.skipTransactionChecks;
    feeCtx.m_gasTipCap = input.gasTipCap;
    feeCtx.m_gasFeeCap = input.gasFeeCap;
    feeCtx.m_blockInfo = input.blockInfo;
    feeCtx.m_authTupleCount = static_cast<uint64_t>(input.authorizations.size());
    feeCtx.m_blobGasFeeCap = input.blobGasFeeCap;
    feeCtx.m_blobVersionedHashes = input.blobVersionedHashes;
    feeCtx.m_web3TypedTxKind = input.web3TypedTxKind;
    feeCtx.m_accessList = input.accessList;
}

inline std::optional<EVMCResult> runOpStackEntryPrecheck(
    OpStackExecutionRequest const& input, state::EvmStateReader const& stateView)
{
    OpStackFeeContext feeCtx;
    populateTestFeeContext(feeCtx, input);
    OpStackPrecheckPolicy policy(input, feeCtx);
    TxPipelineContext ctx{stateView, input.message, input.revisionConfig, bcos::u256(0)};
    policy.checkEntryRules(ctx);
    if (ctx.earlyExit)
    {
        return std::optional<EVMCResult>{std::move(ctx.evmcResult)};
    }
    return std::nullopt;
}

}  // namespace bcos::evm::test

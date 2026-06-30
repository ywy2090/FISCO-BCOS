#pragma once

#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"

namespace bcos::evm
{

inline void captureSettlementSnapshot(
    TxPipelineContext& ctx, ExecuteMessageOutput const& kernelOutput)
{
    if (ctx.intrinsicDebitMode != IntrinsicDebitMode::Eip7623)
    {
        return;
    }

    ctx.snapshot.gasLimit = ctx.originalGasLimit;
    ctx.snapshot.calldata =
        gas::calcEip7623Components(bytesConstRef(ctx.message.input_data, ctx.message.input_size));
    ctx.snapshot.evmGasRefund = kernelOutput.gasRefund;
}

}  // namespace bcos::evm

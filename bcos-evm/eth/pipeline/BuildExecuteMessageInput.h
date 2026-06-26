#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"

namespace bcos::evm
{

inline ExecuteMessageInput buildExecuteMessageInput(TxPipelineContext& ctx)
{
    ExecuteMessageInput input;
    input.state = &ctx.state;
    input.vm = ctx.inputs.vm;
    input.message = ctx.message;
    input.gasPrice = ctx.gasPrice;
    input.blockInfo = ctx.inputs.blockInfo;
    input.blockHashes = ctx.inputs.blockHashes;
    input.revisionConfig = ctx.revisionConfig;
    input.txProps = ctx.txProps;
    input.accessList = ctx.inputs.accessList;
    input.authorizationListPresent = ctx.inputs.authorizationListPresent;
    input.authorizations = ctx.inputs.authorizations;
    input.web3TypedTxKind = ctx.inputs.web3TypedTxKind;
    input.extension = ctx.extension;
    return input;
}

}  // namespace bcos::evm

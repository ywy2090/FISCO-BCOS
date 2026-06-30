#include "bcos-evm/eth/apply/EthMessage.h"
#include "bcos-evm/eth/apply/EthStateTransitionBindings.h"
#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include "bcos-evm/eth/policy/EthVmHostPolicy.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <span>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
std::vector<protocol::LogEntry> convertLogs(std::vector<LogEntry> const& logs)
{
    std::vector<protocol::LogEntry> out;
    out.reserve(logs.size());
    for (auto const& log : logs)
    {
        std::span addressView(log.address.bytes, sizeof(log.address.bytes));
        h256s topics;
        topics.reserve(log.topics.size());
        for (auto const& topic : log.topics)
        {
            topics.emplace_back(state::fromEvmC(topic));
        }
        out.emplace_back(
            toHex<decltype(addressView), bcos::bytes>(addressView), std::move(topics), log.data);
    }
    return out;
}
}  // namespace

task::Task<EthMessageResult> applyEthMessage(EthMessageRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("applyEthMessage requires stateView/vm/hashImpl");
    }

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("eth", input.blockInfo.number, input.txHash));

    EthMessageResult output;
    output.message = input.message;
    output.revisionConfig = input.revisionConfig;

    StateTransitionContext ctx{
        *input.stateView, input.message, input.revisionConfig, input.gasPrice};
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList;
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    trace::logMessageContext(input.message);

    state::EthVmHostPolicy extension;
    ctx.wireExecutionEnvironment(input.vm, &extension, nullptr);

    EthStateTransitionBindings::Context bindingsCtx{input, output};
    auto bindings = EthStateTransitionBindings::bind(bindingsCtx);

    stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

    EVM_LOG(DEBUG) << LOG_DESC("applyEthMessage done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                   << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                   << LOG_KV("includedTxVmError", ctx.topLevelIncludedTxVmError);

    output.evmcResult = std::move(ctx.evmcResult);
    output.topLevelIncludedTxVmError = ctx.topLevelIncludedTxVmError;
    output.receiptLogs = convertLogs(ctx.kernelOutput.logs);
    output.logs = std::move(ctx.kernelOutput.logs);
    output.message = ctx.message;
    output.stateDiff = std::move(ctx.kernelOutput.stateDiff);

    if (bindings.hooks.getIntrinsicGasParams().mode == IntrinsicDebitMode::Eip7623)
    {
        output.gasSettlementSnapshot = ctx.snapshot;
    }

    co_return output;
}

}  // namespace bcos::evm

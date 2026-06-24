#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/EthOrchestrationProfile.h"
#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-evm/eth/policy/EthHostExtension.h"
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

task::Task<ExecuteViaEthOutput> executeViaEth(ExecuteViaEthInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("executeViaEth requires stateView/vm/hashImpl");
    }

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("eth", input.blockInfo.number, input.txHash));

    ExecuteViaEthOutput output;
    output.executionContext.message = input.message;
    output.executionContext.revisionConfig = input.revisionConfig;

    OrchestrationContext ctx{*input.stateView, input.message, input.revisionConfig, input.gasPrice};
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList;
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    trace::logMessageContext(input.message);

    state::EthHostExtension ethExtension;
    ctx.extension = &ethExtension;

    EthOrchestrationProfile::Session session{input, output};
    auto hooks = EthOrchestrationProfile::buildHooks(session);
    // TODO: OrchestrationErrorPolicy (candidate 4)
    hooks.mapIntrinsicFailure = [](OrchestrationContext& orchestrationCtx, IntrinsicDebitFailure) {
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        orchestrationCtx.evmcResult =
            EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
    };

    hooks.mapException = [](OrchestrationContext& orchestrationCtx,
                             std::exception_ptr exceptionPtr) {
        try
        {
            std::rethrow_exception(exceptionPtr);
        }
        catch (protocol::OutOfGas&)
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_OUT_OF_GAS;
            failResult.gas_left = 0;
            orchestrationCtx.evmcResult =
                EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
        }
        catch (std::exception&)
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_INTERNAL_ERROR;
            failResult.gas_left = 0;
            orchestrationCtx.evmcResult =
                EVMCResult(failResult, protocol::TransactionStatus::Unknown);
        }

        if (orchestrationCtx.state.has_checkpoint())
        {
            orchestrationCtx.state.revert();
        }
    };

    runOrchestration(ctx, hooks);

    EVM_LOG(DEBUG) << LOG_DESC("executeViaEth done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                   << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                   << LOG_KV("includedTxVmError", output.topLevelIncludedTxVmError);

    output.evmcResult = std::move(ctx.evmcResult);
    output.executionContext.logs = convertLogs(ctx.kernelOutput.logs);
    output.logs = std::move(ctx.kernelOutput.logs);
    output.executionContext.message = ctx.message;
    output.stateDiff = std::move(ctx.kernelOutput.stateDiff);

    if (hooks.intrinsicPolicy.mode == IntrinsicDebitMode::Eip7623)
    {
        output.executionContext.gasSettlementSnapshot = ctx.snapshot;
    }

    co_return output;
}

}  // namespace bcos::evm

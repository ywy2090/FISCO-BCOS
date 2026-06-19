#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include <stdexcept>

namespace bcos::evm
{
namespace
{
EVMCResult adoptResult(evmc::Result&& result, const bcos::crypto::Hash& hashImpl)
{
    auto raw = result.release_raw();
    auto [status, ignored] = evmcStatusToErrorMessage(hashImpl, raw.status_code);
    (void)ignored;
    return EVMCResult(raw, status);
}
}  // namespace

task::Task<OpStackExecuteViaHostOutput> opStackExecuteViaHost(OpStackExecuteViaHostInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("opStackExecuteViaHost requires stateView/vm/hashImpl");
    }

    OpStackExecuteViaHostOutput output;
    state::State state(*input.stateView);
    OpHostExtension extension;

    OpStackTxExecutor::OpStackTxExecutionData txData;
    txData.m_call = input.call;
    txData.m_isDepositTx = input.isDepositTx;
    txData.m_state = &state;
    txData.m_message = input.message;
    txData.m_gasPrice = input.gasPrice;
    txData.m_gasTipCap = input.gasTipCap;
    txData.m_gasFeeCap = input.gasFeeCap;
    txData.m_hasGasFeeCap = input.hasGasFeeCap;
    txData.m_gasLimit = input.message.gas;
    txData.m_blockInfo = input.blockInfo;
    txData.m_skipNonceChecks = input.skipNonceChecks;
    txData.m_skipTransactionChecks = input.skipTransactionChecks;
    txData.m_noBaseFee = input.noBaseFee;
    txData.m_floorDataGas = input.floorDataGas;
    txData.m_rollupCostData = input.rollupCostData;

    auto buyGasOk = co_await input.opTxExecutor.buyGas(txData);
    if (!buyGasOk)
    {
        output.evmcResult = std::move(*txData.m_evmcResult);
        co_return output;
    }

    auto executeOutput = executeMessage(ExecuteMessageInput{.stateView = &state,
        .vm = input.vm,
        .message = input.message,
        .gasPrice = input.gasPrice,
        .blockInfo = input.blockInfo,
        .blockHashes = input.blockHashes,
        .revisionConfig = input.revisionConfig,
        .txProps = input.txProps,
        .accessList = input.accessList,
        .web3TypedTxKind = input.web3TypedTxKind,
        .extension = &extension,
        .fixStorageStatus = true});

    output.evmcResult = adoptResult(std::move(executeOutput.result), *input.hashImpl);
    output.logs = std::move(executeOutput.logs);
    evmc_result settlementResult{};
    settlementResult.status_code = output.evmcResult.status_code;
    settlementResult.gas_left = output.evmcResult.gas_left;
    txData.m_evmcResult.emplace(settlementResult, output.evmcResult.status);
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, output.evmcResult.gas_left)),
            state.get_refund(), txData.m_floorDataGas);
    txData.m_gasRemaining = settlement.gasRemaining;
    txData.m_maxUsedGas = settlement.maxUsedGas;
    txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);

    co_await input.opTxExecutor.refundGas(txData);

    output.gasUsed = txData.m_gasUsed;
    output.stateDiff = state.build_diff();
    co_return output;
}
}  // namespace bcos::evm

#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackFee.h"
#include "bcos-evm/opstack/OpStackFloorGas.h"
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

std::optional<EVMCResult> executeEntryChecks(OpStackTxExecutor::OpStackTxExecutionData& txData)
{
    bcos::bytesConstRef inputData{txData.m_message.input_data, txData.m_message.input_size};
    auto const floorCheck = executeEntryFloorDataGasCheck(
        static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)), inputData);
    txData.m_floorDataGas = floorCheck.floorGas;
    if (floorCheck.ok)
    {
        return std::nullopt;
    }

    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
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
    auto const feeParams = loadOpStackFeeParams(state);
    input.opTxExecutor.m_l1CostFunc = [feeParams](RollupCostData const& data, uint64_t) {
        return l1CostFjord(data, feeParams);
    };
    input.opTxExecutor.m_operatorCostFunc = [feeParams](uint64_t gas, uint64_t) {
        return operatorCostIsthmus(gas, feeParams);
    };

    OpStackTxExecutor::OpStackTxExecutionData txData;
    txData.m_call = input.call;
    txData.m_isDepositTx = input.isDepositTx;
    txData.m_state = &state;
    txData.m_message = input.message;
    txData.m_gasTipCap = input.gasTipCap;
    txData.m_gasFeeCap = input.gasFeeCap;
    txData.m_hasGasFeeCap = true;
    txData.m_effectiveGasPrice =
        resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, input.blockInfo.baseFee);
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

    if (auto entryError = executeEntryChecks(txData); entryError.has_value())
    {
        txData.m_evmcResult = std::move(*entryError);
    }

    if (!txData.m_evmcResult.has_value())
    {
        auto executeOutput = executeMessage(ExecuteMessageInput{.stateView = &state,
            .vm = input.vm,
            .message = input.message,
            .gasPrice = txData.m_effectiveGasPrice,
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
    }
    else
    {
        output.evmcResult = std::move(*txData.m_evmcResult);
    }

    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, txData.m_evmcResult->gas_left)),
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

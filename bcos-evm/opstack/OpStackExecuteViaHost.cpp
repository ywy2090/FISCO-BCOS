#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackFee.h"
#include "bcos-evm/opstack/OpStackFloorGas.h"
#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include "bcos-evm/opstack/OpStackPreCheck.h"
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

uint64_t computeIntrinsicGasDebit(OpStackTxExecutor::OpStackTxExecutionData const& txData)
{
    auto const intrinsic =
        gas::computeTxIntrinsicGas(txData.m_message, txData.m_accessList, txData.m_web3TypedTxKind);
    auto const base = static_cast<uint64_t>(std::max<int64_t>(0, intrinsic.preExecutionDebit()));
    return base + static_cast<uint64_t>(std::max<int64_t>(
                      0, gas::calcAuthTupleIntrinsicGas(txData.m_authTupleCount)));
}

std::optional<EVMCResult> executeEntryChecks(OpStackTxExecutor::OpStackTxExecutionData& txData)
{
    auto const availableGas = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_message.gas));
    auto const intrinsicGas = computeIntrinsicGasDebit(txData);
    if (availableGas < intrinsicGas)
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
    }

    auto const value = state::fromEvmC(txData.m_message.value);
    if (!txData.m_skipTransactionChecks && value != 0 &&
        !canTransfer(*txData.m_state, txData.m_message.sender, value))
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        return EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
    }

    bcos::bytesConstRef inputData{txData.m_message.input_data, txData.m_message.input_size};
    auto const floorCheck = executeEntryFloorDataGasCheck(
        static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)), inputData);
    txData.m_floorDataGas = floorCheck.floorGas;
    if (floorCheck.ok)
    {
        txData.m_message.gas -= static_cast<int64_t>(intrinsicGas);
        return std::nullopt;
    }

    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
}

struct GasPoolReturnGuard
{
    std::function<void(uint64_t, uint64_t)>* hook{};
    uint64_t gasRemaining{0};
    uint64_t gasUsed{0};
    bool armed{false};

    ~GasPoolReturnGuard()
    {
        if (armed && hook && *hook)
        {
            (*hook)(gasRemaining, gasUsed);
        }
    }
};

void returnDepositPoolGas(OpStackExecuteViaHostInput const& input,
    OpStackTxExecutor::OpStackTxExecutionData const& txData)
{
    if (!input.gasPoolReturnGasHook)
    {
        return;
    }
    auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
    auto const gasRemaining = gasLimit > gasUsed ? gasLimit - gasUsed : 0;
    input.gasPoolReturnGasHook(gasRemaining, gasUsed);
}
}  // namespace

task::Task<OpStackExecuteViaHostOutput> opStackExecuteViaHost(OpStackExecuteViaHostInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("opStackExecuteViaHost requires stateView/vm/hashImpl");
    }

    if (bcos::evm_standard::isIsthmusOrchestrationProfile(input.revisionConfig))
    {
        input.opTxExecutor.m_isIsthmus = true;
    }

    OpStackExecuteViaHostOutput output;
    state::State state(*input.stateView);
    OpHostExtension extension(&state);
    auto const feeParams = loadOpStackFeeParams(state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, state);

    OpStackTxExecutor::OpStackTxExecutionData txData;
    txData.m_call = input.call;
    txData.m_isDepositTx = isDepositTx(input);
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
    txData.m_accessList = input.accessList;
    txData.m_web3TypedTxKind = input.web3TypedTxKind;
    txData.m_authTupleCount = static_cast<uint64_t>(input.authorizations.size());
    txData.m_blobGasFeeCap = input.blobGasFeeCap;
    txData.m_blobVersionedHashes = input.blobVersionedHashes;
    txData.m_rollupCostData = input.rollupCostData;

    if (auto preCheckError = opStackPreCheck(input, state); preCheckError.has_value())
    {
        output.evmcResult = std::move(*preCheckError);
        co_return output;
    }

    if (txData.m_isDepositTx)
    {
        output.receiptMeta.depositNonce = state.get_nonce(input.message.sender);
        if (input.depositTx.has_value() && input.depositTx->mint.has_value() &&
            *input.depositTx->mint > 0)
        {
            state.set_balance(input.message.sender,
                state.get_balance(input.message.sender) + *input.depositTx->mint);
        }

        state.checkpoint();
        if (auto entryError = executeEntryChecks(txData); entryError.has_value())
        {
            output.evmcResult = std::move(*entryError);
            state.revert();
            auto const nonce = state.get_nonce(input.message.sender);
            state.set_nonce(input.message.sender, nonce + 1);
            txData.m_gasUsed = std::max<int64_t>(0, txData.m_gasLimit);
            output.gasUsed = txData.m_gasUsed;
            output.stateDiff = state.build_diff();
            returnDepositPoolGas(input, txData);
            co_return output;
        }

        auto executeOutput = executeMessage(ExecuteMessageInput{.stateView = &state,
            .vm = input.vm,
            .message = input.message,
            .gasPrice = 0,
            .blockInfo = input.blockInfo,
            .blockHashes = input.blockHashes,
            .revisionConfig = input.revisionConfig,
            .txProps = input.txProps,
            .accessList = input.accessList,
            .authorizationListPresent = input.authorizationListPresent,
            .authorizations = input.authorizations,
            .web3TypedTxKind = input.web3TypedTxKind,
            .extension = &extension,
            .fixStorageStatus = true});

        output.evmcResult = adoptResult(std::move(executeOutput.result), *input.hashImpl);
        output.logs = std::move(executeOutput.logs);

        if (output.evmcResult.status_code == EVMC_SUCCESS)
        {
            auto const settlement = postExecuteGasSettlement(
                static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
                static_cast<uint64_t>(std::max<int64_t>(0, output.evmcResult.gas_left)),
                state.get_refund(), txData.m_floorDataGas);
            txData.m_gasRemaining = settlement.gasRemaining;
            txData.m_maxUsedGas = settlement.maxUsedGas;
            txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
            output.gasUsed = txData.m_gasUsed;
            auto const nonce = state.get_nonce(input.message.sender);
            state.set_nonce(input.message.sender, nonce + 1);
            state.commit();
        }
        else
        {
            auto const settlement = postExecuteGasSettlement(
                static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
                static_cast<uint64_t>(std::max<int64_t>(0, output.evmcResult.gas_left)),
                state.get_refund(), txData.m_floorDataGas);
            state.revert();
            auto const nonce = state.get_nonce(input.message.sender);
            state.set_nonce(input.message.sender, nonce + 1);
            txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
            output.gasUsed = txData.m_gasUsed;
        }

        output.stateDiff = state.build_diff();
        returnDepositPoolGas(input, txData);
        co_return output;
    }

    if (input.gasPoolSubGasHook)
    {
        auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, input.message.gas));
        if (!input.gasPoolSubGasHook(gasLimitForPool))
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_OUT_OF_GAS;
            failResult.gas_left = 0;
            output.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
            co_return output;
        }
    }

    GasPoolReturnGuard guard{&input.gasPoolReturnGasHook};

    auto buyGasOk = co_await input.opTxExecutor.buyGas(txData);
    if (!buyGasOk)
    {
        output.evmcResult = std::move(*txData.m_evmcResult);
        co_return output;
    }

    guard.armed = true;

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
            .authorizationListPresent = input.authorizationListPresent,
            .authorizations = input.authorizations,
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
    output.receiptMeta.l1Fee = txData.m_l1CostCharged;
    if (isOpStackIsthmus(input.forkSchedule, txData.m_blockInfo.timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
        output.receiptMeta.operatorFee =
            input.opTxExecutor.m_operatorCostFunc(gasUsed, txData.m_blockInfo.timestamp);
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
    output.stateDiff = state.build_diff();
    guard.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasRemaining));
    guard.gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
    co_return output;
}
}  // namespace bcos::evm

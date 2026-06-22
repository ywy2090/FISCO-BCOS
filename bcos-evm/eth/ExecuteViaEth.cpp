#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/policy/EthHostExtension.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include <span>
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

    auto message = input.message;
    ExecuteViaEthOutput output;
    output.executionContext.message = message;
    output.executionContext.revisionConfig = input.revisionConfig;

    state::State state(*input.stateView);
    state::TransactionProperties txProps;
    execution::setWarmDestinationFromKind(txProps, message.kind);

    state::EthHostExtension extension;

    try
    {
        if (input.revisionConfig.eip7623)
        {
            auto const components =
                gas::calcEip7623Components(bytesConstRef(message.input_data, message.input_size));
            if (message.gas < components.normalCost)
            {
                evmc_result failResult{};
                failResult.status_code = EVMC_OUT_OF_GAS;
                failResult.gas_left = 0;
                output.evmcResult =
                    EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
                co_return output;
            }
            message.gas -= components.normalCost;
        }

        if (input.revisionConfig.eip7623)
        {
            auto const intrinsic =
                gas::computeTxIntrinsicGas(message, input.accessList, input.web3TypedTxKind);
            output.executionContext.gasSettlementSnapshot.gasLimit = input.message.gas;
            output.executionContext.gasSettlementSnapshot.gasBeforeEvm = message.gas;
            output.executionContext.gasSettlementSnapshot.calldata =
                gas::calcEip7623Components(bytesConstRef(message.input_data, message.input_size));
            output.executionContext.gasSettlementSnapshot.fixedIntrinsic = intrinsic.fixedCost();
            output.executionContext.gasSettlementSnapshot.createTerm = intrinsic.createIntrinsic;
        }

        auto const txValue = state::fromEvmC(message.value);
        if (txValue != 0 && !canTransfer(state, message.sender, txValue))
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
            failResult.gas_left = 0;
            output.evmcResult =
                EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
            co_return output;
        }

        auto executeOutput = executeMessage(ExecuteMessageInput{.stateView = &state,
            .vm = input.vm,
            .message = message,
            .gasPrice = input.gasPrice,
            .blockInfo = input.blockInfo,
            .blockHashes = input.blockHashes,
            .revisionConfig = input.revisionConfig,
            .txProps = txProps,
            .accessList = input.accessList,
            .authorizationListPresent = input.authorizationListPresent,
            .authorizations = input.authorizations,
            .web3TypedTxKind = input.web3TypedTxKind,
            .extension = &extension,
            .fixStorageStatus = true});

        output.evmcResult = adoptResult(std::move(executeOutput.result), *input.hashImpl);
        output.logs = executeOutput.logs;
        output.executionContext.logs = convertLogs(executeOutput.logs);
        output.executionContext.message = message;

        if (output.evmcResult.status_code == EVMC_SUCCESS)
        {
            output.stateDiff = std::move(executeOutput.stateDiff);
            output.executionContext.gasSettlementSnapshot.evmGasRefund = executeOutput.gasRefund;
        }
    }
    catch (protocol::OutOfGas&)
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        output.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
        if (state.has_checkpoint())
        {
            state.revert();
        }
    }
    catch (std::exception&)
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_INTERNAL_ERROR;
        failResult.gas_left = 0;
        output.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
        if (state.has_checkpoint())
        {
            state.revert();
        }
    }

    co_return output;
}

}  // namespace bcos::evm

#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/policy/VmHostPolicy.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

enum class OrchestrationExitKind
{
    None,
    PreExecuteRejected,
    PreDebitRejected,
    IntrinsicRejected,
    KernelCompleted,
    ExceptionMapped
};

struct OrchestrationInputs
{
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    Eip2930AccessList const* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint8_t web3TypedTxKind{0};
};

class OrchestrationContext
{
public:
    OrchestrationContext(state::StateView const& stateView, evmc_message inputMessage,
        bcos::evm_standard::RevisionConfig inputRevisionConfig, intx::uint256 inputGasPrice)
      : message(inputMessage),
        originalGasLimit(inputMessage.gas),
        state(stateView),
        gasPrice(intxToU256(inputGasPrice)),
        revisionConfig(inputRevisionConfig)
    {
        execution::setWarmDestinationFromKind(txProps, message.kind);
    }

    OrchestrationContext(state::StateView const& stateView, evmc_message inputMessage,
        bcos::evm_standard::RevisionConfig inputRevisionConfig, bcos::u256 inputGasPrice)
      : message(inputMessage),
        originalGasLimit(inputMessage.gas),
        state(stateView),
        gasPrice(inputGasPrice),
        revisionConfig(inputRevisionConfig)
    {
        execution::setWarmDestinationFromKind(txProps, message.kind);
    }

    OrchestrationContext(OrchestrationContext const&) = delete;
    OrchestrationContext& operator=(OrchestrationContext const&) = delete;
    OrchestrationContext(OrchestrationContext&&) = delete;
    OrchestrationContext& operator=(OrchestrationContext&&) = delete;

    OrchestrationInputs inputs;
    evmc_message message{};
    int64_t originalGasLimit{0};
    state::State state;
    bcos::u256 gasPrice{0};
    state::VmHostPolicy* extension{nullptr};
    state::TransactionProperties txProps{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    gas::TxGasSettlementSnapshot snapshot{};
    ExecuteMessageOutput kernelOutput{};
    EVMCResult evmcResult{evmc_result{}};
    bool earlyExit{false};
    OrchestrationExitKind exitKind{OrchestrationExitKind::None};
    IntrinsicDebitMode intrinsicDebitMode{IntrinsicDebitMode::None};

private:
    static bcos::u256 intxToU256(intx::uint256 const& value)
    {
        evmc_bytes32 bytes{};
        intx::be::store(bytes.bytes, value);
        return fromBigEndian<bcos::u256>(bytes.bytes);
    }
};

}  // namespace bcos::evm

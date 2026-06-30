#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/host/EvmHostHooks.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <stdexcept>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

struct ChainPrecompileDispatch;

enum class StateTransitionExitKind
{
    None,
    RulesRejected,
    GasAffordRejected,
    IntrinsicRejected,
    Completed,
    ExceptionHandled
};

struct StateTransitionInputs
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

class StateTransitionContext
{
public:
    StateTransitionContext(state::StateView const& stateView, evmc_message inputMessage,
        bcos::evm_standard::RevisionConfig inputRevisionConfig, intx::uint256 inputGasPrice)
      : message(inputMessage),
        originalGasLimit(inputMessage.gas),
        state(stateView),
        gasPrice(intxToU256(inputGasPrice)),
        revisionConfig(inputRevisionConfig)
    {
        execution::setWarmDestinationFromKind(txProps, message.kind);
    }

    StateTransitionContext(state::StateView const& stateView, evmc_message inputMessage,
        bcos::evm_standard::RevisionConfig inputRevisionConfig, bcos::u256 inputGasPrice)
      : message(inputMessage),
        originalGasLimit(inputMessage.gas),
        state(stateView),
        gasPrice(inputGasPrice),
        revisionConfig(inputRevisionConfig)
    {
        execution::setWarmDestinationFromKind(txProps, message.kind);
    }

    StateTransitionContext(StateTransitionContext const&) = delete;
    StateTransitionContext& operator=(StateTransitionContext const&) = delete;
    StateTransitionContext(StateTransitionContext&&) = delete;
    StateTransitionContext& operator=(StateTransitionContext&&) = delete;

    /// Chain Bundle injection: vm (required), host hooks, call-target port (optional).
    void wireExecutionEnvironment(
        evmc::VM* vm, state::EvmHostHooks* extension, ChainPrecompileDispatch* chainPort)
    {
        if (vm == nullptr)
        {
            throw std::invalid_argument(
                "StateTransitionContext::wireExecutionEnvironment requires vm");
        }
        inputs.vm = vm;
        this->extension = extension;
        this->chainPort = chainPort;
    }

    InnerExecuteInput toInnerExecuteInput() const
    {
        InnerExecuteInput input;
        input.state = const_cast<state::State*>(&state);
        input.vm = inputs.vm;
        input.message = message;
        input.gasPrice = gasPrice;
        input.blockInfo = inputs.blockInfo;
        input.blockHashes = inputs.blockHashes;
        input.revisionConfig = revisionConfig;
        input.txProps = txProps;
        input.accessList = inputs.accessList;
        input.authorizationListPresent = inputs.authorizationListPresent;
        input.authorizations = inputs.authorizations;
        input.web3TypedTxKind = inputs.web3TypedTxKind;
        input.extension = extension;
        input.chainPort = chainPort;
        return input;
    }

    StateTransitionInputs inputs;
    evmc_message message{};
    int64_t originalGasLimit{0};
    state::State state;
    bcos::u256 gasPrice{0};
    state::EvmHostHooks* extension{nullptr};
    ChainPrecompileDispatch* chainPort{nullptr};
    state::TransactionProperties txProps{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    gas::TxGasSettlementSnapshot snapshot{};
    InnerExecuteOutput kernelOutput{};
    EVMCResult evmcResult{evmc_result{}};
    bool earlyExit{false};
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    IntrinsicDebitMode intrinsicDebitMode{IntrinsicDebitMode::None};
    /// Eth-only: set by EthStateTransitionErrorPolicy when top-level vmerr is included in block.
    bool topLevelIncludedTxVmError{false};

private:
    static bcos::u256 intxToU256(intx::uint256 const& value)
    {
        evmc_bytes32 bytes{};
        intx::be::store(bytes.bytes, value);
        return fromBigEndian<bcos::u256>(bytes.bytes);
    }
};

}  // namespace bcos::evm

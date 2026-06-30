#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/InnerExecute.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/state/EvmHostHooks.h"
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

struct ChainCallTargetDispatcher;
struct EvmTxContextView;

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

    StateTransitionInputs inputs;
    evmc_message message{};
    int64_t originalGasLimit{0};
    state::State state;
    bcos::u256 gasPrice{0};
    state::EvmHostHooks* extension{nullptr};
    ChainCallTargetDispatcher* chainPort{nullptr};
    EvmTxContextView const* txContextView{nullptr};
    state::TransactionProperties txProps{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    gas::TxGasSettlementSnapshot snapshot{};
    InnerExecuteOutput kernelOutput{};
    EVMCResult evmcResult{evmc_result{}};
    bool earlyExit{false};
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    IntrinsicDebitMode intrinsicDebitMode{IntrinsicDebitMode::None};
    /// Eth-only: set by EthOrchestrationErrorPolicy when top-level vmerr is included in block.
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

#pragma once

#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/opstack/OpStackFeeSidecar.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-utilities/Common.h>
#include <optional>
#include <vector>

namespace bcos::evm
{

struct OpStackExecutionRequest;

/// Read-only projection over pipeline ctx + execution request + fee sidecar.
struct OpStackSettlementFacade
{
    StateTransitionContext& ctx;
    OpStackExecutionRequest const& input;
    OpStackFeeSidecar& sidecar;

    StateTransitionContext& pipelineContext() noexcept { return ctx; }
    StateTransitionContext const& pipelineContext() const noexcept { return ctx; }

    OpStackFeeSidecar& mutableSidecar() noexcept { return sidecar; }
    OpStackFeeSidecar const& feeSidecar() const noexcept { return sidecar; }

    bool isCall() const noexcept;
    bool isDeposit() const noexcept;
    bool skipNonceChecks() const noexcept;
    bool skipTransactionChecks() const noexcept;
    bool noBaseFee() const noexcept;

    /// OpStack typed txs always carry EIP-1559 fee caps on the execution request.
    bool hasGasFeeCap() const noexcept;

    bcos::u256 gasTipCap() const noexcept;
    bcos::u256 gasFeeCap() const noexcept;
    bcos::u256 blobGasFeeCap() const noexcept;
    state::BlockInfo const& blockInfo() const noexcept;
    Eip2930AccessList const* accessList() const noexcept;
    uint8_t web3TypedTxKind() const noexcept;
    uint64_t authTupleCount() const noexcept;
    std::vector<bcos::h256> const& blobVersionedHashes() const noexcept;
    std::optional<RollupCostData> const& rollupCostData() const noexcept;

    bcos::u256 effectiveGasPrice() const;
};

}  // namespace bcos::evm

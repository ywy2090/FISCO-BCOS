#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-utilities/Common.h"
#include "eth/eip/Eip2930AccessList.h"
#include "eth/eip/Eip7702.h"
#include "eth/gas/GasSettlementTypes.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "opstack/settlement/OpStackFeeSidecar.h"

namespace bcos::evm
{

bool OpStackSettlementProjection::isCall() const noexcept
{
    return input.call;
}

bool OpStackSettlementProjection::isDeposit() const noexcept
{
    return isDepositTx(input);
}

bool OpStackSettlementProjection::skipNonceChecks() const noexcept
{
    return input.skipNonceChecks;
}

bool OpStackSettlementProjection::skipTransactionChecks() const noexcept
{
    return input.skipTransactionChecks;
}

bool OpStackSettlementProjection::noBaseFee() const noexcept
{
    return input.noBaseFee;
}

bool OpStackSettlementProjection::hasGasFeeCap() const noexcept
{
    // OpStack typed txs always carry EIP-1559 fee caps on the execution request.
    return true;
}

bcos::u256 OpStackSettlementProjection::gasTipCap() const noexcept
{
    return input.gasTipCap;
}

bcos::u256 OpStackSettlementProjection::gasFeeCap() const noexcept
{
    return input.gasFeeCap;
}

bcos::u256 OpStackSettlementProjection::blobGasFeeCap() const noexcept
{
    return input.blobGasFeeCap;
}

state::BlockInfo const& OpStackSettlementProjection::blockInfo() const noexcept
{
    return input.blockInfo;
}

Eip2930AccessList const* OpStackSettlementProjection::accessList() const noexcept
{
    return input.accessList;
}

uint8_t OpStackSettlementProjection::web3TypedTxKind() const noexcept
{
    return input.web3TypedTxKind;
}

uint64_t OpStackSettlementProjection::authTupleCount() const noexcept
{
    return static_cast<uint64_t>(input.authorizations.size());
}

std::vector<bcos::h256> const& OpStackSettlementProjection::blobVersionedHashes() const noexcept
{
    return input.blobVersionedHashes;
}

std::optional<RollupCostData> const& OpStackSettlementProjection::rollupCostData() const noexcept
{
    return input.rollupCostData;
}

bcos::u256 OpStackSettlementProjection::effectiveGasPrice() const
{
    // Prefer sidecar value set by buyGas; fall back to pre-execution EIP-1559 plan.
    if (sidecar.effectiveGasPrice != 0)
    {
        return sidecar.effectiveGasPrice;
    }
    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, input.blockInfo,
        gas::FeeCapsView{
            ctx.gasPrice, input.gasTipCap, input.gasFeeCap, input.web3TypedTxKind, true},
        ctx.originalGasLimit);
    return gas::planPreExecution(feeInputs).effectiveGasPrice;
}

}  // namespace bcos::evm

#include "bcos-evm/opstack/OpStackSettlementView.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/pipeline/FeeInputsProjection.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"

namespace bcos::evm
{

bool OpStackSettlementView::isCall() const noexcept
{
    return input.call;
}

bool OpStackSettlementView::isDeposit() const noexcept
{
    return isDepositTx(input);
}

bool OpStackSettlementView::skipNonceChecks() const noexcept
{
    return input.skipNonceChecks;
}

bool OpStackSettlementView::skipTransactionChecks() const noexcept
{
    return input.skipTransactionChecks;
}

bool OpStackSettlementView::noBaseFee() const noexcept
{
    return input.noBaseFee;
}

bool OpStackSettlementView::hasGasFeeCap() const noexcept
{
    return true;
}

bcos::u256 OpStackSettlementView::gasTipCap() const noexcept
{
    return input.gasTipCap;
}

bcos::u256 OpStackSettlementView::gasFeeCap() const noexcept
{
    return input.gasFeeCap;
}

bcos::u256 OpStackSettlementView::blobGasFeeCap() const noexcept
{
    return input.blobGasFeeCap;
}

state::BlockInfo const& OpStackSettlementView::blockInfo() const noexcept
{
    return input.blockInfo;
}

Eip2930AccessList const* OpStackSettlementView::accessList() const noexcept
{
    return input.accessList;
}

uint8_t OpStackSettlementView::web3TypedTxKind() const noexcept
{
    return input.web3TypedTxKind;
}

uint64_t OpStackSettlementView::authTupleCount() const noexcept
{
    return static_cast<uint64_t>(input.authorizations.size());
}

std::vector<bcos::h256> const& OpStackSettlementView::blobVersionedHashes() const noexcept
{
    return input.blobVersionedHashes;
}

std::optional<RollupCostData> const& OpStackSettlementView::rollupCostData() const noexcept
{
    return input.rollupCostData;
}

bcos::u256 OpStackSettlementView::effectiveGasPrice() const
{
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

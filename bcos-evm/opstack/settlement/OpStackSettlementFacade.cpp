#include "bcos-evm/opstack/settlement/OpStackSettlementFacade.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"

namespace bcos::evm
{

bool OpStackSettlementFacade::isCall() const noexcept
{
    return input.call;
}

bool OpStackSettlementFacade::isDeposit() const noexcept
{
    return isDepositTx(input);
}

bool OpStackSettlementFacade::skipNonceChecks() const noexcept
{
    return input.skipNonceChecks;
}

bool OpStackSettlementFacade::skipTransactionChecks() const noexcept
{
    return input.skipTransactionChecks;
}

bool OpStackSettlementFacade::noBaseFee() const noexcept
{
    return input.noBaseFee;
}

bool OpStackSettlementFacade::hasGasFeeCap() const noexcept
{
    return true;
}

bcos::u256 OpStackSettlementFacade::gasTipCap() const noexcept
{
    return input.gasTipCap;
}

bcos::u256 OpStackSettlementFacade::gasFeeCap() const noexcept
{
    return input.gasFeeCap;
}

bcos::u256 OpStackSettlementFacade::blobGasFeeCap() const noexcept
{
    return input.blobGasFeeCap;
}

state::BlockInfo const& OpStackSettlementFacade::blockInfo() const noexcept
{
    return input.blockInfo;
}

Eip2930AccessList const* OpStackSettlementFacade::accessList() const noexcept
{
    return input.accessList;
}

uint8_t OpStackSettlementFacade::web3TypedTxKind() const noexcept
{
    return input.web3TypedTxKind;
}

uint64_t OpStackSettlementFacade::authTupleCount() const noexcept
{
    return static_cast<uint64_t>(input.authorizations.size());
}

std::vector<bcos::h256> const& OpStackSettlementFacade::blobVersionedHashes() const noexcept
{
    return input.blobVersionedHashes;
}

std::optional<RollupCostData> const& OpStackSettlementFacade::rollupCostData() const noexcept
{
    return input.rollupCostData;
}

bcos::u256 OpStackSettlementFacade::effectiveGasPrice() const
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

#pragma once
#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

struct OpStackSettlementResult;

struct OpStackFeeContext
{
    bool m_call{false};
    bool m_isDepositTx{false};
    bool m_skipNonceChecks{false};
    bool m_skipTransactionChecks{false};
    bool m_noBaseFee{false};
    bcos::u256 m_gasPrice{0};
    bcos::u256 m_gasTipCap{0};
    bcos::u256 m_gasFeeCap{0};
    bool m_hasGasFeeCap{false};
    state::BlockInfo m_blockInfo{};
    bcos::u256 m_l1CostCharged{0};
    bcos::u256 m_operatorCostLimit{0};
    bcos::u256 m_effectiveGasPrice{0};
    bcos::u256 m_baseFee{0};
    uint64_t m_floorDataGas{0};
    const Eip2930AccessList* m_accessList{nullptr};
    uint8_t m_web3TypedTxKind{0};
    uint64_t m_authTupleCount{0};
    bcos::u256 m_blobGasFeeCap{0};
    std::vector<bcos::h256> m_blobVersionedHashes;
    std::optional<RollupCostData> m_rollupCostData;
};

struct OpStackTxFeeLedger
{
    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    evmc_address m_baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    evmc_address m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    evmc_address m_operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;

    task::Task<bool> buyGas(TxPipelineContext& ctx, OpStackFeeContext& feeCtx);
    task::Task<void> refundGas(TxPipelineContext& ctx, OpStackFeeContext const& feeCtx,
        OpStackSettlementResult const& settled);
    task::Task<void> refundIsthmusOperatorCost(
        TxPipelineContext& ctx, OpStackFeeContext const& feeCtx, uint64_t gasUsed);
};

}  // namespace bcos::evm

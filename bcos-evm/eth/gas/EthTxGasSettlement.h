/*
 * Ethereum transaction gas settlement (EIP-7623 + intrinsic gas), transaction-executor only.
 *
 * Task-0-FROZEN: Branch A. executionBurn = gasBeforeEvm - gas_left.
 * effectiveRefund = min(gas_refund, gasUsedBeforeRefund / 5).
 */
#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include <evmc/evmc.h>
#include <algorithm>

namespace bcos::evm
{
namespace gas
{

constexpr int64_t TX_BASE_GAS = 21000;
constexpr int64_t CREATE_BASE_GAS = 32000;
constexpr int64_t INITCODE_WORD_GAS = 2;
constexpr int64_t ACCESS_LIST_ADDRESS_COST = 2400;
constexpr int64_t ACCESS_LIST_STORAGE_KEY_COST = 1900;

struct TxIntrinsicGas
{
    int64_t txBase = TX_BASE_GAS;
    int64_t normalCalldata = 0;
    int64_t accessListCost = 0;
    int64_t createIntrinsic = 0;
    int64_t floorReserve = 0;

    int64_t fixedCost() const { return txBase + accessListCost; }

    int64_t preExecutionDebit() const { return fixedCost() + normalCalldata + createIntrinsic; }

    /// EIP-7623: max(21000 + floor, intrinsic) where intrinsic includes access list + CREATE.
    int64_t gasLimitMinimum() const
    {
        int64_t const intrinsic = preExecutionDebit();
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }

    /// EIP-7623 + EIP-7702: auth intrinsic is part of the intrinsic side of the minimum, not
    /// additive.
    int64_t gasLimitMinimumWithAuth(int64_t authCost) const
    {
        int64_t const intrinsic = preExecutionDebit() + authCost;
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }
};

struct TxGasSettlementContext
{
    int64_t gasLimit = 0;
    int64_t gasBeforeEvm = 0;
    gas::Eip7623Components calldata{};
    int64_t fixedIntrinsic = 0;
    int64_t authIntrinsic = 0;
    int64_t createTerm = 0;
    int64_t evmGasLeft = 0;
    int64_t evmGasRefund = 0;
};

inline int64_t calcAccessListCost(Eip2930AccessList const* accessList) noexcept
{
    if (accessList == nullptr)
    {
        return 0;
    }
    int64_t cost = 0;
    for (auto const& entry : *accessList)
    {
        cost += ACCESS_LIST_ADDRESS_COST;
        cost += static_cast<int64_t>(entry.second.size()) * ACCESS_LIST_STORAGE_KEY_COST;
    }
    return cost;
}

inline int64_t calcAuthTupleIntrinsicGas(uint64_t authTupleCount) noexcept
{
    return static_cast<int64_t>(authTupleCount) * static_cast<int64_t>(PER_EMPTY_ACCOUNT_COST);
}

inline int64_t calcCreateIntrinsic(evmc_message const& message) noexcept
{
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return 0;
    }
    auto const inputSize = static_cast<int64_t>(message.input_size);
    auto const words = (inputSize + 31) / 32;
    return CREATE_BASE_GAS + INITCODE_WORD_GAS * words;
}

inline TxIntrinsicGas computeTxIntrinsicGas(
    evmc_message const& message, Eip2930AccessList const* accessList, uint8_t web3TypedTxKind)
{
    TxIntrinsicGas intrinsic;
    auto const components =
        gas::calcEip7623Components(bcos::bytesConstRef(message.input_data, message.input_size));
    intrinsic.normalCalldata = components.normalCost;
    intrinsic.floorReserve = components.floorCost;

    if (accessList != nullptr && !accessList->empty())
    {
        intrinsic.accessListCost = calcAccessListCost(accessList);
    }

    intrinsic.createIntrinsic = calcCreateIntrinsic(message);
    return intrinsic;
}

inline int64_t effectiveRefundEip3529(int64_t evmGasRefund, int64_t gasUsedBeforeRefund) noexcept
{
    if (gasUsedBeforeRefund <= 0)
    {
        return 0;
    }
    return std::min(evmGasRefund, gasUsedBeforeRefund / 5);
}

/// CREATE settlement: ExecuteViaEth debits 21000 + normal calldata before EVM, not createTerm.
/// geth IntrinsicGas uses TxGasContractCreation (53000) which includes the 32000 create surcharge.
/// When gasBeforeEvm is the full post-partial-intrinsic pool, add createTerm; unit-test pools sized
/// to createTerm-only keep legacy createExtra top-up when executionBurn < createTerm.
inline int64_t calcCreateSettlementExtra(
    TxGasSettlementContext const& ctx, int64_t executionBurn) noexcept
{
    if (ctx.createTerm <= 0)
    {
        return 0;
    }
    if (ctx.gasBeforeEvm <= ctx.createTerm)
    {
        return executionBurn < ctx.createTerm ? ctx.createTerm - executionBurn : 0;
    }
    return ctx.createTerm;
}

inline int64_t finalizeEthereumGasUsed(
    TxGasSettlementContext const& ctx, uint8_t calldataFloorPerToken) noexcept
{
    int64_t const executionBurn = ctx.gasBeforeEvm - ctx.evmGasLeft;
    int64_t const createExtra = calcCreateSettlementExtra(ctx, executionBurn);
    // Snapshot is taken after normal calldata pre-debit and 21000 base; executionBurn is EVM-only.
    int64_t const gasUsedBeforeRefund = ctx.fixedIntrinsic + ctx.calldata.normalCost +
                                        ctx.authIntrinsic + executionBurn + createExtra;
    int64_t const effectiveRefund = effectiveRefundEip3529(ctx.evmGasRefund, gasUsedBeforeRefund);
    // geth (Prague+): after refund, top up only when total used is below FloorDataGas
    // (21000 + tokens * calldata_floor_per_token; access list excluded from floor).
    int64_t const gasUsedAfterRefund = gasUsedBeforeRefund - effectiveRefund;
    int64_t const floorDataGas = TX_BASE_GAS + ctx.calldata.tokenCount * calldataFloorPerToken;
    return std::max(gasUsedAfterRefund, floorDataGas);
}

/// geth state_transition peakGasUsed/refund settlement for included top-level txs whose EVM
/// execution returned a vmerr (INVALID/REVERT/OOG/etc.) after preCheck succeeded.
inline int64_t settleIncludedTopLevelTransactionGas(int64_t gasLimit, int64_t evmGasLeft,
    int64_t stateRefund, uint8_t calldataFloorPerToken, Eip7623Components const& calldata) noexcept
{
    int64_t const gasLeft =
        std::min(std::max<int64_t>(0, evmGasLeft), std::max<int64_t>(0, gasLimit));
    int64_t const peakGasUsed = std::max<int64_t>(0, gasLimit) - gasLeft;
    int64_t const effectiveRefund = effectiveRefundEip3529(stateRefund, peakGasUsed);
    int64_t const gasRemaining =
        std::min(std::max<int64_t>(0, gasLimit), gasLeft + effectiveRefund);
    int64_t gasUsed = std::max<int64_t>(0, gasLimit) - gasRemaining;
    int64_t const floorDataGas = TX_BASE_GAS + calldata.tokenCount * calldataFloorPerToken;
    if (floorDataGas > 0 && gasUsed < floorDataGas)
    {
        gasUsed = std::min(floorDataGas, std::max<int64_t>(0, gasLimit));
    }
    return gasUsed;
}

}  // namespace gas
}  // namespace bcos::evm

namespace bcos::executor_v1::gas
{
using bcos::evm::gas::computeTxIntrinsicGas;
using bcos::evm::gas::TxIntrinsicGas;
}  // namespace bcos::executor_v1::gas

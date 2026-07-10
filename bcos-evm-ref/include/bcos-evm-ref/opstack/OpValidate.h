#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <system_error>
#include <test/state/state.hpp>
#include <variant>

namespace bcos::evmref::opstack
{
struct OpTxProperties
{
    evmone::state::TransactionProperties props;
    intx::uint256 l1_cost;
    intx::uint256 operator_cost_at_gas_limit;
};

/// Reuses evmone validate_transaction then applies OP checks: reject blob tx; balance cap
/// = gasLimit*maxGasPrice + value + l1Cost + operatorCost(gasLimit) (gasFeeCap pricing).
std::variant<OpTxProperties, std::error_code> opValidate(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::Transaction& tx,
    evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg, const OpFeeParams& fee,
    int64_t blockGasLeft);

/// 配对约束：*FromState 必须成对使用；不得与注入式 (opValidate/opTransition) 混用。
/// 从 view 读取 OP_L1_BLOCK 费用参数后委托给 opValidate。
std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft);
}  // namespace bcos::evmref::opstack

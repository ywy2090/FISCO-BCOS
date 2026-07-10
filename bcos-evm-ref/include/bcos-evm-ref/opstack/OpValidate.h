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
    // opValidate 求 l1_cost/operator_cost_at_gas_limit 时用的 OpFeeParams 快照；opTransition
    // 直接复用它而非另收一个 fee 参数，避免 validate/transition 两次调用被灌入不同 OpFeeParams
    // 导致 operator_cost_at_gas_limit - opAtUsed 下溢（二者必须出自同一次费率读取）。
    OpFeeParams fee;
};

/// Reuses evmone validate_transaction then applies OP checks: reject blob tx; balance cap
/// = gasLimit*maxGasPrice + value + l1Cost + operatorCost(gasLimit) (gasFeeCap pricing).
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidate(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, const OpFeeParams& fee, int64_t blockGasLeft);

/// 配对约束：*FromState 必须成对使用；不得与注入式 (opValidate/opTransition) 混用。
/// 从 view 读取 OP_L1_BLOCK 费用参数后委托给 opValidate。
[[nodiscard]] std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft);
}  // namespace bcos::evmref::opstack

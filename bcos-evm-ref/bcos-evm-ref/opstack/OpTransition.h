#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmc/evmc.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// Fork evmone::state::transition (evmone state.cpp:561-649): buyGas adds l1Cost +
/// operatorCost(gasLimit); Host replaced with OpHost; tail routes base/l1/operator fees to vaults.
/// Does not write back; caller applies applyStateDiff(receipt.state_diff).
/// 用 props.fee（opValidate 求 props 时的 OpFeeParams 快照）而非单独收一个 fee 参数——
/// 防止 validate/transition 两次调用被灌入不同 OpFeeParams 导致退款计算下溢（见
/// OpTxProperties::fee）。
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);

/// 配对约束：*FromState 必须成对使用；不得与注入式 (opValidate/opTransition) 混用。
/// props.fee 已由 opValidateFromState 读取并缓存，这里直接透传给 opTransition，不再重读 view。
OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);
}  // namespace bcos::evmref::opstack

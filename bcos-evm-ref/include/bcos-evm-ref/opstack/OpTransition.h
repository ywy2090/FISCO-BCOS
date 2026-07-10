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
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, const OpFeeParams& fee, uint64_t chainId,
    evmc::bytes_view signedTxEnvelope);

/// 配对约束：*FromState 必须成对使用；不得与注入式 (opValidate/opTransition) 混用。
/// 从 view 读取 OP_L1_BLOCK 费用参数后委托给 opTransition。
OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope);
}  // namespace bcos::evmref::opstack

#pragma once

#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <cstdint>
#include <optional>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// 0x7E deposit tx（非 evmone Transaction）。mint 有值时无条件加到 from 余额（nullopt=不加）；
/// value 在 call 中正常转账——两个独立字段。is_system_tx 在 Regolith 后必须为 false。
struct DepositTx
{
    evmc::bytes32 source_hash;
    evmc::address from;
    std::optional<evmc::address> to;   // nullopt = 合约创建（地址由 from + 执行前 nonce 派生）
    std::optional<intx::uint256> mint; // nullopt = 无 mint（对齐 op-geth *big.Int 可为 nil）
    intx::uint256 value;
    int64_t gas_limit;
    bool is_system_tx;
    evmc::bytes data;
};

struct OpDepositReceipt
{
    evmone::state::TransactionReceipt receipt;
    uint64_t deposit_nonce;            // 执行前 depositor nonce
    uint64_t deposit_receipt_version;  // = 1（Canyon+）
};

/// 执行一笔 0x7E deposit：跳过 buyGas；mint 有值加余额；intrinsic + 7623 floor 照扣；
/// 失败双路径保留 mint 且强制 nonce++；is_system_tx==true 抛 std::runtime_error（块级错误）。
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId);
}  // namespace bcos::evmref::opstack

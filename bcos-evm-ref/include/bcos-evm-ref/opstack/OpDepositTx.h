#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <cstdint>
#include <optional>

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
}  // namespace bcos::evmref::opstack

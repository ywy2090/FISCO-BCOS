#pragma once

#include <evmc/evmc.hpp>

namespace evmone::test
{
struct TestState;
}

namespace bcos::evmref::opstack
{
using evmc::literals::operator""_address;

// OP Stack predeploy / vault 地址（镜像 op-geth；逐字节对照生产 OpStackConstants.h）。
inline constexpr evmc::address OP_L1_BLOCK = 0x4200000000000000000000000000000000000015_address;
inline constexpr evmc::address OP_GAS_PRICE_ORACLE =
    0x420000000000000000000000000000000000000f_address;
inline constexpr evmc::address OP_SEQUENCER_FEE_VAULT =
    0x4200000000000000000000000000000000000011_address;
inline constexpr evmc::address OP_BASE_FEE_VAULT =
    0x4200000000000000000000000000000000000019_address;
inline constexpr evmc::address OP_L1_FEE_VAULT = 0x420000000000000000000000000000000000001a_address;
inline constexpr evmc::address OP_OPERATOR_FEE_VAULT =
    0x420000000000000000000000000000000000001b_address;
// 合成 deposit sender（0xdead…0001；非 predeploy，不预填账户）。
inline constexpr evmc::address OP_DEPOSITOR = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;

/// 把 6 个 predeploy/vault 账户预填为余额 0 的空账户（M5 块级 harness 创世准备）。
/// 范围收窄：本 M4 只建账户存在性，真实 bytecode / 存储布点延后 M5。
void seedOpPredeploys(evmone::test::TestState& state);
}  // namespace bcos::evmref::opstack

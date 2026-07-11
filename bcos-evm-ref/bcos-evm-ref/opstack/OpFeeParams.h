#pragma once

#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace evmone::state
{
class StateView;
}

namespace bcos::evmref::opstack
{
/// 本块 L1 attributes deposit 执行后，从 L1Block 存储槽读出（共识关键）。
/// 布局镜像 op-geth L1Block.sol（slot 3/8 打包，非标准 ABI）。
struct OpFeeParams
{
    intx::uint256 l1_base_fee;             // slot 1（整槽）
    uint32_t base_fee_scalar;              // slot 3 bytes[16,20)
    uint32_t blob_base_fee_scalar;         // slot 3 bytes[20,24)
    intx::uint256 blob_base_fee;           // slot 7（整槽）
    uint32_t operator_fee_scalar;          // slot 8 bytes[20,24)
    uint64_t operator_fee_constant;        // slot 8 bytes[24,32)
    uint16_t da_footprint_gas_scalar = 0;  // slot 8 bytes[18,20)
};

/// 从四个存储槽解包（Isthmus 调用方可忽略 da_footprint_gas_scalar）。
OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot7, const evmc::bytes32& slot8) noexcept;

/// 从 OP_L1_BLOCK 的 slot 1/3/7/8 读取并 unpack（缺槽视为零字）。
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
}  // namespace bcos::evmref::opstack

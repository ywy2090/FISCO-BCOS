#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace bcos::evmref::opstack
{
/// FastLZ 压缩后长度（Fjord DA 回归输入）。移植 op-geth FlzCompressLen；逐字节对齐生产。
uint32_t flzCompressLen(evmc::bytes_view data) noexcept;

/// estimatedSize（×1e6）= max(100e6, -42585600 + 836500*fastlzSize)。
intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6；空 envelope 返 0。
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;

/// Fjord L1 data fee（Isthmus 沿用）。签名后完整 tx envelope；空 envelope 返 0；deposit
/// 恒零由调用方保证。
intx::uint256 computeL1Cost(const OpFeeParams& params, evmc::bytes_view signedTxEnvelope) noexcept;

/// Operator fee：Isthmus gas*scalar/1e6+constant；Jovian gas*scalar*100+constant。
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evmref::opstack

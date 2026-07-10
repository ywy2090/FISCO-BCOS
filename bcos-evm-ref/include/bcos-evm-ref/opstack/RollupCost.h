#pragma once

#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>

namespace bcos::evmref::opstack
{
struct OpFeeParams;
struct OpForkConfig;

/// FastLZ 压缩后长度（Fjord DA 回归输入）。移植 op-geth FlzCompressLen；逐字节对齐生产。
uint32_t flzCompressLen(evmc::bytes_view data) noexcept;

/// estimatedSize（×1e6）= max(100e6, -42585600 + 836500*fastlzSize)。
intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6；flzLen==0 返 0（已算好 flz
/// 时复用，避免重复压缩）。
uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6；空 envelope 返 0。
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;

/// Ecotone L1 calldata gas：zeroes*4 + nonZeroes*16（无 pre-Regolith +68）。
uint64_t bedrockCalldataGasUsed(evmc::bytes_view signedTxEnvelope) noexcept;

/// L1 data fee，Fjord+ FastLZ 分支：flzLen==0 返 0（已算好 flz 时复用，避免重复压缩）。
intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept;

/// L1 data fee。Ecotone(has_ecotone_l1_formula) 走 calldataGas 公式；Fjord+ 走 FastLZ。
/// 空 envelope 返 0；deposit 恒零由调用方保证。
intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept;

/// Operator fee：Isthmus gas*scalar/1e6+constant；Jovian gas*scalar*100+constant。
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evmref::opstack

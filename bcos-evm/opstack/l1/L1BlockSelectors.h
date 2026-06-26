#pragma once

#include <cstdint>

namespace bcos::evm::l1block
{
inline constexpr uint32_t kSetL1BlockValuesIsthmus = 0x098999be;
inline constexpr uint32_t kSetL1BlockValuesJovian = 0x3db6be2b;
inline constexpr uint32_t kNotDepositor = 0x3cc50b45;

inline constexpr uint32_t kNumber = 0x8381f58a;
inline constexpr uint32_t kTimestamp = 0xb80777ea;
inline constexpr uint32_t kBasefee = 0x5cf24969;
inline constexpr uint32_t kHash = 0x09bd5a60;
inline constexpr uint32_t kSequenceNumber = 0x64ca23ef;
inline constexpr uint32_t kBlobBaseFeeScalar = 0x68d5dca6;
inline constexpr uint32_t kBaseFeeScalar = 0xc5985918;
inline constexpr uint32_t kBatcherHash = 0xe81b2c6d;
inline constexpr uint32_t kL1FeeOverhead = 0x8b239f73;
inline constexpr uint32_t kL1FeeScalar = 0x9e8c4966;
inline constexpr uint32_t kBlobBaseFee = 0xf8206140;
inline constexpr uint32_t kOperatorFeeScalar = 0x4d5d9a2a;
inline constexpr uint32_t kOperatorFeeConstant = 0x16d3bc7f;
inline constexpr uint32_t kDaFootprintGasScalar = 0xfe3d5710;

inline constexpr uint32_t kL1BaseFee = 0x519b4bd3;
inline constexpr uint32_t kL1BlobBaseFee = 0x84189161;

inline constexpr uint32_t kDepositorAccount = 0xe591b282;
inline constexpr uint32_t kIsCustomGasToken = 0x21326849;
inline constexpr uint32_t kGasPayingToken = 0x4397dfef;
inline constexpr uint32_t kGasPayingTokenName = 0xd8444715;
inline constexpr uint32_t kGasPayingTokenSymbol = 0x550fcdc9;
inline constexpr uint32_t kVersion = 0x54fd4d50;

inline constexpr uint32_t kIsFeatureEnabled = 0x47af267b;
}  // namespace bcos::evm::l1block

#pragma once

#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::evm
{

// Predeploy fee recipients
inline constexpr evmc_address OP_BASE_FEE_RECIPIENT = {
    .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x19}};

inline constexpr evmc_address OP_L1_FEE_RECIPIENT = {
    .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x1a}};

inline constexpr evmc_address OP_OPERATOR_FEE_RECIPIENT = {
    .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x1b}};

inline constexpr evmc_address OP_GAS_PRICE_ORACLE_PREDEPLOY = {
    .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x0f}};

inline constexpr evmc_address OP_L1_BLOCK_PREDEPLOY = {
    .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x15}};

inline constexpr evmc_address OP_DEPOSITOR_ACCOUNT = {
    .bytes = {0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad,
        0xde, 0xad, 0xde, 0xad, 0x00, 0x01}};

// L1Block storage slots
inline constexpr u256 L1_NUMBER_TIMESTAMP_SLOT{0};
inline constexpr u256 L1_BASE_FEE_SLOT{1};
inline constexpr u256 L1_HASH_SLOT{2};
inline constexpr u256 L1_FEE_SCALARS_SLOT{3};
inline constexpr u256 L1_BATCHER_HASH_SLOT{4};
inline constexpr u256 L1_FEE_OVERHEAD_SLOT{5};
inline constexpr u256 L1_FEE_SCALAR_LEGACY_SLOT{6};
inline constexpr u256 L1_BLOB_BASE_FEE_SLOT{7};
inline constexpr u256 OPERATOR_FEE_PARAMS_SLOT{8};
inline constexpr u256 L1_FEATURE_ENABLED_MAPPING_SLOT{9};

// Fjord L1 cost constants
inline constexpr int64_t L1_COST_INTERCEPT = -42'585'600;
inline constexpr int64_t L1_COST_FASTLZ_COEF = 836'500;
inline constexpr int64_t MIN_TX_SIZE_SCALED = 100'000'000;
inline constexpr int64_t FJORD_DIVISOR = 1'000'000'000'000;

// Fjord L1 fee / getL1GasUsed: Ecotone calldata byte cost numerator (same as intrinsic nonzero
// byte).
inline constexpr int64_t FJORD_L1_FEE_CALLDATA_BYTE_NUMERATOR = gas::NONZERO_BYTE_INTRINSIC_COST;
// estimatedDASize = estimatedDASizeScaled / ESTIMATED_DA_SIZE_DIVISOR
inline constexpr int64_t ESTIMATED_DA_SIZE_DIVISOR = 1'000'000;
// getL1GasUsed: unsigned tx envelope overhead added to FastLZ compressed size (op-geth).
inline constexpr uint64_t L1_GAS_USED_UNSIGNED_TX_OVERHEAD = 68;
// Isthmus operator fee: gas * scalar / OPERATOR_FEE_SCALAR_DIVISOR + constant
inline constexpr int64_t OPERATOR_FEE_SCALAR_DIVISOR = 1'000'000;
// Jovian operator fee: gas * scalar * JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER + constant
inline constexpr int64_t JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER = 100;

// GasPriceOracle.decimals()
inline constexpr uint32_t GAS_PRICE_ORACLE_DECIMALS = 6;

inline constexpr size_t ISTHMUS_L1_ATTRIBUTES_LEN = 176;
inline constexpr size_t JOVIAN_L1_ATTRIBUTES_LEN = 178;

// EIP-4844 blob gas per blob (Cancun / Isthmus)
inline constexpr uint64_t OP_BLOB_GAS_PER_BLOB = gas::BLOB_GAS_PER_BLOB;

// Canyon+ deposit receipt metadata (op-geth types.CanyonDepositReceiptVersion)
inline constexpr uint64_t OP_CANYON_DEPOSIT_RECEIPT_VERSION = 1;

}  // namespace bcos::evm

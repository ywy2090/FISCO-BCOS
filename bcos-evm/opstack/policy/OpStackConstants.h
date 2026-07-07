/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OP Stack protocol constants (predeploy addresses, L1Block slots, fee literals).
 * @file OpStackConstants.h
 *
 * Single source of truth for rollup policy values shared across fee math, L1 state I/O,
 * predeploy routing, and settlement. Predeploy addresses align with specs.optimism.io
 * protocol/predeploys and interop/predeploys; fee literals align with op-geth
 * rollup_cost.go and protocol_params.go. Fork-gated behavior lives in OpStackForkSchedule.
 */

#pragma once

#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm
{

namespace opstack_address_detail
{
/// Build 0x42000000000000000000000000000000000000XX predeploy address (OP Stack namespace).
inline constexpr evmc_address makePredeploy(uint8_t suffix) noexcept
{
    return evmc_address{.bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, suffix}};
}
}  // namespace opstack_address_detail

// --- Protocol predeploys (specs.optimism.io/protocol/predeploys.html) ---

/// Legacy withdrawal commitments pre-Bedrock (deprecated).
inline constexpr evmc_address OP_LEGACY_MESSAGE_PASSER_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x00);
/// Legacy deployer allowlist (deprecated).
inline constexpr evmc_address OP_DEPLOYER_WHITELIST_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x02);
/// Wrapped Ether (WETH9).
inline constexpr evmc_address OP_WETH9_PREDEPLOY = opstack_address_detail::makePredeploy(0x06);
/// L2 side of the CrossDomainMessenger bridge.
inline constexpr evmc_address OP_L2_CROSS_DOMAIN_MESSENGER_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x07);
/// GasPriceOracle; offchain L1 fee estimation API (getL1Fee, fork flags).
inline constexpr evmc_address OP_GAS_PRICE_ORACLE_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x0f);
/// L2 StandardBridge (ETH/ERC20 L1↔L2).
inline constexpr evmc_address OP_L2_STANDARD_BRIDGE_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x10);
/// Sequencer priority-fee vault (block.coinbase on OP chains).
inline constexpr evmc_address OP_SEQUENCER_FEE_VAULT_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x11);
/// Factory for OptimismMintableERC20 tokens.
inline constexpr evmc_address OP_OPTIMISM_MINTABLE_ERC20_FACTORY_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x12);
/// Legacy L1 block number getter (deprecated; use L1Block).
inline constexpr evmc_address OP_L1_BLOCK_NUMBER_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x13);
/// L2 ERC721 bridge.
inline constexpr evmc_address OP_L2_ERC721_BRIDGE_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x14);
/// L1Block; L1 context and fee scalars on L2 (native dispatch in bcos-evm).
inline constexpr evmc_address OP_L1_BLOCK_PREDEPLOY = opstack_address_detail::makePredeploy(0x15);
/// L2→L1 withdrawal message passer (Bedrock+).
inline constexpr evmc_address OP_L2_TO_L1_MESSAGE_PASSER_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x16);
/// Factory for OptimismMintableERC721 tokens.
inline constexpr evmc_address OP_OPTIMISM_MINTABLE_ERC721_FACTORY_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x17);
/// Owner proxy for upgradeable predeploy implementations.
inline constexpr evmc_address OP_PROXY_ADMIN_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x18);
/// Base-fee vault; receives EIP-1559 base fee on L2 (not burned).
inline constexpr evmc_address OP_BASE_FEE_VAULT_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x19);
/// L1 data-fee vault; receives L1 portion of tx fees.
inline constexpr evmc_address OP_L1_FEE_VAULT_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x1a);
/// Operator-fee vault (Isthmus+).
inline constexpr evmc_address OP_OPERATOR_FEE_VAULT_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x1b);
/// Ethereum Attestation Service schema registry.
inline constexpr evmc_address OP_SCHEMA_REGISTRY_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x20);
/// Ethereum Attestation Service (EAS).
inline constexpr evmc_address OP_EAS_PREDEPLOY = opstack_address_detail::makePredeploy(0x21);
/// OP governance token (non-proxied).
inline constexpr evmc_address OP_GOVERNANCE_TOKEN_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x42);

// --- Interop predeploys (specs.optimism.io/interop/predeploys.html) ---

/// Cross-chain message inbox (superchain interop).
inline constexpr evmc_address OP_CROSS_L2_INBOX_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x22);
/// L2→L2 CrossDomainMessenger.
inline constexpr evmc_address OP_L2_TO_L2_CROSS_DOMAIN_MESSENGER_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x23);
/// Superchain ETH bridge.
inline constexpr evmc_address OP_SUPERCHAIN_ETH_BRIDGE_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x24);
/// ETH liquidity pool for interop.
inline constexpr evmc_address OP_ETH_LIQUIDITY_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x25);
/// Superchain ERC20 factory.
inline constexpr evmc_address OP_OPTIMISM_SUPERCHAIN_ERC20_FACTORY_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x26);
/// Beacon for OptimismSuperchainERC20 implementations.
inline constexpr evmc_address OP_OPTIMISM_SUPERCHAIN_ERC20_BEACON_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x27);
/// Superchain token bridge.
inline constexpr evmc_address OP_SUPERCHAIN_TOKEN_BRIDGE_PREDEPLOY =
    opstack_address_detail::makePredeploy(0x28);

// --- Special addresses (outside 0x4200… namespace or non-vault) ---

/// Pre-Bedrock ERC20-wrapped ETH representation (deprecated).
inline constexpr evmc_address OP_LEGACY_ERC20_ETH_PREDEPLOY = {
    .bytes = {0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad,
        0xde, 0xad, 0xde, 0xad, 0x00, 0x00}};  // 0xDead…0000

/// EIP-4788 beacon block root (Ecotone+; not in 0x4200 namespace).
inline constexpr evmc_address OP_BEACON_BLOCK_ROOT_PREDEPLOY = {
    .bytes = {0x00, 0x0f, 0x3d, 0xf6, 0xd7, 0x32, 0x80, 0x7e, 0xf1, 0x31, 0x9f, 0xb7, 0xb8, 0xbb,
        0x85, 0x22, 0xd0, 0xbe, 0xac, 0x02}};  // 0x000F3df6…Beac02

/// Synthetic sender for deposit (L1→L2) transactions; exempt from L1/operator fees.
inline constexpr evmc_address OP_DEPOSITOR_ACCOUNT = {
    .bytes = {0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad,
        0xde, 0xad, 0xde, 0xad, 0x00, 0x01}};  // 0xdead…0001

// --- Fee settlement recipients (alias vault predeploys; op-geth rollup_cost.go) ---

inline constexpr evmc_address OP_BASE_FEE_RECIPIENT = OP_BASE_FEE_VAULT_PREDEPLOY;
inline constexpr evmc_address OP_L1_FEE_RECIPIENT = OP_L1_FEE_VAULT_PREDEPLOY;
inline constexpr evmc_address OP_OPERATOR_FEE_RECIPIENT = OP_OPERATOR_FEE_VAULT_PREDEPLOY;

// --- L1Block predeploy storage slots (mirrors op-geth L1Block.sol) ---
// Packing details: L1BlockStorage.h. Scalars in slot 3/8 share a word (non-standard ABI layout).

inline constexpr u256 L1_NUMBER_TIMESTAMP_SLOT{0};   // L1 block number (high) + timestamp (low)
inline constexpr u256 L1_BASE_FEE_SLOT{1};           // L1 base fee per gas
inline constexpr u256 L1_HASH_SLOT{2};               // L1 block hash
inline constexpr u256 L1_FEE_SCALARS_SLOT{3};        // Ecotone+ baseFeeScalar + blobBaseFeeScalar +
                                                     // sequence
inline constexpr u256 L1_BATCHER_HASH_SLOT{4};       // Batcher address hash (version byte + keccak)
inline constexpr u256 L1_FEE_OVERHEAD_SLOT{5};       // Legacy Regolith/Bedrock L1 fee overhead
inline constexpr u256 L1_FEE_SCALAR_LEGACY_SLOT{6};  // Legacy Regolith/Bedrock L1 fee scalar
inline constexpr u256 L1_BLOB_BASE_FEE_SLOT{7};      // L1 blob base fee (Ecotone+)
inline constexpr u256 OPERATOR_FEE_PARAMS_SLOT{8};  // Isthmus operator scalar+constant; Jovian adds
                                                    // daFootprint
inline constexpr u256 L1_FEATURE_ENABLED_MAPPING_SLOT{9};  // Base slot for per-fork feature flags
                                                           // mapping

// --- Fjord L1 data fee (linear regression over FastLZ-compressed tx size) ---
// l1Cost = max(intercept + coef * max(compressedLen, minTxSizeScaled), 0) / fjordDivisor

inline constexpr int64_t L1_COST_INTERCEPT = -42'585'600;
inline constexpr int64_t L1_COST_FASTLZ_COEF = 836'500;
inline constexpr int64_t MIN_TX_SIZE_SCALED = 100'000'000;   // 100 * 1e6
inline constexpr int64_t FJORD_DIVISOR = 1'000'000'000'000;  // 1e12

// --- Ecotone/Isthmus/Jovian L1 gas and operator fee helpers ---

/// Ecotone calldata byte cost numerator in getL1Fee (same as intrinsic nonzero byte).
inline constexpr int64_t FJORD_L1_FEE_CALLDATA_BYTE_NUMERATOR = gas::NONZERO_BYTE_INTRINSIC_COST;
/// estimatedDASize = estimatedDASizeScaled / ESTIMATED_DA_SIZE_DIVISOR
inline constexpr int64_t ESTIMATED_DA_SIZE_DIVISOR = 1'000'000;
/// Unsigned tx envelope overhead added to FastLZ size in getL1GasUsed (op-geth).
inline constexpr uint64_t L1_GAS_USED_UNSIGNED_TX_OVERHEAD = 68;
/// Isthmus operator fee: gas * scalar / OPERATOR_FEE_SCALAR_DIVISOR + constant
inline constexpr int64_t OPERATOR_FEE_SCALAR_DIVISOR = 1'000'000;
/// Jovian operator fee: gas * scalar * JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER + constant
inline constexpr int64_t JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER = 100;

// --- GasPriceOracle & L1 attributes deposit calldata ---

/// GasPriceOracle.decimals() return value.
inline constexpr uint32_t GAS_PRICE_ORACLE_DECIMALS = 6;
/// setL1BlockValues calldata body length (Isthmus; 4-byte selector excluded).
inline constexpr size_t ISTHMUS_L1_ATTRIBUTES_LEN = 176;
/// setL1BlockValues calldata body length (Jovian; adds daFootprint field).
inline constexpr size_t JOVIAN_L1_ATTRIBUTES_LEN = 178;

// --- Blob gas & deposit receipt metadata ---

/// EIP-4844 blob gas per blob (Cancun / Isthmus).
inline constexpr uint64_t OP_BLOB_GAS_PER_BLOB = gas::BLOB_GAS_PER_BLOB;
/// Canyon+ deposit receipt version field (op-geth types.CanyonDepositReceiptVersion).
inline constexpr uint64_t OP_CANYON_DEPOSIT_RECEIPT_VERSION = 1;

}  // namespace bcos::evm

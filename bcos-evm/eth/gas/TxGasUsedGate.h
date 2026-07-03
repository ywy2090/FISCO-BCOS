/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief TE / EEST top-level gasUsed fork gates (GAP-TE-002).
 * @file TxGasUsedGate.h
 *
 * Shared helper for TransactionExecutor and EEST adapters. NOT used on the Eth reference
 * path (applyEthMessage uses meterPostExecuteGas directly without isWeb3 gating).
 *
 * Branches:
 *   - included top-level vmerr + eip7623 + snapshot → peak settlement (ADR-015)
 *   - regular web3 + eip7623 + snapshot            → peak settlement
 *   - otherwise                                    → rawGasUsed (caller-supplied legacy value)
 */

#pragma once

#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include "bcos-evm/eth/gas/TopLevelGasSettlement.h"

namespace bcos::evm::gas
{

/// Selects peak EIP-7623 settlement vs legacy rawGasUsed for TE/EEST paths.
/// isWeb3 is a FISCO TE concept (Web3Transaction type); intentionally absent from eth layer.
inline int64_t finalizeEthTxGasUsed(int64_t gasLimit, int64_t legacyGasLeft, int64_t rawGasUsed,
    bool isWeb3, bool eip7623, bool topLevelIncludedTxVmError,
    TxGasSettlementSnapshot const& snapshot, uint8_t calldataFloorPerToken) noexcept
{
    // ADR-015: included vmerr still bills peak gas when 7623 snapshot is active.
    if (topLevelIncludedTxVmError && eip7623 && snapshot.eip7623SnapshotActive)
    {
        return settleTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    // TE-only: non-web3 txs skip 7623 settlement unless included-vmerr branch above fired.
    if (snapshot.eip7623SnapshotActive && isWeb3 && eip7623)
    {
        return settleTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    return rawGasUsed;
}

}  // namespace bcos::evm::gas

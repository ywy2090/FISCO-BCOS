/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Eth normal-tx gas lifecycle index (read-only map).
 * @file TxGasLifecycle.h
 *
 * Chronological stages for a top-level Eth transaction. Include this header for DTOs plus
 * a navigable map of where each stage is implemented. No formulas here (ADR-005).
 *
 * Gas units (message.gas) and wei (balance) are intentionally split:
 *   stages 2–4 operate in gas units; stages 1 and 5 project/consume wei via FeeSettlementPlan.
 */

#pragma once

// --- Types (DTOs) ---
#include "bcos-evm/eth/gas/GasSettlementTypes.h"

// --- Stage 0: txpool / builder prechecks (optional, out of bcos-evm) ---
//   gasLimitMinimum(), computeTxIntrinsicGas() — TxIntrinsicGas.h

// --- Stage 1: buyGas (pre-execution wei debit) ---
//   planPreExecution()           — TxFeeSettlement.h
//   EthFeeSettlement::buyGas()   — settlement/EthFeeSettlement.*

// --- Stage 2: kernel state transition (gas units) ---
//   deductIntrinsicGas()         — kernel/state-transition/DeductIntrinsicGas.h
//   IntrinsicGasAccounting     — GasSettlementTypes.h (bookkeeping on StateTransitionContext)
//   innerExecute / EVM         — kernel/execution/InnerExecute.*

// --- Stage 3: post-EVM snapshot ---
//   captureSettlementSnapshot()  — StateTransitionExecute.cpp (FloorDataGas mode only)
//   TxGasSettlementSnapshot      — GasSettlementTypes.h

// --- Stage 4: post-execute gasUsed (gas units) ---
//   settleTopLevelTransactionGas() — TopLevelGasSettlement.h
//   meterPostExecuteGas()           — PostExecuteGasMetering.h
//   EthNormalTxFeeCoordinator::completeAfterPipeline() — settlement/EthNormalTxFeeCoordinator.*

// --- Stage 5: refundGas (post-execution wei credit) ---
//   planPostExecution()          — TxFeeSettlement.h
//   EthFeeSettlement::refundGas() — settlement/EthFeeSettlement.*

// --- TE / EEST adapter gate (not Eth reference path) ---
//   finalizeEthTxGasUsed()       — TxGasUsedGate.h

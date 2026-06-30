/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Index of canonical eth-kernel pipeline symbols (geth vocabulary).
 * @file GethNamingAliases.h
 *
 * Portable eth/ symbols and their geth analogues — see ADR-030 for the full
 * bcos-evm ↔ go-ethereum map, including chain extension entry points.
 *
 * Kernel index (post naming migration):
 *   ChainPrecheckPolicy — setupMessage, checkTransactionRules, checkGasAffordable,
 *                         checkBalanceAndValue, runEvmExecution
 *   runTxPipeline
 *   debitIntrinsicGas / IntrinsicGasDebitParams
 *   executeMessage / TxExecutionRunner
 *   EvmTxContextView / *ExecutionBundle
 *   ChainCallTargetDispatcher
 *   EvmHostHooks / EthVmHostPolicy
 */

#pragma once

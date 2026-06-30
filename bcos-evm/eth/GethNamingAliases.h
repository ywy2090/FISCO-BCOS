/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Index of canonical execution pipeline symbols.
 * @file GethNamingAliases.h
 *
 * Canonical names (post naming migration):
 *   ChainPrecheckPolicy — setupMessage, checkTransactionRules, checkGasAffordable,
 *                         checkBalanceAndValue, runEvmExecution
 *   runTxPipeline
 *   debitIntrinsicGas / IntrinsicGasDebitParams
 *   executeMessage / TxExecutionRunner
 *   EvmTxContextView / *ExecutionBundle
 *   ChainCallTargetDispatcher
 *   EvmHostHooks / EthVmHostPolicy / FiscoVmHostPolicy
 *   fiscoExecute / ethReferenceExecute / opStackExecute / runOpStackTxLifecycle
 */

#pragma once

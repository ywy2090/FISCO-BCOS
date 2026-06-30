/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief ADR-030 §6 documentation-only orchestration type aliases.
 * @file GethOrchestrationAliases.h
 *
 * geth vocabulary for OrchestrationProfile / StateTransitionHooks /
 * StateTransitionErrorPolicy / ExecutionBundle — not used in production code;
 * canonical C++ names remain on disk (see ADR-030 §6, Tier C/D).
 *
 * Eth reference-path types are aliased here (eth/ includes only). FISCO and
 * OpStack chain types are forward-declared with mapping comments to avoid
 * pulling bcos/ or opstack/ into eth kernel headers.
 */

#pragma once

#include "bcos-evm/eth/apply/EthExecutionBundle.h"
#include "bcos-evm/eth/apply/EthOrchestrationProfile.h"
#include "bcos-evm/eth/apply/EthPrecheckPolicy.h"
#include "bcos-evm/eth/apply/EthStateTransitionErrorPolicy.h"
#include "bcos-evm/eth/pipeline/StateTransitionErrorPolicy.h"

namespace bcos::evm
{

// --- eth kernel interfaces (Tier A — portable preCheck / error mapping) ---

/// geth: execute return error vs included vmerr — ADR-030 §6
using ExecutionResultMapper = StateTransitionErrorPolicy;

// --- eth reference chain (Tier C — orchestration bind / host bundle) ---

/// geth: hook bind table — ADR-030 §6 (eth reference path)
using OrchestrationBindProfile = EthOrchestrationProfile;

/// geth: inputs to build OrchestrationBindProfile — ADR-030 §6
using HookBindInputs = EthOrchestrationProfile::BindingsContext;

/// geth: chain preCheck binding — ADR-030 §6 (eth reference path)
using ReferencePreCheckPolicy = EthPrecheckPolicy;

/// geth: chain execution result mapping — ADR-030 §6 (eth reference path)
using ReferenceExecutionResultMapper = EthStateTransitionErrorPolicy;

/// geth: EvmHostHooks + adapter lifetime — ADR-030 §6 (eth reference path)
using EvmHostContext = EthExecutionBundle;

/// geth: chain-owned host bundle (synonym) — ADR-030 §6
using ChainHostBundle = EthExecutionBundle;

// --- FISCO / OpStack (forward declarations; using= omitted to avoid chain includes) ---

struct FiscoOrchestrationProfile;
struct FiscoPrecheckPolicy;
struct FiscoStateTransitionErrorPolicy;
struct FiscoExecutionBundle;

struct OpStackOrchestrationProfile;
struct OpStackPrecheckPolicy;
struct OpStackStateTransitionErrorPolicy;
struct OpStackExecutionBundle;
struct OpStackSettlementFacade;

// geth OrchestrationBindProfile ↔ FiscoOrchestrationProfile / OpStackOrchestrationProfile
// geth HookBindInputs        ↔ FiscoOrchestrationProfile::BindingsContext /
//                              OpStackOrchestrationProfile::BindingsContext
// geth StateTransitionHooks  ↔ FiscoPrecheckPolicy / OpStackPrecheckPolicy / EthPrecheckPolicy
// geth ExecutionResultMapper ↔ FiscoStateTransitionErrorPolicy / OpStackStateTransitionErrorPolicy
// geth EvmHostContext        ↔ FiscoExecutionBundle / OpStackExecutionBundle
// geth SettlementProjection  ↔ OpStackSettlementFacade

}  // namespace bcos::evm

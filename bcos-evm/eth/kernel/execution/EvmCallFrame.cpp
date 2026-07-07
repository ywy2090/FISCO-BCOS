/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EvmCallFrame.cpp
 *
 * Unified execution of one evmc_message frame (CALL / DELEGATECALL / CREATE / CREATE2).
 *
 * Call sites:
 *   - innerExecute (InnerExecute)             → FrameScope::TopLevel
 *   - EthHost::call (evmone host callback)      → FrameScope::Nested
 *
 * Pipeline (runFrameSteps):
 *   1. routeFrameMessage  — normalize recipient/code_address, EIP-7702 delegation routing
 *   2. runCallTargetFastPath    — precompile / empty-account / policy short-circuit (no evmone)
 *   3. prepareNestedMessage     — chain hooks before nested EVM (warm pin, caller address)
 *   4. CREATE pre-checkpoint    — bind address, balance/nonce checks, EIP-2929 warm (no journal),
 *                                 bump nested sender nonce
 *   5. checkpointFrame          — open state journal slice for this frame
 *   6. transferOrFail           — move msg.value (skipped for DELEGATECALL/CALLCODE)
 *   7. runVm                    — load bytecode, invoke evmone via EthHost
 *   8. finalizeFrame            — code deposit, install contract, commit/revert journal
 *
 * TopLevel vs Nested differences:
 *   - TopLevel: no state.commit() here (outer state-transition owns the tx journal);
 *               CREATE bind/warm run before checkpoint; extension finalizes create nonce.
 *   - Nested:   state.commit() on success; executionAddress updated for delegate routing;
 *               nested CREATE bumps sender nonce *before* checkpoint (survives revert).
 */

#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/eip/Eip2929Gate.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/kernel/execution/CreateDeployment.h"
#include "bcos-evm/eth/kernel/execution/FrameBytecode.h"
#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "eth/core/CallTargetTypes.h"
#include "eth/core/EvmHostHooks.h"
#include "eth/core/RevisionConfig.h"
#include "eth/state/StateKeyHash.hpp"
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace bcos::evm::execution
{
namespace
{

/// Build a minimal evmc::Result for early-exit paths (balance failure, policy reject, etc.).
evmc::Result makeFrameResult(evmc_status_code status, int64_t gasLeft)
{
    evmc_result result{};
    result.status_code = status;
    result.gas_left = gasLeft;
    result.gas_refund = 0;
    result.create_address = {};
    return evmc::Result(result);
}

/// Resolve the deployed contract address reported to callers/receipts.
/// Priority: message.recipient (already bound by bindCreateForInit) → code_address → VM output.
/// TopLevel CREATE may still have a zero recipient before init binding; nested CREATE always
/// has recipient set during bindCreateForInit.
evmc_address resolveCreateAddress(evmc_message const& message, evmc_result const& result) noexcept
{
    auto createAddr = message.recipient;
    if (state::isZeroAddress(createAddr))
    {
        createAddr = message.code_address;
    }
    if (state::isZeroAddress(createAddr))
    {
        createAddr = result.create_address;
    }
    return createAddr;
}

/// Per-frame mutable workspace. `routed.routed` is the message after target/7702 routing;
/// `callMessage()` always reads/writes the routed copy, not the original evmc_message.
struct FrameWork
{
    CallFrameContext& ctx;
    evmc_message const& originalMsg;
    RoutedFrame routed;
    bcos::bytes code;  // lazy-loaded in runVm; empty until first EVM execution
    state::EthHost& host;

    evmc_message& callMessage() noexcept { return routed.routed; }
    evmc_message const& callMessage() const noexcept { return routed.routed; }
};

/// Trace log for nested frames only; top-level tracing is owned by state-transition / apply.
void logFrameDoneIfNested(evmc_message const& originalMsg, evmc::Result const& result)
{
    if (originalMsg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call done") << LOG_KV("depth", originalMsg.depth)
                       << LOG_KV("status", trace::evmcStatus(result.status_code))
                       << LOG_KV("gasLeft", result.gas_left);
    }
}

/// Caller seen by chain hooks during nested execution.
/// DELEGATECALL/CALLCODE keep the parent execution context: use executionAddress when set,
/// otherwise fall back to msg.sender (first nested frame from top-level CALL).
evmc_address resolveCallerAddress(
    evmc_address const& executionAddress, evmc_message const& msg) noexcept
{
    if (!state::isZeroAddress(executionAddress))
    {
        return executionAddress;
    }
    return msg.sender;
}

/// Precompile / empty-account / policy fast path (geth: evm.Call before interpreter).
///
/// Classifies the call target; when not a normal contract, runs the envelope and returns.
/// Returns std::nullopt → fall through to normal EVM contract execution.
/// Returns FrameResult  → frame completes here; caller must not run value transfer / VM again.
///
/// Precompile envelopes handle their own gas accounting and optional in-envelope value transfer
/// (unless extension->skipHostValueTransfer(), used by FISCO paths that settle value elsewhere).
std::optional<FrameResult> runCallTargetFastPath(FrameWork& work, FrameScope scope)
{
    auto& callMessage = work.callMessage();
    if ((callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2))
    {
        return std::nullopt;
    }

    bool const skipVt =
        work.ctx.extension != nullptr && work.ctx.extension->skipHostValueTransfer();

    auto const desc = classifyCallTarget(work.ctx.state, work.ctx.revisionConfig, callMessage,
        scope, work.ctx.callTargetPort, work.ctx.extension);

    if (desc.admission != CallTargetAdmission::Ok)
    {
        // Chain policy blocked the classified target; no state change.
        evmc_result raw{};
        raw.status_code = EVMC_PRECOMPILE_FAILURE;
        raw.gas_left = callMessage.gas;
        return FrameResult{.result = evmc::Result(raw), .envelopeComplete = false};
    }

    precompiled::PrecompileEnvelopeInput envInput{.state = work.ctx.state,
        .revision = work.ctx.revisionConfig,
        .target = desc,
        .message = callMessage,
        .skipValueTransfer = skipVt,
        .callTargetPort = work.ctx.callTargetPort};

    switch (desc.route)
    {
    case CallTargetRoute::BuiltinPrecompile:
    case CallTargetRoute::ChainPrecompile:
    {
        auto out = precompiled::executePrecompileEnvelope(envInput);
        return FrameResult{
            .result = std::move(out.result), .gasRefund = out.gasRefund, .envelopeComplete = true};
    }
    case CallTargetRoute::EmptyAccount:
    {
        // Calls into empty code (non-existent contract): charge access gas, return empty success.
        auto out = precompiled::executeEmptyAccountEnvelope(envInput);
        return FrameResult{
            .result = std::move(out.result), .gasRefund = out.gasRefund, .envelopeComplete = true};
    }
    case CallTargetRoute::EvmContract:
        return std::nullopt;
    }
    return std::nullopt;
}

/// TopLevel CALL value recipient: code_address when set, otherwise recipient.
evmc_address resolveValueTransferRecipient(evmc_message const& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
}

/// DELEGATECALL / CALLCODE execute foreign code in the caller's own storage and
/// balance context and MUST NOT move balance between accounts. evmone still forwards
/// a (possibly non-zero) value in evmc_message.value — DELEGATECALL inherits the
/// parent frame's apparent value, CALLCODE carries the explicit stack value — but
/// go-ethereum's core/vm/evm.go DelegateCall/CallCode perform no Transfer. Host-side
/// value settlement uses this predicate to skip the transfer for those kinds.
inline bool isValueTransferSkippedKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_DELEGATECALL || kind == EVMC_CALLCODE;
}

/// Move msg.value for this frame (scope/kind rules below).
/// On failure: revert everything since checkpointFrame() and return INSUFFICIENT_BALANCE
/// with full frame gas left (matching geth's balance-check failure before interpreter run).
///
/// Skips transfer when:
///   - msg.value is zero
///   - DELEGATECALL / CALLCODE (evmone forwards inherited value but geth does not Transfer)
///   - extension->skipHostValueTransfer() (FISCO paths that settle value elsewhere)
///
/// TopLevel CREATE: sender → recipient (deployed address).
/// TopLevel CALL:   sender → resolveValueTransferRecipient(msg).
/// Nested:          sender → recipient.
std::optional<FrameResult> transferOrFail(FrameWork& work, FrameScope scope)
{
    auto const& msg = work.callMessage();
    auto& state = work.ctx.state;

    if (state::isZeroBytes32(msg.value))
    {
        return std::nullopt;
    }
    if (isValueTransferSkippedKind(msg.kind))
    {
        return std::nullopt;
    }
    if (work.ctx.extension != nullptr && work.ctx.extension->skipHostValueTransfer())
    {
        return std::nullopt;
    }

    auto const value = state::fromEvmC(msg.value);

    auto transfer = [&](evmc_address const& from, evmc_address const& to) -> bool {
        return state.transfer_balance(from, to, value);
    };

    bool ok = false;
    if (scope == FrameScope::TopLevel)
    {
        if ((msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2))
        {
            ok = transfer(msg.sender, msg.recipient);
        }
        else
        {
            ok = transfer(msg.sender, resolveValueTransferRecipient(msg));
        }
    }
    else
    {
        ok = transfer(msg.sender, msg.recipient);
    }

    if (!ok)
    {
        work.ctx.state.revert();
        return FrameResult{
            .result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, work.callMessage().gas)};
    }
    return std::nullopt;
}

/// Chain-specific nested-frame setup (EvmHostHooks).
/// Runs only for FrameScope::Nested, before checkpoint, so hook side effects participate in
/// the frame journal and roll back with revert().
void prepareNestedMessage(FrameWork& work)
{
    auto const callerAddress = resolveCallerAddress(work.ctx.executionAddress, work.callMessage());
    if (work.ctx.extension != nullptr)
    {
        work.ctx.extension->setCallerAddress(callerAddress);
        work.ctx.extension->prepareMessage(work.ctx.revisionConfig.revision, work.callMessage());
    }
}

/// Bind CREATE/CREATE2 recipient from init code (+ salt) and validate init-code size limits.
/// Uses executionAddress as the logical sender for CREATE2 address prediction under DELEGATECALL.
void bindCreateForInit(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    assignCreateAddresses(work.ctx.executionAddress, callMessage,
        bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), work.ctx.state);
}

/// Open a journal checkpoint so this frame's state changes can commit or revert atomically.
void checkpointFrame(FrameWork& work)
{
    work.ctx.state.checkpoint();
}

/// Ensure the CREATE target account exists (EIP-2929 warm is applied before checkpoint).
void initializeCreateAccount(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    initializeCreateTargetAccount(
        work.ctx.state, callMessage.recipient, work.ctx.revisionConfig.revision);
    // geth CreateAccount runs before initcode; SELFDESTRUCT in init must see wasCreatedInTx
    // (legacy immediate code clear and EIP-6780 end-of-tx destruction).
    work.host.markCreatedInTx(callMessage.recipient);
    if (work.ctx.extension != nullptr)
    {
        work.ctx.extension->onCreateTargetInitialized(
            work.ctx.revisionConfig.revision, callMessage);
    }
}

/// Roll back per-tx CREATE tracking when a CREATE/CREATE2 frame aborts after init binding.
void unmarkCreateInTxOnAbort(FrameWork& work)
{
    auto const& createAddr = work.callMessage().recipient;
    // Keep tracking when an earlier successful CREATE in this tx already deployed code here
    // (EEST dynamic_create2: a later collision attempt must not drop wasCreatedInTx before CALL).
    if (work.host.wasCreatedInTx(createAddr) && work.ctx.state.get_code_size(createAddr) != 0)
    {
        return;
    }
    work.host.unmarkCreatedInTx(createAddr);
}

/// EIP-2681: CREATE/CREATE2 fails when sender nonce is already uint64 max (no wrap).
std::optional<FrameResult> failIfCreateSenderNonceOverflow(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind != EVMC_CREATE && callMessage.kind != EVMC_CREATE2)
    {
        return std::nullopt;
    }
    if (isCreateSenderNonceOverflow(work.ctx.state, callMessage.sender))
    {
        return FrameResult{.result = makeFrameResult(EVMC_FAILURE, work.callMessage().gas)};
    }
    return std::nullopt;
}

/// geth evm.create: CanTransfer before AddAddressToAccessList — insufficient balance aborts cold.
std::optional<FrameResult> failIfCreateInsufficientBalance(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind != EVMC_CREATE && callMessage.kind != EVMC_CREATE2)
    {
        return std::nullopt;
    }
    if (state::isZeroBytes32(callMessage.value))
    {
        return std::nullopt;
    }
    if (work.ctx.extension != nullptr && work.ctx.extension->skipHostValueTransfer())
    {
        return std::nullopt;
    }

    auto const value = state::fromEvmC(callMessage.value);
    if (work.ctx.state.get_balance(callMessage.sender) < value)
    {
        return FrameResult{
            .result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, work.callMessage().gas)};
    }
    return std::nullopt;
}

/// geth: AddAddressToAccessList before Snapshot; survives CREATE init/deposit failure.
/// Nested CREATE (parent journal open): journaled warm so outer revert restores cold.
void warmCreateTargetBeforeCheckpoint(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind != EVMC_CREATE && callMessage.kind != EVMC_CREATE2)
    {
        return;
    }
    if (!isCreateWarmPinEnabled(work.ctx.revisionConfig))
    {
        return;
    }
    auto const& createAddr = callMessage.recipient;
    if (state::isZeroAddress(createAddr))
    {
        return;
    }
    if (work.ctx.state.has_checkpoint())
    {
        (void)work.ctx.state.warm_up_address(createAddr);
    }
    else
    {
        (void)work.ctx.state.warm_up_address_no_journal(createAddr);
    }
}

/// geth: CREATE/CREATE2 at an address already self-destructed in this tx (6780 tx-new path).
std::optional<FrameResult> failIfCreateDeploymentBlocked(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind != EVMC_CREATE && callMessage.kind != EVMC_CREATE2)
    {
        return std::nullopt;
    }
    auto const& createAddr = callMessage.recipient;
    if (isCreateDeploymentAddressCollision(
            work.ctx.state, createAddr, work.ctx.revisionConfig.eip6780) ||
        isSameTxSelfDestructedCreateBlocked(
            work.ctx.state, createAddr, work.ctx.revisionConfig.eip6780))
    {
        work.ctx.state.revert();
        return FrameResult{.result = makeFrameResult(EVMC_FAILURE, 0)};
    }
    return std::nullopt;
}

/// Load bytecode (following 7702 delegation if applicable) and run evmone.
/// Host callbacks (storage, balance, nested call) go through EthHost bound in FrameWork.
evmc::Result runVm(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (work.code.empty())
    {
        work.code = loadFrameBytecode(
            work.ctx.state, work.ctx.revisionConfig, callMessage, work.routed.executionAddress);
    }
    // geth: initcode is executed from the code buffer, not msg.input calldata. evmc still carries
    // init bytes in input_data for address prediction / intrinsic gas; zero them for the
    // interpreter so CALLDATA* opcodes see empty input while CODESIZE reflects init length.
    if (callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2)
    {
        auto initMessage = callMessage;
        initMessage.input_data = nullptr;
        initMessage.input_size = 0;
        return work.ctx.vm.execute(work.host, work.ctx.revisionConfig.revision, initMessage,
            work.code.data(), work.code.size());
    }
    return work.ctx.vm.execute(work.host, work.ctx.revisionConfig.revision, callMessage,
        work.code.data(), work.code.size());
}

/// EIP-211: successful CREATE/CREATE2 exposes empty returndata to the caller; failures keep
/// init revert data. Must run after installCreatedContractCode (which consumes deployment output).
void clearCreateSuccessReturnData(evmc::Result& result)
{
    auto& raw = const_cast<evmc_result&>(result.raw());
    if (raw.release != nullptr)
    {
        raw.release(&raw);
    }
    raw.output_data = nullptr;
    raw.output_size = 0;
    raw.release = nullptr;
}

/// geth: ErrCodeStoreOutOfGas (Homestead+) keeps frame journal; warm pre-Snapshot persists.
void commitFailedCreateDepositOutOfGas(
    FrameWork& work, FrameScope scope, evmc_address const& createAddr)
{
    if (scope == FrameScope::Nested)
    {
        if (!state::isZeroAddress(createAddr))
        {
            work.ctx.state.unpin_create_pre_snapshot_warm(createAddr);
            work.ctx.state.journal_warm_address_for_revert(createAddr);
        }
        work.ctx.state.commit();
        return;
    }
    // TopLevel: tx journal owns revert; partial CREATE state stays committed in overlay.
}

/// Post-interpreter frame teardown: gas for code deposit, persist CREATE output, journal action.
///
/// CREATE success path:
///   1. applyCreateCodeDepositGas — EIP-3860/EIP-170 size gas; may downgrade to OOG
///   2. installCreatedContractCode — write runtime code to state
///   3. markCreatedInTx — idempotent re-mark on success (init-time mark at account touch)
///
/// Journal:
///   - Nested success → commit() (merge into parent journal)
///   - Any failure    → revert() (discard since checkpointFrame)
///   - TopLevel       → no commit/revert here; outer pipeline owns tx-level journal
///
/// executionAddress update (nested CALL success only):
///   Propagate delegate target so deeper DELEGATECALL frames see the correct caller context.
FrameResult finalizeFrame(FrameWork& work, FrameScope scope, evmc::Result result)
{
    auto& callMessage = work.callMessage();
    std::optional<CreateCodeDepositReject> depositReject;

    // CREATE: charge code-deposit gas and possibly fail after interpreter returned SUCCESS.
    if (result.status_code == EVMC_SUCCESS &&
        (callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2))
    {
        auto raw = result.release_raw();
        depositReject = applyCreateCodeDepositGas(raw, work.ctx.revisionConfig.revision);
        if (depositReject.has_value() && raw.release != nullptr)
        {
            raw.release(&raw);
            raw.release = nullptr;
            raw.output_data = nullptr;
            raw.output_size = 0;
        }
        if (raw.status_code == EVMC_SUCCESS)
        {
            result = evmc::Result(raw);
        }
        else
        {
            result = makeFrameResult(raw.status_code, raw.gas_left);
        }
    }

    if (result.status_code == EVMC_SUCCESS)
    {
        state::installCreatedContractCode(work.ctx.state, callMessage, result.raw());
        if ((callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2))
        {
            evmc_address createAddr = scope == FrameScope::TopLevel ?
                                          resolveCreateAddress(callMessage, result.raw()) :
                                          callMessage.recipient;
            work.host.markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = callMessage.recipient;
            }
            clearCreateSuccessReturnData(result);
        }

        if (scope == FrameScope::TopLevel)
        {
            // FISCO: bump contract nonce after successful top-level CREATE (outside frame journal).
            if ((callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2) &&
                work.ctx.extension != nullptr)
            {
                auto const createAddr = resolveCreateAddress(callMessage, result.raw());
                work.ctx.extension->finalizeTopLevelCreateNonce(work.ctx.state, createAddr);
            }
        }
        else
        {
            if (callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2)
            {
                auto const& createAddr = callMessage.recipient;
                if (!state::isZeroAddress(createAddr))
                {
                    work.ctx.state.unpin_create_pre_snapshot_warm(createAddr);
                    // Pre-checkpoint warm survives CREATE revert; journal it on success so a
                    // reverting parent call restores cold access (EIP-2929).
                    work.ctx.state.journal_warm_address_for_revert(createAddr);
                }
            }
            work.ctx.state.commit();
            if (!(callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2))
            {
                // CALL / DELEGATECALL: advance execution context for nested delegate routing.
                auto const nextExecution = state::isZeroAddress(callMessage.code_address) ?
                                               callMessage.recipient :
                                               callMessage.code_address;
                if (!state::isZeroAddress(nextExecution))
                {
                    work.ctx.executionAddress = nextExecution;
                }
            }
        }
    }
    else
    {
        evmc_address failedCreateAddr{};
        bool const isCreateFailure =
            callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2;
        if (isCreateFailure)
        {
            failedCreateAddr = callMessage.recipient;
            unmarkCreateInTxOnAbort(work);
        }

        // geth: ErrCodeStoreOutOfGas only skips RevertToSnapshot on Frontier.
        bool const depositOogNoRevert = depositReject == CreateCodeDepositReject::DepositOutOfGas &&
                                        work.ctx.revisionConfig.revision == EVMC_FRONTIER;
        if (depositOogNoRevert && isCreateFailure)
        {
            commitFailedCreateDepositOutOfGas(work, scope, failedCreateAddr);
        }
        else
        {
            work.ctx.state.revert();
            if (isCreateFailure && !state::isZeroAddress(failedCreateAddr) &&
                isCreateWarmPinEnabled(work.ctx.revisionConfig) && !work.ctx.state.has_checkpoint())
            {
                // Top-level / no parent journal: pre-checkpoint no_journal warm survives revert.
                work.ctx.state.pin_create_pre_snapshot_warm(failedCreateAddr);
            }
        }
    }

    logFrameDoneIfNested(work.originalMsg, result);
    return FrameResult{.result = std::move(result)};
}

/// Increment sender nonce for nested CREATE *before* checkpointFrame().
///
/// geth semantics: the nonce bump is not part of the call-frame snapshot, so it persists even
/// when CREATE init/revert rolls back the frame journal. Skipped at depth==0 (top-level nonce
/// is handled by state-transition pre-check). extension->bumpContractCreateNonce covers FISCO
/// contract-account nonce when sender != tx.origin.
void bumpNestedCreateSenderNonce(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (!(callMessage.kind == EVMC_CREATE || callMessage.kind == EVMC_CREATE2) ||
        state::isZeroAddress(callMessage.sender) || work.originalMsg.depth == 0)
    {
        return;
    }

    work.ctx.state.set_nonce(callMessage.sender, work.ctx.state.get_nonce(callMessage.sender) + 1);
    if (work.ctx.extension != nullptr &&
        std::memcmp(callMessage.sender.bytes, work.ctx.txOrigin.bytes,
            sizeof(callMessage.sender.bytes)) != 0)
    {
        work.ctx.extension->bumpContractCreateNonce(callMessage.sender);
    }
}

/// Main frame orchestrator; each step may return early via std::optional<FrameResult>.
FrameResult runFrameSteps(
    CallFrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    // Step 1: normalize addresses and apply 7702 delegation to routed message.
    FrameWork work{
        ctx, message, routeFrameMessage(ctx.state, ctx.revisionConfig, message, scope), {}, host};

    // Step 2: precompile / empty-account / policy fast path.
    if (auto early = runCallTargetFastPath(work, scope))
    {
        return std::move(*early);
    }

    // Step 3: nested-only host hooks (warm/access preparation).
    if (scope == FrameScope::Nested)
    {
        prepareNestedMessage(work);
    }

    // Step 4: CREATE pre-checkpoint — bind, validate, warm (geth: before Snapshot).
    if (work.callMessage().kind == EVMC_CREATE || work.callMessage().kind == EVMC_CREATE2)
    {
        bindCreateForInit(work);
        if (auto early = failIfCreateSenderNonceOverflow(work))
        {
            return std::move(*early);
        }
        if (auto early = failIfCreateInsufficientBalance(work))
        {
            return std::move(*early);
        }
        if (scope == FrameScope::Nested)
        {
            bumpNestedCreateSenderNonce(work);
        }
        warmCreateTargetBeforeCheckpoint(work);
    }

    // Step 5: open frame journal.
    checkpointFrame(work);

    // Step 6: collision check, account touch, value transfer (geth post-Snapshot order).
    if ((work.callMessage().kind == EVMC_CREATE || work.callMessage().kind == EVMC_CREATE2))
    {
        if (auto early = failIfCreateDeploymentBlocked(work))
        {
            return std::move(*early);
        }
        initializeCreateAccount(work);
        if (auto early = transferOrFail(work, scope))
        {
            if (scope == FrameScope::Nested)
            {
                unmarkCreateInTxOnAbort(work);
            }
            return std::move(*early);
        }
    }
    else if (auto early = transferOrFail(work, scope))
    {
        return std::move(*early);
    }

    // Step 7–8: interpreter + finalize (commit/revert, code install).
    auto result = runVm(work);
    return finalizeFrame(work, scope, std::move(result));
}
}  // namespace

/// Public entry; thin wrapper so call sites depend only on EvmCallFrame.h.
FrameResult runCallFrame(
    CallFrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    return runFrameSteps(ctx, message, scope, host);
}

}  // namespace bcos::evm::execution

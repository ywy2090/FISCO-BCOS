# ADR-019: Orchestration Pipeline (`runTxPipeline`)

**Status:** Accepted  
**Date:** 2026-06-23  
**Related:** ADR-005, ADR-015, ADR-016, `bcos-evm/capability-matrix.md`, `docs/superpowers/specs/2026-06-23-orchestration-pipeline-design.md`

---

## Context

Three chain orchestrators (`ethReferenceExecute`, `fiscoExecute`, `opStackExecute`) duplicated the same transaction-level EVM execution pipeline: null validation, `State` construction, `ExecuteMessageInput` assembly, intrinsic gas debit, `adoptResult`, and settlement snapshot capture. The duplication was a shallow module (interface ≈ implementation) with no single enforcement of the invariant that `executeMessage` receives the same `evmc_message` after intrinsic debit.

**Observed defect (OpStack):** `executeEntryChecks` debited `txData.m_message.gas` but `executeMessage` received `input.message` (undebted). Eth/Fisco already passed a local debited `message`.

**Design goals (Q1–Q20, round-2 constraints):**

| # | Decision |
| --- | --- |
| Q1 | Converge three paths **and** fix OpStack gas sync in the same PR |
| Q2 | Kernel covers through settlement snapshot (adopt + snapshot) |
| Q3 | Shared gas math in kernel; fee routing stays in hooks/wrapper |
| Q4 | `TxPipeline` + sync `TxPipelineHooks` |
| Q5 | `debitIntrinsicGas` is portable intrinsic only; OpStack floor/balance in `preDebitEntry` |
| Q6 | State machine / RAII outside kernel; kernel catch is not a revert owner |
| Q7 | `buyGas`/`refundGas` in wrapper; `runTxPipeline` is sync `void` |
| Q8 | Explicit `IntrinsicDebitMode` |
| Q9 | Structured intrinsic failure → `mapIntrinsicFailure` |
| Q10 | `TxPipelineContext` construction-valid, no default constructor |
| Q11 | `mapException(std::exception_ptr)`; chain rethrow/catch in own `.cpp` |
| Q12 | `try/catch` covers steps ②–⑪; step ① validate outside catch |
| Q13 | `TxPipelineExitKind` for early-exit; no automatic post-settle |
| Q14 | `ctx.state` is sole tx-level `State` owner; `buildExecuteMessageInput` sets `ExecuteMessageInput.state = &ctx.state`; `TxExecutionRunner` rejects null `state` and **must not** copy from `StateView` |
| Q15 | No `buildExtension`; wrapper pre-constructs `HostExtension`, stores borrow in `ctx.extension`; ctx non-copyable/non-movable |
| Q16 | `gasPrice` is constructor param; Eth `preExecute` / OpStack `buyGas` may overwrite |
| Q17 | `captureSettlementSnapshot` only when `mode == Eip7623`; OpStack settlement in `opstack/` via `postSettle` |
| Q18 | Final `stateDiff`/`logs` mapping is wrapper-owned; OpStack uses `ctx.state.build_diff()` after all wrapper-side state changes |
| Q19 | After OpStack `buyGas` succeeds, every `exitKind` must settle + refund + `build_diff`; preserve intrinsic→canTransfer→floor precedence |
| Q20 | `preKernel` may mutate state and fail via `throw`; `mapException` owns `state.revert()` + checkpoint; kernel never reverts state |

---

## Decision

### 1. Shared deep module: `eth/pipeline/`

Introduce synchronous `runTxPipeline(TxPipelineContext& ctx, TxPipelineHooks const& hooks)` as the **only** fixed orchestration pipeline. Three execution-bridge entry points become thin wrappers: map input → fill hooks → call pipeline → map output.

**Seam discipline (extends ADR-005):** `eth/pipeline/` and all portable orchestration headers under `eth/` **must not** `#include` `bcos/` or `opstack/` headers. Chain-specific behavior enters only through `TxPipelineHooks` or wrapper code outside the kernel.

**File layout:**

```text
bcos-evm/eth/pipeline/
  TxPipelineContext.h
  TxPipelineHooks.h
  TxPipeline.h / .cpp
  AdoptEvmcResult.h
  IntrinsicGasDebit.h
  BuildExecuteMessageInput.h
  CaptureSettlementSnapshot.h
  NormalizeIncludedTxVmerr.h   // Eth ADR-015; called from Eth postAdopt hook
```

### 2. Fixed 12-step pipeline (kernel)

```text
① validate(vm, hashImpl) — outside try/catch; throws std::invalid_argument
② hooks.prepareMessage(ctx)
③ hooks.preExecute(ctx) → earlyExit?
③½ hooks.preDebitEntry(ctx) → earlyExit?
④ debitIntrinsicGas(ctx.message, hooks.intrinsicPolicy) → earlyExit?
⑤ hooks.preKernel(ctx)        // may mutate ctx.state; failure via throw
⑥ buildExecuteMessageInput(ctx) + hooks.tuneKernelInput
⑦ executeMessage(input)       // input.message == ctx.message (post-debit)
⑧ adoptEvmcResult(...)
⑨ captureSettlementSnapshot     // Eip7623 mode only
⑩ hooks.postAdopt(ctx)
⑪ hooks.postSettle(ctx)
```

Steps ②–⑪ are inside `try/catch`. On exception: `exitKind = ExceptionMapped`, call `mapException(ctx, std::current_exception())`; kernel does **not** revert state.

**Core invariant:** Step ④ mutates `ctx.message`; step ⑦ uses the same reference. OpStack removes dual-track `txData.m_message` vs `input.message`.

### 3. `IntrinsicDebitMode`

| Mode | Semantics | Used by |
| --- | --- | --- |
| `None` | No intrinsic/auth debit | Eth/Fisco when not 7623 and no auth |
| `AuthOnly` | Auth tuple cost only | Eth non-7623 with EIP-7702 auth |
| `Eip7623` | gasLimit minimum + calldata checks + `preExecutionDebit` + auth | Eth 7623; Fisco web3+7623 |
| `OpStackEntry` | `availableGas >= intrinsicDebit` then subtract; no floor/balance here | OpStack normal/deposit |

`debitIntrinsicGas` returns `DebitIntrinsicGasOutcome{ok, failure, gasLeftOnFailure, debitAmount}`. Failure enum includes `GasLimitMinimum`, `CalldataOutOfGas`, `AuthTupleOutOfGas`, `OpStackIntrinsicOutOfGas`. It **must not** construct chain-final `EVMCResult`; `mapIntrinsicFailure` hook maps to chain status.

**Order discipline (not unified across chains):**

| Chain | balance / floor | intrinsic debit |
| --- | --- | --- |
| Eth | ⑤ `preKernel` (`canTransfer`) after debit | ④ kernel |
| Fisco | ⑤ `preKernel` (21000, xfer) after debit | ④ kernel (conditional 7623) |
| OpStack | ③½ `preDebitEntry` before debit | ④ kernel (subtract only) |

### 4. `TxPipelineExitKind`

```cpp
enum class TxPipelineExitKind {
    None,
    PreExecuteRejected,   // ③
    PreDebitRejected,     // ③½
    IntrinsicRejected,    // ④
    KernelCompleted,      // ⑦–⑪ completed
    ExceptionMapped       // catch path
};
```

Early exits at ③/③½/④ set `ctx.earlyExit = true` and the corresponding `exitKind`. Pipeline does **not** automatically run ⑨–⑪. Eth/Fisco wrappers return directly; OpStack wrapper applies §5 settlement matrix.

### 5. Wrapper-out async fee and state machine (ADR-005 preserved)

These remain **outside** `runTxPipeline`:

| Responsibility | Chain | Location |
| --- | --- | --- |
| `co_await buyGas` / `co_await refundGas` | OpStack normal | wrapper, before/after pipeline |
| `GasPoolReturnGuard` | OpStack normal | wrapper around buyGas→refundGas |
| `gasPoolSubGasHook` | OpStack normal | wrapper, before buyGas |
| deposit mint + `state.checkpoint()` | OpStack deposit | wrapper, before pipeline |
| commit / revert / nonce / `returnDepositPoolGas` | OpStack deposit | wrapper, after pipeline |
| L1 fee / operator fee / receipt meta | OpStack normal | wrapper, after refundGas |
| Eth 1559 caps | Eth | ③ `preExecute` |
| Fisco `gas_left < 0` post-check | Fisco | wrapper, after pipeline |

**OpStack settlement/refund matrix (Q19):** Once `buyGas` succeeds (`GasPoolReturnGuard.armed`), **every** `exitKind` must run `postExecuteGasSettlement` + `refundGas` + `ctx.state.build_diff()`. Failures before buyGas return directly with no settle/refund.

**Output mapping (Q18):** Kernel does not produce final `stateDiff`/`logs`. OpStack **must** call `ctx.state.build_diff()` as the tail step after all wrapper-side balance changes (buyGas/refundGas/mint/nonce), never `kernelOutput.stateDiff`.

### 6. `TxPipelineContext` and `ExecuteMessageInput` state ownership

**Cold read vs mutable journal (two seams, one tx):**

| Layer | Type | Role |
| --- | --- | --- |
| Wrapper / bridge request | `StateView const*` (`stateView`) | Cold account view passed into `TxPipelineContext` constructor |
| Pipeline | `TxPipelineContext::state` (`state::State`) | Sole mutable journal for the tx (warm, checkpoint, diff) |
| Kernel step ⑦ | `ExecuteMessageInput::state` (`state::State*`) | **Must** point at `&ctx.state` when invoked from `runTxPipeline` |

**Rules (Q14 enforcement):**

- `TxPipelineContext` is constructed with `StateView const&`, initial `evmc_message`, `RevisionConfig`, `gasPrice`; it wraps the reader in `state::State`.
- Explicit `= delete` copy/move; pipeline sole owner of `state::State` and mutable `evmc_message`.
- `buildExecuteMessageInput(ctx)` sets `input.state = &ctx.state` (not the wrapper's cold `stateView` pointer).
- `TxExecutionRunner::run` dereferences `input.state` directly; **`resolveState` / `dynamic_cast` / silent `State` copy from `StateView` is forbidden** — passing a bare reader pointer would mutate a discarded journal and break warm/nonce visibility.
- Direct `executeMessage()` callers (tests, `transition()`) must construct `state::State{reader}` and pass `&state`.
- Null `input.state` or `input.vm` → `throw std::invalid_argument("executeMessage requires State owner and vm")`.
- `extension` is a borrow pointer set by wrapper before `runTxPipeline` (no `buildExtension` hook).
- `originalGasLimit` captured at construction for settlement/snapshot.

**Tests:** `TxPipelineTest.pipeline_passes_ctx_state_pointer_to_execute_message` asserts `execInput.state == &ctx.state`; `TxExecutionRunnerTest` asserts mutations (e.g. sender nonce bump) are visible on the caller's `State`.

### 7. Test-only OpStack spy seam

`OpStackExecuteMessageTestHook.h` (guarded by `BCOS_EVM_TESTING`) provides `setExecuteMessageSpy` / `maybeCallExecuteMessageSpy` so tests can assert `ExecuteMessageInput.message.gas == originalGasLimit - intrinsicDebit`.

`OpStackIntrinsicGasSyncTest` compiles OpStack sources with `BCOS_EVM_TESTING` and does **not** link ordinary `bcos-evm-op`, avoiding production library pollution.

OpStack balance/floor logic lives in `opstack/OpStackFloorGasPrecheck.*` and enters the pipeline via `preDebitEntry` hook only.

---

## Consequences

- Three anonymous `adoptResult` copies replaced by single `adoptEvmcResult`.
- `ExecuteMessageInput` 13-field assembly is single-point via `buildExecuteMessageInput`.
- `capability-matrix.md` TE baseline column semantics note orchestration via `runTxPipeline`.
- ADR-005 §4 documents the `eth/pipeline/` boundary and wrapper-out fee/state-machine rule.
- Eth/Fisco behavioral equivalence expected; OpStack intrinsic path documents expected gas delta (fix).
- New orchestration behavior must extend hooks or wrapper code, not add chain includes under `eth/`.

---

## Compliance checklist

- [ ] New orchestration step added only inside `runTxPipeline` or documented hook phase.
- [ ] No `bcos/` or `opstack/` includes under `eth/pipeline/`.
- [ ] Intrinsic failure uses structured outcome + `mapIntrinsicFailure`, not inline `EVMCResult` in kernel.
- [ ] Exception path uses `mapException(std::exception_ptr)`; Fisco rethrow/catch stays in `FiscoExecute.cpp`.
- [ ] OpStack fee/state machine changes stay in wrapper, not pipeline steps.
- [ ] `capability-matrix.md` updated when orchestration capability surfaces change.
- [ ] OpStack tests use spy seam for message-gas sync when asserting intrinsic debit propagation.
- [ ] `ExecuteMessageInput.state` is a `state::State*` journal owner; no `StateView`-only fallback in `TxExecutionRunner`.
- [ ] Pipeline path: `buildExecuteMessageInput(ctx).state == &ctx.state`.

# Geth Naming Phase 2+ Implementation Plan

**Branch:** `feat/adr-030-geth-naming`  
**Baseline:** `6e1a6a2e0`  
**Authority:** ADR-029, ADR-030  
**Out of scope:** Phase 5 orchestration type renames; Phase 6 TE ABI removal

## Global Constraints

- Preserve Tier E stable ABI: `executeMessage`, `fiscoExecute`, `ethReferenceExecute`, `opStackExecute` MUST remain as callable symbols (aliases add new names; do not remove old).
- `eth/` MUST NOT `#include` `bcos/` or `opstack/` headers (ADR-005).
- New alias symbols live primarily in `bcos-evm/eth/GethNamingAliases.h` plus minimal forwarding on `ChainPrecheckPolicy` / `OrchestrationErrorPolicy` where ADR-030 §8 requires.
- Use `// geth: <symbol> — ADR-030` comments when touching pipeline/kernel steps.
- Run focused ctest before commit; full eth test subset for Tasks 1–2.
- Follow existing clang-format; pre-commit hook must pass.
- Do NOT commit `.superpowers/sdd/` artifacts, `.codegraph/`, or unrelated WIP.

---

## Task 1: Phase 2 — Geth alias layer + test

Implement ADR-030 §8 Tier A inline aliases (coexist with current names).

### 1.1 Expand `bcos-evm/eth/GethNamingAliases.h`

Add `#include` guards and forward declarations as needed. Implement:

| Alias | Forwards to (current) |
| --- | --- |
| `stateTransitionExecute(ctx, precheck, error)` | `runTxPipeline` |
| `deductIntrinsicGas(msg, policy)` | `debitIntrinsicGas` |
| `innerExecute(input)` | `executeMessage` |
| `prepareState(...)` | `execution::warmTransactionEntry(...)` (same signature) |
| `applyReferenceMessage(input)` | `ethReferenceExecute` — declare in header, define in `.cpp` or include reference header |
| `applyFiscoMessage(input)` | `fiscoExecute` — forward declare; define in small `.cpp` in `eth/` that includes `bcos/FiscoExecute.h` **only in .cpp** OR add aliases in each chain header (preferred: aliases in chain headers to avoid eth→bcos link) |
| `applyOpStackMessage(input)` | `opstackExecute` — same pattern in `opstack/OpStackExecute.h` |
| `evmCall` / `evmCreate` / `evmDelegateCall` / `evmStaticCall` | overloads wrapping `runExecutionFrame` with appropriate `FrameScope` / kind checks |

**Chain ApplyMessage aliases:** add `inline`/`[[nodiscard]]` forwards in:
- `bcos-evm/eth/reference/EthReferenceExecute.h`
- `bcos-evm/bcos/FiscoExecute.h`
- `bcos-evm/opstack/OpStackExecute.h`

Do NOT add bcos/opstack includes to `GethNamingAliases.h`.

### 1.2 ChainPrecheckPolicy geth slice aliases

In `ChainPrecheckPolicy.h`, add non-virtual inline methods (default forwards to existing virtuals):

- `preCheckRules` → `checkTransactionRules`
- `preCheckGasAffordable` → `checkGasAffordable`
- `preCheckCanTransfer` → `checkBalanceAndValue`
- `normalizeMessage` → `setupMessage`
- `pipelineInvokeEvmKernel` → `runEvmExecution` (ADR-029 name as alias before Phase 1 rename)

### 1.3 OrchestrationErrorPolicy

Add inline alias `finalizeGasUsed(ctx)` → `onPostExecuteNormalize(ctx)`.

### 1.4 TxPipeline geth comments

Add `// geth: stateTransition.execute — ADR-030` above `runTxPipeline` in `TxPipeline.cpp`; add step comments for preCheck / IntrinsicGas / innerExecute path.

### 1.5 Test `bcos-evm/test/eth/GethNamingAliasesTest.cpp`

- Assert `deductIntrinsicGas` ≡ `debitIntrinsicGas` on a trivial None-mode case.
- Assert `innerExecute` symbol resolves (link test with minimal mock or call through stub — at minimum compile-time + address equality of inline forwards).
- Assert `ChainPrecheckPolicy` default impl: create a test policy, verify `preCheckRules` calls through (use counting override of `checkTransactionRules`).
- Register in `bcos-evm/test/cmake/EthTests.cmake` as `GethNamingAliasesTest`.

### 1.6 Update `GethNamingAliases.h` index comment to list alias symbols (not stale setupMessage-only list).

**Acceptance:** `GethNamingAliasesTest` passes; build succeeds; no Tier E symbol removed.

---

## Task 2: Phase 1 — ADR-029 pipeline prefix renames

Rename `ChainPrecheckPolicy` virtual API and all overrides (Eth/Fisco/OpStack + tests):

| Old | New |
| --- | --- |
| `setupMessage` | `pipelineSetupMessage` |
| `checkTransactionRules` | `pipelineCheckRules` |
| `checkGasAffordable` | `pipelineCheckGasAffordable` |
| `checkBalanceAndValue` | `pipelineCheckBalance` |
| `tuneExecutionInput` | `pipelineTuneKernelInput` |
| `runEvmExecution` | `pipelineInvokeEvmKernel` |

Update `TxPipeline.cpp` call sites and trace log step names.

Add **deprecated inline aliases** on `ChainPrecheckPolicy` for old names forwarding to new (1 release).

Rename `TxExecutionRunner::run` → `runEvmKernelTopLevel` with deprecated `run` alias.

Rename `runExecutionFrame` → `runCallFrame` with deprecated alias in `ExecutionFrame.h`.

Update geth aliases from Task 1:
- `preCheckRules` → `pipelineCheckRules`
- `normalizeMessage` → `pipelineSetupMessage`
- `innerExecute` unchanged (still `executeMessage`)

**Acceptance:** eth orchestration + TxPipeline tests pass; `FiscoOrchestrationProfileTest`, `OpStackOrchestrationProfileTest` pass.

---

## Task 3: Phase 4a — apply*Message as primary documented names

- Ensure `applyReferenceMessage`, `applyFiscoMessage`, `applyOpStackMessage` exist (Task 1).
- Add `[[deprecated("use applyReferenceMessage")]]` on `ethReferenceExecute` etc. OR document-only in ADR (prefer **comment + alias only**, no deprecation attribute on Tier E yet).
- Update `bcos-evm/eth/README.md`, `architecture-overview.md` dual-label flows.
- Update `GethNamingAliases.h` index.

**Acceptance:** docs build; no TE breakage.

---

## Task 4: Phase 4b — Rename `eth/reference/` → `eth/apply/`

- Move directory and update all `#include` paths + CMake.
- Optionally rename `EthReferenceExecute` → keep function name stable; rename files to `ApplyReferenceMessage.h` OR keep file names and only move directory (prefer: **directory only** `eth/apply/`, keep `EthReferenceExecute.*` filenames for minimal TE churn).

**Acceptance:** build + eth reference tests pass.

---

## Task 5: Phase 3 — Kernel hard renames (batch 1)

Rename canonical symbols (keep deprecated aliases):

| New canonical | Old |
| --- | --- |
| `deductIntrinsicGas` | `debitIntrinsicGas` (move implementation name) |
| `runCallFrame` | already done in Task 2 |
| `runEvmKernelTopLevel` | already done in Task 2 |

Defer `runTxPipeline` → `stateTransitionExecute` and `executeMessage` → `innerExecute` to avoid TE churn (document as Phase 3b follow-up in progress ledger).

**Acceptance:** intrinsic gas tests pass; grep shows `debitIntrinsicGas` only as deprecated alias.

---

## Task 6: Docs / ADR compliance

- Fix ADR-030 checklist: mark GethNamingAliasesTest done.
- Remove stale `EthPipelineHookBinder` from `eth/README.md`.
- Add parity PR template note (one paragraph in ADR-030 or review-pack).

**Acceptance:** markdown only; no code behavior change.

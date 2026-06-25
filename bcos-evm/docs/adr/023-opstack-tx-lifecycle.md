# ADR-023: OpStack Transaction Lifecycle (Deep Module)

**Status:** Accepted (C0–C2 complete)  
**Date:** 2026-06-25  
**Related:** ADR-019, ADR-021, `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`, architecture review candidate #1 (OpStack outer-ring wiring)

---

## Context

ADR-021 deepened **settlement math** (`finalizeNormal` / `finalizeDeposit`) and **async settlement facades** (`settleNormal` / `settleDeposit`). `TxPipelineContext` is the gas/message/state single source for normal L2 txs.

Understanding one OpStack user transaction still requires reading **`OpStackExecutionBridge::opStackExecute`** across:

- `OpStackFeeContext` manual initialization (20+ fields)
- `OpStackPrecheckPolicy::checkEntryRules` (sync validation before buyGas; C3: consolidated from `opStackTxPrecheck`)
- deposit pre-pipeline (`depositNonce` → `mint` → `checkpoint`)
- normal `gasPool.subGas` → `buyGas` → `runTxPipeline` → `settleNormal`
- buyGas-failure `gasPool.returnGas` in bridge (success path `returnGas` in `settleNormal`)
- `receiptMeta` projection (`l1Fee`, `operatorFee`, `depositNonce`)

The bridge is a **shallow adapter** with **low locality**: combination bugs (earlyExit × buyGas × gasPool × receipt) have no single module interface to test.

Grilling decisions D12–D18 (2026-06-25) resolved interface shape and delivery phasing.

---

## Decision

### 1. Deep module: `OpStackTxLifecycle`

Introduce `runOpStackTxLifecycle(OpStackExecutionRequest)` as the **OpStack outer-ring deep module** in `opstack/`.

`opStackExecute` remains the **TE-stable adapter**: validate `stateView` / `vm` / `hashImpl`, then delegate to lifecycle.

| Export | Role |
| --- | --- |
| `OpStackTxLifecycle.h` → `runOpStackTxLifecycle` | Deep module interface; primary characterization test surface after C1 |
| `OpStackExecutionBridge.h` → `opStackExecute` | Stable TE seam |

### 2. Compose ADR-021 — do not merge

| Module | Responsibility | Lifecycle relationship |
| --- | --- | --- |
| `runTxPipeline` (eth) | Fixed 12-step kernel | lifecycle calls via `OpStackOrchestrationProfile::bind` |
| `finalizeNormal` / `finalizeDeposit` | Sync gas/journal math | lifecycle calls only through `settle*` |
| `settleNormal` / `settleDeposit` | Async: finalize → refund → gasPool | lifecycle sole caller post-pipeline |
| `OpStackTxFeeLedger` | buyGas / refundGas | lifecycle calls; not on bridge after C1 |

ADR-021 invariants unchanged.

### 3. Internal session (not interface)

`OpStackTxSession` (private to lifecycle implementation):

- `TxPipelineContext ctx`
- `OpStackFeeContext fee`
- `OpStackVmHostPolicy host`
- `depositNonceSnapshot` (deposit only)

`feeCtx` does not cross the lifecycle external seam.

### 4. Lifecycle step order

```text
populateFeeContext → Profile::bind (once)
OpStackPrecheckPolicy::checkEntryRules(ctx)   // sync rules before buyGas (nonce/7702/blob/feeCap)
branch:
  deposit:
    acquireGasPool
    runDepositPrePipeline (depositNonce → mint → checkpoint)
    runTxPipeline (checkGasAffordable: floor + post-buyGas canTransfer)
    settleDeposit → projectOutput
  normal:
    acquireGasPool
    buyGas → on fail: releaseGasPool(limit, 0); early exit
    runTxPipeline (checkTransactionRules: no-op; checkGasAffordable after buyGas)
    settleNormal → projectOutput
```

### 4½. OpStack sync precheck module (C3)

All **sync** precheck rules live in `OpStackPrecheckPolicy` (two phases):

| Phase | Method | When | Rules |
| --- | --- | --- | --- |
| Entry | `checkEntryRules` | lifecycle, before `buyGas` | nonce, 7702 sender, EIP-1559 caps, blob intent, auth+CREATE |
| Gas afford | `checkGasAffordable` | `runTxPipeline`, after `buyGas` (normal) | floor data gas, `canTransfer(value)` on post-debit balance |

`checkTransactionRules` remains **no-op** for OpStack (pipeline step ②). Async `buyGas`/`refundGas`/`settle*` stay in lifecycle per ADR-019 Q7.

Deleted: `opStackTxPrecheck` free function. `isDepositTx()` → `OpStackDepositTx.h`.

### 5. GasPool ownership (C2)

Lifecycle is the **only** module that calls `gasPool.subGas` / `gasPool.returnGas` (including buyGas-failure abort). Precheck becomes side-effect-free.

Private helpers: `acquireGasPool`, `releaseGasPool`.

### 6. Profile wiring

Lifecycle **inlines** `OpStackOrchestrationProfile::bind(session)` — hooks are not injected through the external interface (D13).

### 7. Early-exit result contract (D18)

| Path | Guaranteed fields | Not guaranteed |
| --- | --- | --- |
| precheck failure | `evmcResult` | `gasUsed`, `stateDiff`, `receiptMeta`, `logs` |
| buyGas / subGas failure | `evmcResult`; gasPool released | same as above |
| full path | all via `projectOutput` | — |

### 8. Phased delivery

| Phase | Scope | Status |
| --- | --- | --- |
| **C0** | ADR-023 (this doc) + `OpStackTxLifecycleCharacterizationTest` (6 paths via `opStackExecute`) | Done |
| **C1** | `runOpStackTxLifecycle`; bridge delegate; tests target lifecycle | Done |
| **C2** | Precheck pure; gasPool centralized in lifecycle | Done |
| **C3** | Sync precheck consolidated in `OpStackPrecheckPolicy` (entry + gasAffordable phases) | Done |

### 9. Characterization matrix (C0 oracle)

| # | Scenario | Key assertions |
| --- | --- | --- |
| 1 | Normal SUCCESS | fee routing (sender/coinbase/L1/operator), `gasUsed`, `l1Fee`, gasPool once |
| 2 | Normal IntrinsicRejected | `gasUsed == 0`, gasPool `returnGas(fullLimit, 0)` |
| 3 | Normal buyGas fail | `NotEnoughCash`, gasPool `returnGas(limit, 0)`, no settle |
| 4 | Deposit SUCCESS + mint | mint retained, `depositNonce`, nonce +1 |
| 5 | Deposit REVERT | balance revert, nonce +1, actual `gasUsed` |
| 6 | Deposit IntrinsicRejected | `gasUsed == gasLimit`, balance revert, nonce +1 |
| 7 | Deposit gasPool reject | `EVMC_OUT_OF_GAS`, subGas once, no returnGas, no mint |

---

## Consequences

### Positive

- Single interface for outer-ring OpStack tx story; bridge locality after C1.
- Characterization tests lock combination behavior before refactor.
- ADR-021 settlement layers stay independently testable.

### Negative / trade-offs

- Temporary dual entry (`opStackExecute` + `runOpStackTxLifecycle`) until TE migrates naming (TE keeps `opStackExecute`).
- C2 moves deposit `subGas` from precheck — behavior-equivalent, tests required.

---

## Compliance (C0 reviewers)

- [ ] ADR-023 documents D12–D18 decisions
- [x] `OpStackTxLifecycleCharacterizationTest` — 7 cases green via `runOpStackTxLifecycle`
- [ ] No production code change in C0 (tests + ADR only)
- [ ] `eth/` seam discipline unchanged (ADR-005)

## Compliance (C1 reviewers)

- [x] `runOpStackTxLifecycle` owns bridge body
- [x] `opStackExecute` is validate + delegate only
- [x] Characterization tests call `runOpStackTxLifecycle`
- [x] Zero behavior change vs C0 oracles

## Compliance (C2 reviewers)

- [x] Entry precheck has no `gasPool` side effects
- [x] Lifecycle `acquireGasPool` used for deposit and normal
- [x] Lifecycle `releaseGasPoolFullLimit` on buyGas failure
- [x] ADR-023 Status → Accepted

## Compliance (C3 reviewers)

- [x] `OpStackPrecheckPolicy::checkEntryRules` owns former `opStackTxPrecheck` logic
- [x] Lifecycle bind-once; entry rules before buyGas; pipeline `checkGasAffordable` unchanged
- [x] `OpStackPrecheckPolicyTest` + migrated entry-precheck tests green
- [x] `OpStackTxLifecycleCharacterizationTest` regression green

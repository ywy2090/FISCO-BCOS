# VM Execution Domain — EVMC-Only Semantics (Reference Paths)

**Date:** 2026-06-26  
**Status:** Draft (brainstorm approved: Approach B)  
**Scope:** ETH reference + OpStack reference orchestration (`ethReferenceExecute`, `opStackExecute` / lifecycle)  
**Out of scope:** FISCO `bcos/` production baseline, `bcos-protocol/TransactionStatus` enum changes, receipt wire format changes  
**Related:** ADR-005, ADR-015, ADR-019, ADR-028 (draft), GAP-005/009/010/011, `error-handling-geth-parity-report-2026-06-26-v2.md`

---

## 1. Problem

Today `bcos-evm` mixes three error representations inside and around the EVM kernel:

| Layer | Representation | Issue |
| --- | --- | --- |
| VM frames | `evmc_status_code` + `gas_left` | Correct domain language |
| `EVMCResult` | `evmc_result` **+** `protocol::TransactionStatus` | Dual field; VM-adjacent code sets both |
| Mapping | `evmcStatusToTransactionStatus` vs `evmcStatusToErrorMessage` | **GAP-010:** different coverage; narrow table throws (`GAP-005/011`) |
| Precheck | `EVMC_INSUFFICIENT_BALANCE` + `InsufficientFunds(10015)` vs execution `NotEnoughCash(7)` | **GAP-009:** same EVMC, different TS |

geth separates **vm.Error** (execution) from **preCheck / pool errors** (no EVM). Reference paths should mirror that: **VM execution domain speaks EVMC only**; framework status is projected once at a defined boundary.

---

## 2. Decision (Approach B)

**Pipeline `adoptEvmcResult` is the first `TransactionStatus` projection point.**  
**Bridge / lifecycle may override status and attach outcome flags** (ADR-015 normalize, ADR-028 consensus reject).  
**TE consumes `EVMCResult.status`; it does not re-implement mapping.**

Rejected alternatives:

| Approach | Why not |
| --- | --- |
| A. Bridge-only first projection | EEST/GST adapter and pipeline characterization need status before TE; duplicates adopt |
| C. New `VmFrameResult` type everywhere | Large blast radius; same semantics achievable incrementally |

---

## 3. Domain boundary

### 3.1 Inside VM execution domain (EVMC only)

Modules:

- `eth/execution/ExecutionFrame.*`
- `eth/execution/ExecuteMessage.*` (through `runExecutionFrame` return)
- `eth/state/EthHost.cpp` (`call` / `create` host callbacks)
- `eth/vm/VMInstance.cpp`
- `eth/precompiled/PrecompileRouter.cpp` and precompile dispatch bodies returning `evmc::Result`

**Allowed:** `evmc_status_code`, `evmc_result` / `evmc::Result`, `gas_left`, `EVMC_PRECOMPILE_FAILURE`.

**Forbidden:**

- `#include "bcos-protocol/TransactionStatus.h"`
- `EVMCResult(..., TransactionStatus::*)` construction
- Orchestration statuses: `OutOfGasLimit`, `Malformed`, `InsufficientFunds`, etc.

**Frame helpers** (e.g. `makeFrameResult(status, gasLeft)`) take only EVMC status + gas.

### 3.2 Outside VM domain (orchestration + bridge)

- `eth/pipeline/TxPipeline.*`, `*PrecheckPolicy*`, `*OrchestrationErrorPolicy*`
- `eth/reference/EthReferenceBridge.*`, `opstack/OpStackTxLifecycle.*`
- `transaction-executor/*` (receipt assembly)

May use `TxPipelineExitKind`, `TxConsensusOutcome` (ADR-028), and `TransactionStatus` on `EVMCResult` **after** adopt or via explicit override structs.

### 3.3 ADR-015 exception (orchestration, not VM)

Top-level included vmerr normalization (`normalizeIncludedTxVmerr`) runs in **ErrorPolicy / bridge**, never inside `ExecutionFrame`. VM still returns raw EVMC; orchestration may set `status_code = EVMC_SUCCESS` and `status = None` on the exported `EVMCResult`.

---

## 4. Single mapping table

### 4.1 New module: `eth/EvmcStatusMap.h`

```cpp
namespace bcos::evm {

protocol::TransactionStatus mapEvmcToTransactionStatus(evmc_status_code status) noexcept;

}  // namespace bcos::evm
```

**Contract:**

- Single authoritative EVMC → `TransactionStatus` table for reference paths.
- **Never throws** (`UnknownEVMCStatus` removed from hot path — GAP-005/011).
- `default` → `TransactionStatus::Unknown`.

**Initial mapping** (align with Plan Task 5 / `EvmcStatusMappingTest` oracle):

| `evmc_status_code` | `TransactionStatus` |
| --- | --- |
| `EVMC_SUCCESS` | `None` |
| `EVMC_REVERT` | `RevertInstruction` |
| `EVMC_OUT_OF_GAS` | `OutOfGas` |
| `EVMC_INSUFFICIENT_BALANCE` | `NotEnoughCash` |
| `EVMC_STACK_OVERFLOW` | `OutOfStack` |
| `EVMC_STACK_UNDERFLOW` | `StackUnderflow` |
| `EVMC_INVALID_INSTRUCTION`, `EVMC_UNDEFINED_INSTRUCTION` | `BadInstruction` |
| `EVMC_BAD_JUMP_DESTINATION` | `BadJumpDestination` |
| `EVMC_INVALID_MEMORY_ACCESS` | `StackUnderflow` (evmone compat; documented deviation) |
| `EVMC_STATIC_MODE_VIOLATION` | `Unknown` (geth `ErrWriteProtection` has no 1:1 TS) |
| `EVMC_INTERNAL_ERROR`, `EVMC_FAILURE`, `EVMC_REJECTED`, `EVMC_OUT_OF_MEMORY`, `EVMC_CONTRACT_VALIDATION_FAILURE`, `EVMC_PRECOMPILE_FAILURE` | `Unknown` |
| unlisted / future | `Unknown` |

### 4.2 Refactor existing APIs

| API | New behavior |
| --- | --- |
| `evmcStatusToTransactionStatus` | Thin alias → `mapEvmcToTransactionStatus` |
| `evmcStatusToErrorMessage` | `status = mapEvmcToTransactionStatus(...)` + optional ABI output bytes (unchanged messages) |
| `adoptEvmcResult` | `EVMCResult(raw, mapEvmcToTransactionStatus(raw.status_code))` |
| `EVMCResult(evmc_result)` single-arg ctor | Same map; no throw |
| `VMInstance::execute` | Uses single-arg ctor → no longer throws on unmapped status |

Deprecate duplicate switch bodies in `EVMCResult.cpp` (one implementation).

---

## 5. GAP-009 — precheck vs execution

On reference paths, **precheck failures are not VM errors**.

| Current | Target (with ADR-028) |
| --- | --- |
| `EVMC_INSUFFICIENT_BALANCE` + `InsufficientFunds` on precheck | `consensusOutcome = Rejected`; optional trace `evmcResult`; **no receipt** |
| EVM nested/top-level transfer fail | `EVMC_INSUFFICIENT_BALANCE` + `NotEnoughCash` via **map only** at adopt |

Until ADR-028 lands, precheck may still set `EVMCResult(..., InsufficientFunds)` in **PrecheckPolicy only** — not in VM domain. Document as temporary bridge override, not VM semantics.

---

## 6. Data flow

### ETH reference

```text
runTxPipeline
  → executeMessage → runExecutionFrame → evmc::Result
  → adoptEvmcResult → EVMCResult (first TS)
  → ErrorPolicy.onPostExecuteNormalize (ADR-015)
ethReferenceExecute
  → if consensusRejected: override / skip receipt meta
  → EthReferenceResult
TE Execute
  → applyStateDiff / settleGas / makeReceipt(read status)
```

### OpStack reference

Same adopt seam; `OpStackOrchestrationErrorPolicy` intrinsic mapping remains trace-only once ADR-028 marks `consensusOutcome = Rejected` for entry failures.

---

## 7. Delivery phases

| Phase | Deliverable | Closes |
| --- | --- | --- |
| **P0** | `EvmcStatusMap.h`; unify `EVMCResult.cpp`; flip `EvmcStatusMappingTest` | GAP-005/010/011 |
| **P1** | CI/lint: VM domain files must not include `TransactionStatus.h` | discipline |
| **P2** | `FrameResult` / `ExecuteMessageOutput` document `evmc::Result` only; remove stray `EVMCResult` in execution headers | purity |
| **P3** | Precheck decoupled from fake EVMC + ADR-028 `consensusOutcome` | GAP-009, GAP-001/002 |
| **P4** | ADR-029 + capability-matrix note | docs |

**YAGNI (explicit non-goals):**

- FISCO `bcos/` path migration
- Replacing `TransactionStatus` in protocol
- Exposing raw EVMC on receipt wire

---

## 8. Testing

| Test | Purpose |
| --- | --- |
| `EvmcStatusMappingTest` | Single table; adopt == single-arg ctor; no throw |
| `ExecutionFrameTest`, `InsufficientBalanceGasLeftTest` | VM domain EVMC + gas only |
| New `VmDomainIncludesGuardTest` (optional) | compile test or grep gate for forbidden includes |
| EEST smoke (`specs-tests-smoke`) | No stateRoot regression |
| `EthIncludedTxVmerrTest`, ADR-015 cases | Normalize still orchestration-side |

---

## 9. Success criteria

1. All known EVMC statuses map without throw on pipeline and `VMInstance` paths.
2. `evmcStatusToTransactionStatus` and `evmcStatusToErrorMessage` agree on status for every EVMC code in the test matrix.
3. No `TransactionStatus` includes in `eth/execution/`, `EthHost.cpp`, `VMInstance.cpp`.
4. Reference path behavior unchanged except: fixed throws (GAP-011), unified mapping (GAP-010).
5. FISCO baseline untouched.

---

## 10. Open follow-ups (not blocking P0)

- `STATIC_MODE_VIOLATION` → dedicated TS vs `Unknown` (needs product/RPC decision)
- `INVALID_MEMORY_ACCESS` → `StackUnderflow` naming debt vs geth
- Full ADR-028 inclusion reject (separate spec)

---

## Spec self-review (2026-06-26)

- [x] No TBD placeholders in normative sections
- [x] Scope limited to ETH/OpStack reference (§1, §7 YAGNI)
- [x] Consistent with ADR-015 (normalize outside VM) and ADR-028 draft
- [x] P0–P4 ordered; P0 independently shippable
- [x] GAP-009 explicitly deferred to P3 / ADR-028 (no contradiction with P0)

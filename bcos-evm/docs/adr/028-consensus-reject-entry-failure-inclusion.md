# ADR-028: Consensus Reject on Entry Failure (Inclusion Parity)

**Status:** Proposed  
**Date:** 2026-06-26  
**Related:** ADR-015, ADR-019, ADR-023, ADR-025, ADR-026, GAP-001/002 (`error-handling-geth-parity-report-2026-06-26-v2.md`)  
**geth anchor:** `core/state_transition.go` `preCheck` / `execute()` error return; `core/state_processor.go` `ApplyTransaction` err → no receipt  
**op-geth anchor:** same as geth for normal L2; deposit entry failures remain intentionally included (ADR-021/023)

---

## Context

Wave 3 error-handling parity review (**GAP-001**, **GAP-002**) identified a split between **settlement/state** and **block inclusion** for transactions that fail **before EVM execution**:

| Layer | geth / op-geth (normal L2) | FISCO (current, post ADR-025) |
| --- | --- | --- |
| Orchestration | `preCheck()` / intrinsic / afford failure → `execute()` returns error | Pipeline early-exit; ErrorPolicy maps to `EVMC_OUT_OF_GAS` + `OutOfGasLimit` |
| State / fee (OpStack) | No debit beyond failed preCheck; tx not applied | **ADR-025 closed:** `abortNormalAfterBuyGas`, `gasUsed = 0`, empty `stateDiff`, no phantom fee meta |
| TE Finalize | **No receipt**; tx **not in block** | **`makeReceipt()` always runs** → failed receipt with `OutOfGasLimit(2)`, often `gasUsed = gasLimit` |
| Block assembly | `ApplyTransaction` err aborts block application for that tx index | `SchedulerSerialImpl` **unconditionally** `receipts.emplace_back(...)` |

**ADR-025 explicitly deferred inclusion:** orchestration outputs after entry failure are state-consistent, but TE may still include the tx with failed status. This ADR closes that gap for **ETH reference** and **OpStack normal L2** paths.

**In scope:** pre-execution rejections that geth treats as **consensus-invalid for inclusion** — the tx must not appear in the block’s receipt list and must not mutate committed chain state (nonce, balance debits beyond pool accounting, storage).

**Out of scope:**

- **Deposit txs** — Regolith+ entry failure remains **included** with `gasUsed = gasLimit` (ADR-021/023).
- **Included top-level vmerr** — `INVALID`, `REVERT`, `OUT_OF_GAS` after EVM starts remain **included** with receipt (ADR-015).
- **FISCO production baseline** — may retain legacy include semantics behind an explicit chain flag (§7).
- **Nested frame errors** (GAP-004), **TE gas settlement drift** (GAP-TE-002), **EVMC dual mapping** (GAP-009+) — separate ADRs/issues.

---

## Decision

### 1. Shared outcome taxonomy: `TxConsensusOutcome`

Introduce a portable enum in `eth/` (proposed path: `eth/TxConsensusOutcome.h`):

```cpp
enum class TxConsensusOutcome {
    Executed,   // tx may produce receipt (success, revert, or ADR-015 included vmerr)
    Rejected,   // geth preCheck-equivalent; must not produce receipt or commit tx state
};

inline bool isConsensusRejected(TxPipelineExitKind exitKind) noexcept
{
    switch (exitKind)
    {
    case TxPipelineExitKind::RulesRejected:
    case TxPipelineExitKind::GasAffordRejected:
    case TxPipelineExitKind::IntrinsicRejected:
        return true;
    default:
        return false;
    }
}
```

| `TxPipelineExitKind` | `TxConsensusOutcome` | geth analogue |
| --- | --- | --- |
| `RulesRejected` | `Rejected` | `preCheck` rule failure (nonce caps, malformed typed tx, …) |
| `GasAffordRejected` | `Rejected` | `preCheck` balance / floor / transfer afford |
| `IntrinsicRejected` | `Rejected` | `IntrinsicGas` failure |
| `Completed` | `Executed` | EVM ran (incl. ADR-015 vmerr normalization) |
| `ExceptionHandled` | `Executed`* | Mapped exception after checkpoint revert; not a preCheck reject |

\* `ExceptionHandled` continues existing ErrorPolicy + settlement behavior; it is **not** promoted to `Rejected` unless a future ADR proves geth excludes that class.

**TE-layer reject (outside pipeline exit kind):**

| Condition | `TxConsensusOutcome` |
| --- | --- |
| `EthTxFeeSettlement::buyGas` / `OpStackNormalTxFeeCoordinator::buyGas` returns false | `Rejected` |
| OpStack `acquireGasPool` failure (normal L2) | `Rejected` |

### 2. Bridge / lifecycle propagation

Extend result structs:

```cpp
// EthReferenceResult, OpStackExecutionResult
TxConsensusOutcome consensusOutcome{TxConsensusOutcome::Executed};
```

**ETH reference (`ethReferenceExecute`):** after `runTxPipeline`, if `isConsensusRejected(ctx.exitKind)`:

1. Set `output.consensusOutcome = Rejected`.
2. Force `output.stateDiff = {}` (no partial journal export).
3. Clear `output.topLevelIncludedTxVmError`.
4. Retain `ctx.evmcResult` **only** for debug/trace; TE must not use it to `makeReceipt`.

**OpStack normal (`runOpStackTxLifecycle`):** after ADR-025 abort path (`isNormalPreExecutionReject`), additionally set `output.consensusOutcome = Rejected`. `buyGas` / gas-pool acquire failures set `Rejected` before pipeline.

**OpStack deposit:** always `Executed` at lifecycle boundary (inclusion intentional); existing deposit settlement matrix unchanged.

**ErrorPolicy mapping unchanged:** `EthOrchestrationErrorPolicy::onIntrinsicGasFailure` may continue to populate `OutOfGasLimit` on `ctx.evmcResult` for trace/tests. Inclusion gating is **`consensusOutcome`**, not `evmcResult.status_code`.

### 3. TE executor contract

`EthTransactionExecutorImpl` and `OpStackTransactionExecutorImpl` gain a per-tx flag (e.g. `m_consensusRejected`) set when:

- `buyGas` fails, or
- orchestration returns `consensusOutcome == Rejected`.

**Execute phase on reject:**

1. **Do not** `applyStateDiff`.
2. **Do not** `settleGasUsedFromEvmResult` / `refundGas` / OpStack fee side effects beyond ADR-025 abort already performed in lifecycle.
3. **Do not** bump sender nonce on storage (see §4).

**Finalize phase on reject:**

```cpp
if (m_consensusRejected) {
    co_return nullptr;  // no TransactionReceipt
}
co_return co_await makeReceipt(...);
```

`co_return {}` from Execute mid-phase is **not** sufficient today — Finalize still calls `makeReceipt`. This ADR makes **`nullptr` receipt** the explicit TE contract for consensus reject.

### 4. Nonce and Prepare ordering

geth increments sender nonce **after** `preCheck` succeeds inside `execute()` (`state_transition.go` ~619–620).

Current TE **Prepare** calls `warmTransactionEntry`, which may touch account warmth but must **not** commit a nonce increment before consensus outcome is known.

| Rule | Requirement |
| --- | --- |
| Prepare | Warmth / access-list only; **no durable nonce bump** |
| Execute success | Nonce bump via `stateDiff` / ledger path same as today |
| Execute reject | Storage checkpoint for the tx must leave sender nonce **unchanged** |

If any path currently bumps nonce before Execute success, move the bump to post-`Executed` or roll back on `Rejected`.

### 5. Block scheduler / inclusion semantics

Align with geth **`ApplyTransaction` error → block processing fails for that tx** (validator rejects block containing invalid tx), not silent skip:

```cpp
// SchedulerSerialImpl stage 4 (conceptual)
auto receipt = co_await context.template executeStep<2>();
if (!receipt) {
    BOOST_THROW_EXCEPTION(ConsensusTxRejected{...});
}
receipts.emplace_back(std::move(receipt));
```

**RPC / client observable behavior:**

| Path | geth | Target after ADR-028 |
| --- | --- | --- |
| Block builder includes invalid tx | Block invalid / import error | Same — block execution error |
| `eth_getTransactionReceipt` | `null` for never-included tx | `null` |
| `eth_sendRawTransaction` | Pool validation error (overlapping preCheck rules) | Unchanged pool layer; out of ADR scope |

**Alternative considered — skip tx, continue block:** rejected because receipts root / tx count would diverge from geth unless the tx were never in the block’s tx list (which implies pool/precheck rejection upstream, not execution-time skip).

### 6. Relationship to prior ADRs

| ADR | Relationship |
| --- | --- |
| **ADR-015** | **Preserved.** `Completed` + `topLevelIncludedTxVmError` → `Executed`, receipt with failure semantics. |
| **ADR-019** | `TxPipelineExitKind` taxonomy unchanged; ADR-028 adds **consumer** `isConsensusRejected`. |
| **ADR-025** | **Settlement closed.** ADR-028 adds `consensusOutcome` on same abort paths; removes TE receipt on those paths. |
| **ADR-026** | Fee projection unchanged; reject path must not invoke post-settlement projection (already true post-025). |
| **ADR-023** | Deposit branch exempt from `Rejected`; characterization matrix row #2/#8 gain **inclusion** columns. |

### 7. FISCO production baseline (optional deviation)

FISCO non-reference executors may retain **legacy include-on-entry-failure** for product compatibility:

- Gate via `RevisionConfig` or executor profile flag (default **off** on ETH reference + OpStack TE).
- When flag **on**: TE may still `makeReceipt` with failed status (pre-ADR-028 behavior); flag must be documented in `capability-matrix.md`.

Default for **GST / EEST / op-geth parity tests:** flag **off** → `Rejected`.

---

## Implementation phases

| Phase | Deliverable | Primary files |
| --- | --- | --- |
| **A** | `TxConsensusOutcome` + `isConsensusRejected` | `eth/TxConsensusOutcome.h` |
| **B** | Bridge/lifecycle sets `consensusOutcome` | `ApplyReferenceMessage.cpp`, `OpStackTxLifecycle.cpp` |
| **C** | TE Execute/Finalize gate | `EthTransactionExecutorImpl.h`, `OpStackTransactionExecutorImpl.h` |
| **D** | Scheduler null-receipt → block error | `SchedulerSerialImpl.h`, parallel scheduler if applicable |
| **E** | Flip characterization oracles + E2E receipt count | `EthIntrinsicGasFailureCharacterizationTest.cpp`, `OpStackTxLifecycleCharacterizationTest.cpp`, TE fixtures |
| **F** | Docs | This ADR → **Accepted**; `architecture-known-gaps.md` close GAP-001/002 inclusion |

Recommended PR order: **A → B → C → E** (unit/char tests) → **D** (integration) → **F**.

---

## Characterization oracle updates

Extend ADR-023 / parity-test matrix with **inclusion** column:

| # | Scenario | State/fee (ADR-025) | **Inclusion (ADR-028)** |
| --- | --- | --- | --- |
| ETH-1 | Reference `IntrinsicRejected` | empty diff; no fee debit | **`consensusOutcome == Rejected`**; TE `Finalize → nullptr` |
| ETH-2 | TE `buyGas` insufficient | sender balance unchanged | **`Rejected`**; no receipt |
| OP-2 | Normal `IntrinsicRejected` | `gasUsed == 0`; no l1Fee meta | **`Rejected`**; no receipt |
| OP-8 | Normal `GasAffordRejected` | same as OP-2 | **`Rejected`**; no receipt |
| OP-3 | Normal `buyGas` fail | abort before pipeline | **`Rejected`**; no receipt |
| DEP-* | Deposit entry failure | `gasUsed = gasLimit` | **`Executed`**; receipt **still produced** |
| VM-1 | Top-level `INVALID` opcode | ADR-015 settlement | **`Executed`**; receipt **produced** |

Existing tests that assert `OutOfGasLimit` receipt on entry failure flip to **`EXPECT_EQ(receipt, nullptr)`** at TE boundary, or `consensusOutcome == Rejected` at lifecycle boundary.

---

## Consequences

### Positive

- Closes GAP-001/002 **inclusion** half; combined with ADR-025 yields end-to-end geth/op-geth parity for normal entry failure.
- Single `consensusOutcome` flag decouples trace-oriented `evmcResult` mapping from block inclusion policy.
- Explicit `nullptr` receipt contract simplifies scheduler and RPC null-receipt semantics.

### Trade-offs

- Block execution **fails** if a block contains a preCheck-invalid tx (stricter than silent skip). Matches geth validator behavior; block producers must filter via pool/precheck upstream.
- Scheduler + TE behavior change is breaking for any caller assuming every tx index has a receipt.
- FISCO legacy mode adds a configuration surface to avoid breaking existing deployments.

### Risks

- Missed nonce bump in Prepare → reject still mutates state (audit Prepare vs geth ordering during Phase C).
- Parallel scheduler must mirror serial null-receipt handling.
- `call == true` eth_call paths: reject should return error payload, not block inclusion semantics (TE already branches on `m_call`).

---

## Compliance

- [ ] `eth/TxConsensusOutcome.h` with `isConsensusRejected`
- [ ] `EthReferenceResult` / `OpStackExecutionResult` carry `consensusOutcome`
- [ ] `ethReferenceExecute` + `runOpStackTxLifecycleOwner` set `Rejected` on entry-failure paths
- [ ] TE Finalize returns `nullptr` when rejected; Execute skips apply/settle/refund
- [ ] Nonce unchanged on reject (characterization or state diff assert)
- [ ] Scheduler treats `nullptr` receipt as block execution error
- [ ] Characterization tests ETH-1/2, OP-2/3/8 flipped; deposit/vmerr rows unchanged
- [ ] `architecture-known-gaps.md` GAP-001/002 inclusion marked closed
- [ ] Optional FISCO legacy flag documented in capability matrix

---

## References

- geth `core/state_transition.go` — `preCheck`, `execute`, nonce increment ordering
- geth `core/state_processor.go` — `ApplyTransaction` err handling
- `EthOrchestrationErrorPolicy.h`, `EthTransactionExecutorImpl.h:169-199`
- `OpStackTxLifecycle.cpp`, `OpStackSettlement.cpp` `isNormalPreExecutionReject`
- `SchedulerSerialImpl.h:117-118`
- Parity review: `docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26-v2.md` § GAP-001, GAP-002
- Test prompt: `docs/superpowers/reviews/error-handling-geth-parity-test-prompt.md` Task 2, 6

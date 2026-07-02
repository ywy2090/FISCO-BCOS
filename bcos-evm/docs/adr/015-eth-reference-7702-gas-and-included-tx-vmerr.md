# ADR-015: ETH Reference — EIP-7702 Auth Intrinsic and Included-Tx vmerr Settlement

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** ADR-005 (orchestration boundaries), ADR-006 (7702 gating), `capability-matrix.md` rows 7702 precheck / 7623 settlement

---

## Context

EEST `test_self_sponsored_set_code` (Osaka) exposed three reference-path gaps vs geth `state_transition.go`:

1. **7702 auth intrinsic (25_000 / tuple)** was checked but not debited or included in EIP-7623 settlement → wrong `gasUsed`, sender balance, and `stateRoot` on success paths.
2. **Zero-value SSTORE to an empty slot** was persisted in the in-memory account map → extra storage trie entries vs geth (`SetState` no-op when `prev == value`).
3. **Top-level EVM vmerr** (`INVALID`, `REVERT`, `OUT_OF_GAS`, …) was surfaced as transaction rejection (`EVMC_UNDEFINED_INSTRUCTION`, etc.) even though geth returns `ExecutionResult{Err: vmerr}` with `TransitionDb err == nil` — 7702 authorizations applied before `Call` remain committed.

GST reference tests without `expectException` assert `EVMC_SUCCESS` at the adapter boundary (included transaction semantics).

---

## Decision

### 7702 auth intrinsic (orchestration)

On the ETH reference path (`ExecuteViaEth.cpp`, when `eip7623`):

- Debit `calcAuthTupleIntrinsicGas(n)` from `message.gas` before `executeMessage` (same gross charge as geth `IntrinsicGas` + `CallNewAccountGas` per tuple).
- Record `authIntrinsic` on `gasSettlementSnapshot` and add it to `finalizeEthereumGasUsed`.
- Apply existence refund (`PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST`) via normal `state.get_refund()` → `evmGasRefund` in settlement.

### Storage no-op (kernel)

`State::set_storage` returns early when `get_storage(key) == value` (geth `SetState` parity). GST `hashStorageTrie` / `buildPostStateView` omit zero-valued slots from the post-state trie.

### Included top-level vmerr (orchestration)

After preCheck succeeds and `executeMessage` runs at **depth 0**:

- **Do not** treat vm execution errors as transaction rejection for reference/GST parity.
- Normalize `evmc_result.status_code` to `EVMC_SUCCESS` for included-tx vmerr (exclude `INSUFFICIENT_BALANCE` and `INTERNAL_ERROR`).
- Set `ExecuteViaEthOutput::topLevelIncludedTxVmError` when normalization occurs.

**Gas settlement split (7623):**

| Path | Function | When |
| --- | --- | --- |
| Successful EVM | `finalizeEthereumGasUsed` | `status == SUCCESS` and not `topLevelIncludedTxVmError` |
| Included-tx vmerr | `settleIncludedTopLevelTransactionGas` | `topLevelIncludedTxVmError` — geth `peakGasUsed` + EIP-3529 refund cap model |

`executeMessage` captures `gasRefund` on the failure/revert path after checkpoint revert so auth existence refund is visible to settlement.

Nested calls (`depth > 0`) keep raw `evmc_status_code`.

---

## Consequences

- Matrix row **EIP-7702 precheck + intrinsic gas** on ETH (reference) moves from `unsupported` to `explicit`.
- EEST smoke covers all ten `self_sponsored_set_code` Osaka variants (114 subtests including warming + invalid auth signature expansion).
- TE baseline (BCOS/OPStack) unchanged by this ADR; OPStack already uses `postExecuteGasSettlement` for vmerr paths.
- Receipt-level failure signaling on production TE executors remains a separate product concern; this ADR governs **reference path + GST adapter** only.

**Update (2026-07-01 — product goal: full geth parity):** Receipt / RPC / `receiptsRoot` must match geth for included top-level vmerr (failed receipt status while state + gas settlement remain ADR-015 included semantics). See §Receipt parity extension below; audit [`2026-07-01-eth-vs-geth-parity.md`](../audits/2026-07-01-eth-vs-geth-parity.md) § ADR-015 / Deferred backlog.

## References

- geth `core/state_transition.go` — `IntrinsicGas`, `applyAuthorization`, `TransitionDb` vmerr handling
- `ExecuteViaEth.cpp`, `EthTxGasSettlement.h`, `ExecuteViaEthAdapter.cpp`
- EEST probes: `manifests/eth-eest-probe-{return,invalid,revert,oog}.json`

---

## Receipt parity extension (2026-07-01)

**Goal:** Full geth parity on **included** top-level vmerr — same as today for **state root + gasUsed**, plus **receipt status / receiptsRoot / `eth_getTransactionReceipt.status`** aligned with geth.

**Problem:** `normalizeIncludedTxVmerr` currently sets both `status_code = EVMC_SUCCESS` **and** `status = TransactionStatus::None`. TE `makeReceipt` writes `None` → `ReceiptResponse` maps `status==0` to RPC **success=1**, diverging from geth **status=0 (failed)** for INVALID / OOG / etc.

**Decision (extends §Included top-level vmerr, does not revert settlement):**

| Field | Settlement / applyStateDiff / refund | Receipt / RPC |
| --- | --- | --- |
| `evmc_result.status_code` | Normalize to `EVMC_SUCCESS` when `topLevelIncludedTxVmError` | N/A (receipt uses `EVMCResult.status`) |
| `EVMCResult.status` | **Preserve** pre-normalize mapping (`BadInstruction`, `OutOfGas`, …) | Written to receipt → RPC failure |
| `topLevelIncludedTxVmError` | Drives `settleIncludedTopLevelTransactionGas` | Unchanged |

**Implementation sketch:**

1. `IncludedTxVmerrNormalize.h` — normalize **`status_code` only**; if `status == Unknown`, backfill via `evmcStatusToTransactionStatus(original_code)` before overwrite.
2. `normalizeSetCodeTransactionVmerr` — **geth-oracle** EEST row for 7702 REVERT receipt bit (state root already passes; receipt may stay `RevertInstruction` if geth shows failed).
3. `EthTxFeeSettlement::makeReceipt` / TE path — no special case if `status` already correct.
4. Tests — extend `EthIncludedTxVmerrTest` + TE E2E: assert `receipt.status() != None` and RPC-equivalent failure for INVALID/OOG; keep `topLevelIncludedTxVmError` + peak gas assertions.
5. **Do not** change nested-frame or `INSUFFICIENT_BALANCE` / `INTERNAL_ERROR` exclusions.

**Related full-parity work (separate ADRs):** ADR-028 entry-failure **reject** (no receipt) — Gap 39; London–Cancun historical fork rows (audit 3.1 / 9.3 / 11.1).

---

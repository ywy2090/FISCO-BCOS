# ADR-009: OPStack EIP-4844 — Orchestration-Layer Test Sufficiency (Wave 2)

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** ADR-005, ADR-008, Wave 2 design, FIX-08 / FIX-09

---

## Context

Audit D7-1 flagged missing EIP-4844 **full executor (TE) E2E** for blob transactions. `BlobGasBalanceTest` already covers blob `buyGas` deduction, preCheck rejection, and `opStackExecuteViaHost` success paths at the **orchestration** layer (`OpStackPreCheck`, `OpStackTxExecutor`, `OpStackExecuteViaHost`).

Wave 2 must close D7-1 without expanding into TE builder/RPC work unrelated to Isthmus TE baseline sign-off.

---

## Decision

For **OPStack Isthmus TE baseline sign-off (Wave 2)**:

1. **FIX-08 is cancelled** — no requirement for `OpStackTransactionExecutorImpl::executeTransaction` blob type-0x03 TE E2E.
2. **D7-1 closure evidence** = existing + augmented **orchestration-layer** tests:
   - `bcos-evm/test/opstack/BlobGasBalanceTest.cpp`
   - **FIX-09:** full `OpStackPreCheck` shape coverage (Malformed paths: hashes without cap, non-4844 tx with hashes, etc.) — **required** for sign-off (grill 2026-06-21, option C)
3. **Out of scope for Wave 2:** signed type-0x03 RLP through full TE pipeline; `OpStackTxInputBuilder` changes solely for blob TE E2E.

---

## Consequences

- Implementation plan Task 8 (FIX-08) removed; workload reduced.
- Audit Part 2 D7-1 marked CLOSED on orchestration evidence, not TE blob tx E2E.
- Future full-node / RPC blob tx parity remains a separate track if needed.

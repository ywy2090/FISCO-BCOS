# ADR-011: L1 Attributes Deposit — TE Executor E2E Required (Wave 2)

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** FIX-06, D5-4, `L1AttributesDepositTest`, `isthmus_l1_attributes.bin`

---

## Context

Audit D5-4 flagged missing `depositNonce` / nonce assertions on L1 attributes system deposit. `L1AttributesDepositTest` exercises `opStackExecuteViaHost` with fixture `isthmus_l1_attributes.bin` and asserts `OpStackReceiptMeta.depositNonce` plus depositor nonce bump.

Wave 2 must close D5-4 with evidence at the **TE baseline** layer, not only direct orchestration calls.

---

## Decision

For **Wave 2 sign-off (FIX-06 / D5-4)**:

1. **Required:** `OpStackTransactionExecutorImpl::executeTransaction` E2E using **`isthmus_l1_attributes.bin`** (or equivalent TE-wired system deposit tx) through the full executor pipeline.
2. **Assertions on TE receipt:** `receipt->depositNonce()` matches expected value; depositor account nonce incremented consistently with meta.
3. **Existing `L1AttributesDepositTest` (executeViaHost)** remains valuable orchestration evidence but **does not alone** close D5-4 for TE baseline ✅.
4. **Out of scope:** production node / JSON-RPC deposit receipt exposure (same boundary as ADR-008).

---

## Consequences

- Implementation plan Task 6 expands: new or extended case in `TestOpStackTransactionExecutorFixture` (or dedicated test) loading `isthmus_l1_attributes.bin` via TE.
- May require deposit tx / system tx wiring in test harness if not already available for attributes shape.
- Audit D5-4 closure cites TE fixture + existing orchestration test.

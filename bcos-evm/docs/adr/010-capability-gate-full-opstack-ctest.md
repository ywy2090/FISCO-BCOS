# ADR-010: capability-gate — Full Build + OpStack CTest on Trigger

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** FIX-02, `inheritance-work-tracker` #6, `.github/workflows/capability-gate.yml`

---

## Context

Wave 2 audit放行条件 R2 requires CI coverage for opstack + `TestOpStackTransactionExecutorFixture`. `capability-gate.yml` exists with matrix lint only; tracker #6 is `[~]` until workflow merges with ctest.

Options ranged from full build on trigger vs path-filtered jobs vs documentation-only closure.

---

## Decision

For **Wave 2 sign-off (FIX-02)**:

1. **Full CI on workflow trigger** — when `capability-gate` runs (existing PR `paths`), execute:
   - `cmake` configure + build (Debug)
   - Build opstack-related test targets including `test-opstack-transaction-executor-fixture` and `bcos-evm/test` binaries
   - Run both ctest suites:
     - `ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes'` on `build/bcos-evm/test`
     - `ctest -R OpStackTransactionExecutorFixture` on `build/transaction-executor/tests`
   - `bash bcos-evm/tools/ci/check-capability-matrix.sh` (existing lint)
2. **No path-split deferral** — ctest is not optional on a triggered run; lint + build + ctest are one gate.
3. **Tracker #6** closes when this workflow is on the main integration branch and green.

---

## Consequences

- PRs touching gated paths pay full build + test cost (acceptable for correctness).
- Implementation plan Task 2 follows single `opstack-ctest` job with full configure/build, not lint-only.
- Future optimization (caching, narrower targets) is out of scope for Wave 2 unless CI becomes blocking.

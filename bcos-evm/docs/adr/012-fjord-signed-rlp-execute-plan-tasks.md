# ADR-012: Fjord / Signed RLP Tests — Execute Plan Tasks (No Verify-Only Shortcuts)

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** FIX-04, FIX-05, `OpStackFeeTest`, `TestOpStackTransactionExecutorFixture`

---

## Context

At plan authoring, FIX-04 (`FjordL1_SolidityParity_matchesOpGeth105484`) and FIX-05 (`signed_rlp_rollup_execute_e2e`) appeared partially or fully implemented on HEAD. A verify-only path would close audit rows without touching tests.

---

## Decision

For **Wave 2 (FIX-04 / FIX-05)**:

1. **Do not** close R3/R4/D2-1/D2-2 on verify-only evidence.
2. **Execute implementation plan Tasks 4 and 5 as written** — including any additional vectors, TE assertions, comments pinning op-geth, and task2 audit note updates.
3. Existing passing tests may be **extended, refactored, or supplemented**; duplicate coverage is acceptable if Tasks require explicit deliverables not present in current cases.
4. Production code changes remain out of scope unless a Task fails and reveals a real bug.

---

## Consequences

- Wave 2 workload for FIX-04/05 is not skipped even if ctest already green.
- Sign-off evidence includes deliberate Task 4/5 completion checklist, not merely HEAD baseline snapshot.

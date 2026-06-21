# ADR-013: Wave 2 — 7702 Fixture Labeling and Existence Refund Test

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** FIX-10, FIX-11, `stEIP7702_delegation.json`, `OpStack7702ExecuteViaHostPropagationTest`

---

## Context

Audit T5 flagged `stEIP7702_delegation.json` as false coverage (hand-crafted, not ethereum/tests 7702 suite). Audit T6 flagged missing dedicated test for EIP-7702 existence refund when authority already has code.

Intrinsic 25000-per-tuple tests exist in `OpStack7702ExecuteViaHostPropagationTest`; existence refund is a separate gas accounting path.

---

## Decision

### FIX-10 (T5)

1. **Documentation-only closure** — update fixture `source` field and `test-inventory-opstack.md` to clearly label **hand-crafted smoke**, not state-test / inherited 7702 compliance evidence.
2. **Do not** rename file, remove from fixture pipeline, or break existing smoke references in Wave 2.

### FIX-11 (T6)

1. **Required:** new dedicated test case — authority account pre-seeded with code; authorization apply must assert **existence refund** gas behavior (symmetric to intrinsic charging tests).
2. **Not sufficient:** audit note CLOSED on intrinsic 25000 cases alone.

---

## Consequences

- Task 10 shrinks to doc + JSON `source` comment; no file rename (FIX-10 A).
- Task 11 remains full implementation in `OpStack7702ExecuteViaHostPropagationTest.cpp` (or sibling).
- Inherited fixture matrix should not cite `stEIP7702_delegation` as 7702 EIP compliance proof.

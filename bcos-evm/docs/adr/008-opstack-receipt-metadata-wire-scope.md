# ADR-008: OPStack Receipt Metadata — TE In-Memory Scope (Wave 2)

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** ADR-005, Wave 2 design `docs/superpowers/specs/2026-06-21-opstack-isthmus-wave2-design.md`, FIX-01

---

## Context

Isthmus remediation adds `operatorFeeScalar` and `operatorFeeConstant` to the execution path (`OpStackReceiptMeta` → `makeReceipt`). Existing OP fields (`l1Fee`, `operatorFee`, `depositNonce`) use **sidecar** optional strings on `TransactionReceiptImpl`, not tars `TransactionReceipt` hash payload.

Wave 2 audit sign-off (⚠️→✅) must fix D3-2 without expanding into RPC or persistence work.

---

## Decision

For **OPStack Isthmus TE baseline sign-off (Wave 2)**:

1. **`operatorFeeScalar` / `operatorFeeConstant` MUST** be readable on in-memory `bcos::protocol::TransactionReceipt` after `OpStackTransactionExecutorImpl::makeReceipt` (TE fixture asserts).
2. **Out of scope for Wave 2:** JSON-RPC / Web3 receipt JSON exposure; tars-encoded receipt on wire or in block storage.
3. Implementation follows the existing **sidecar** pattern (`m_l1Fee`, `m_operatorFee`, `m_depositNonce` on `TransactionReceiptImpl`). **Do not** add fields to `TransactionReceipt.tars` or receipt hash calculation for this wave.
4. Future RPC or sync exposure is a separate epic; not required for Isthmus TE baseline ✅.

---

## Consequences

- FIX-01: framework interface + `TransactionReceiptImpl` sidecar + `makeReceipt` + fixture tests — **no** tars codegen, **no** RPC layer.
- Audit R1/D3-2 closure evidence = `TestOpStackTransactionExecutorFixture` (and orchestration tests on `OpStackReceiptMeta` where applicable).
- Reviewers should not block Wave 2 on missing `eth_getTransactionReceipt` fields.

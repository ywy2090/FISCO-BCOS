# ADR-003: Sub-Capability Row Granularity for Multi-Layer EIPs

**Status:** Accepted  
**Date:** 2026-06-20  
**Deciders:** bcos-evm architecture (inheritance contract)  
**Related:** `bcos-evm/capability-matrix.md`, ADR-001, ADR-002, `docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md`

---

## Context

EIPs rarely map to a single matrix cell. EIP-7702 alone spans kernel application, tx field propagation, revision profile enablement, and orchestration precheck/intrinsic gas. EIP-2929 spans runtime kernel behavior and distinct tx-entry warm inputs. EIP-7623 spans entry precheck and settlement.

ADR-002 defines status tokens and kernel-capable vs baseline-reachable semantics but does not specify:

1. **How many rows** an EIP must have in the matrix.
2. **When to split** vs merge sub-capabilities.
3. **How to name** rows so reviewers can grep by EIP ID.
4. **Whether rollup/summary rows** are allowed alongside detail rows.
5. **How to record chain-only concerns** (e.g. BCOS 21000 gas debit) that are not part of an Ethereum EIP number.

Inconsistent granularity caused audit errors: single-row “EIP-7702 inherited” overclaims; over-splitting every opcode flag would make the matrix unmaintainable.

---

## Decision

### 1. Granularity principle

> **One matrix row = one sub-capability on one layer that can be tested, gated, or opted out independently.**

A sub-capability is independently testable when a reviewer can point to a distinct code path (kernel, tx decoder, profile builder, orchestrator precheck/settlement, or host hook) and assign a status per chain without contradicting other rows.

### 2. Mandatory row splits (multi-layer EIPs)

When onboarding or auditing an EIP, add **at least one row per layer that applies**. Use these layers (ADR-002):

| Layer | Add a row when… |
| --- | --- |
| `kernel` | Behavior is implemented in `executeMessage` and below |
| `tx input` | Distinct tx fields or `TransactionProperties` / access-list wiring are required |
| `revision profile` | A `RevisionConfig` field (or revision threshold) gates kernel or orchestration behavior |
| `orchestration` | Precheck, intrinsic gas, settlement, nonce, balance, or receipt logic lives above `executeMessage` |
| `host extension` | Behavior depends on `HostExtension` hooks |

**Minimum for a typical Ethereum hardfork EIP:** evaluate all five layers; omit a layer only when that layer genuinely does not apply, with a one-line rationale in the PR (not a matrix row).

### 3. Required splits inside common patterns

| Pattern | Split rule |
| --- | --- |
| **Kernel + profile gate** | Always separate `kernel` row from `revision profile` row when `RevisionConfig` or feature flags gate runtime (e.g. EIP-7702 apply vs `eip7702` enable). |
| **Kernel + tx fields** | Always separate `kernel` from `tx input` when fields pass through orchestrators (e.g. authorization list). |
| **Orchestration precheck vs settlement** | Split when implemented in different functions or gated differently (e.g. EIP-7623 entry precheck vs floor settlement). |
| **Distinct tx-entry effects** | Split when inputs or defaults differ (e.g. EIP-2929 destination warm vs coinbase warm). |
| **Precompile address range** | Split when TE kernel support differs within one EIP family (e.g. EIP-2537 `0x0b–0x11` vs EIP-7212 `0x0100` — separate rows). |
| **Chain-only orchestration** | Separate row named after the concern, not the EIP (e.g. `BCOS fixed 21000 gas debit`); other chains use `unsupported` with reason. |

### 4. When not to split further

Do **not** add rows for:

- Individual opcodes inside evmone revision (covered by `revision` + kernel runtime).
- Every `RevisionConfig` bit that is **profile-only** with no kernel consumer — one `revision profile` row per field (ADR-004 Accepted).
- Duplicate status across three chain columns when the **same layer and same code path** applies — one row, three columns.
- “Summary” or “rollup” rows that only restate other rows — **forbidden** in the matrix (derive end-to-end status at review time; see §6).

### 5. Capability column naming

Use this pattern:

```text
<EIP-ID or concern> <short sub-capability name>
```

Examples (normative):

| Good | Bad |
| --- | --- |
| `EIP-7702 authorization apply` | `EIP-7702` |
| `EIP-7702 tx field propagation` | `7702 wiring` |
| `EIP-2929 tx-entry destination warm` | `EIP-2929 warm access` (too broad) |
| `EIP-7623 settlement / floor gas` | `EIP-7623` |
| `BCOS fixed 21000 gas debit` | `gas` |

Rules:

- Start with `EIP-NNNN` when tied to an Ethereum EIP; use a **concern prefix** for chain-only rows (`BCOS …`, `OPStack …`).
- Sub-capability name matches the **layer** (apply, tx field propagation, revision enable, entry precheck, settlement, runtime warm).
- Keep names stable; rename only with matrix + ADR note in PR.

### 6. End-to-end EIP support (no rollup rows)

The matrix **does not** include rollup/summary rows. Derive end-to-end support at review time:

**Baseline-reachable on chain C** for EIP **E** when **all** of the following hold for every mandatory row of **E** on chain C:

| Row layer | Allowed statuses for “supported” |
| --- | --- |
| `kernel` | `inherited` (must be baseline-reachable, not merely kernel-capable — footnote if gate exists) |
| `tx input` | `inherited` |
| `revision profile` | `inherited` |
| `orchestration` | `inherited`, `explicit`, or `deviation` (documented and tested per ADR-002) |
| `host extension` | `inherited` or `deviation` |

If any mandatory row is `feature-gated` or `unsupported`, end-to-end EIP **E** is **not** baseline-reachable on C until that row is resolved or permanently documented as out of scope.

**Kernel-only “implemented in eth”** = kernel row `inherited` (kernel-capable) on at least one path; say nothing about BCOS/OP production.

### 7. Layer ordering in `capability-matrix.md`

Within the matrix file, group rows **by EIP or concern**, in this order:

1. `kernel`
2. `tx input`
3. `revision profile` (or `revision profile + orchestration` if combined)
4. `orchestration` (precheck before settlement when both exist)
5. `host extension`

Chain-only rows may appear immediately after the EIP block they relate to, or in a “Chain deviations” subsection during Phase 1 audit.

### 8. Combined layers (exception)

A single row may use layer `revision profile + orchestration` **only** when the same gate and the same orchestration entry point control both (e.g. EIP-4844 blob gas where profile flag and precheck are inseparable in review). Prefer splitting when precheck and profile are maintained in different files or phases.

### 9. Phase 1 audit checklist per EIP

For each EIP in scope:

1. List §4 capability classes from the design doc.
2. Add mandatory rows per §2–§3.
3. Fill three chain columns + Test ref per ADR-002.
4. Confirm no forbidden rollup row was added.
5. If kernel row is `inherited` but profile/tx block baseline reachability, footnote **kernel-capable; not baseline-reachable**.

---

## Normative examples (from current matrix)

### EIP-7702 — five rows

| Capability | Layer |
| --- | --- |
| EIP-7702 authorization apply | kernel |
| EIP-7702 tx field propagation | tx input |
| EIP-7702 revision enable | revision profile |
| EIP-7702 precheck + intrinsic gas | orchestration |

End-to-end on OPStack Isthmus: kernel + tx + profile reachable; orchestration `explicit` (acceptable). End-to-end on BCOS: blocked by tx + profile rows.

### EIP-2929 — three rows

| Capability | Layer |
| --- | --- |
| EIP-2929 runtime warm | kernel |
| EIP-2929 tx-entry destination warm | tx input |
| EIP-2929 tx-entry coinbase warm | tx input |

Do not merge into one “EIP-2929 warm access” row.

### EIP-7623 — two orchestration rows + shared helpers

| Capability | Layer |
| --- | --- |
| EIP-7623 entry precheck | orchestration |
| EIP-7623 settlement / floor gas | orchestration |

Shared helpers in `eth/gas/` do not get separate rows unless gated independently.

### EIP-2537 vs EIP-7212 — two kernel rows

Same fork family, different TE kernel support → **two rows**, not one “Prague precompiles” row.

### OPStack deposit — one orchestration row

OP-only EIP semantics → one `orchestration` row; ETH/BCOS columns `unsupported`.

---

## Consequences

### Positive

- Matrix width stays bounded; depth scales with EIP complexity, not with chain count.
- Reviewers can block PRs that collapse multi-layer EIPs into a single misleading row.
- Phase 1 gap list can require “one row per RevisionConfig field” without conflating with EIP rows.

### Negative

- More rows to maintain; EIP-7702 adds four lines minimum.
- End-to-end status requires mental aggregation (no summary row) — mitigated by PR template question: “Is EIP X baseline-reachable on BCOS/OP?”

### Follow-ups

| Item | Owner |
| --- | --- |
| ADR-004: profile-only `RevisionConfig` fields row policy | Done (Accepted) |
| Optional PR template checkbox for end-to-end derivation | Phase 1 |
| CI: warn if Capability cell matches only `EIP-\d+` with no sub-name | Phase 1 lint (optional) |

---

## Compliance checklist (for PR reviewers)

- [ ] Multi-layer EIP has separate rows for kernel / tx input / profile / orchestration where applicable.
- [ ] No rollup-only row; end-to-end support argued in PR text if claimed.
- [ ] Capability names follow `<EIP-NNNN> <sub-capability>` or chain concern prefix.
- [ ] Precompile/support gaps split by address range when TE kernel differs (2537 vs 7212).
- [ ] Orchestration precheck and settlement split when gates or code paths differ.
- [ ] Kernel `inherited` footnotes **kernel-capable / not baseline-reachable** when other rows block TE path.

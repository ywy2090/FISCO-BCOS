# ADR-004: RevisionConfig Field Consumption vs Profile-Only Flags

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** ADR-002, ADR-003, `bcos-evm/capability-matrix.md`, design doc §5.3

---

## Context

`RevisionConfig` mixes fields that the TE kernel reads at runtime with fields that profile builders set but nothing in `executeMessage` / `EthHost` / orchestration consumes yet. Silent `false` defaults and profile-only flags created false confidence during Phase 3 planning.

---

## Decision

### 1. Three field categories

| Category | Definition | Matrix treatment |
| --- | --- | --- |
| **Consumed** | Read by kernel or orchestration on the TE path | One `revision profile` and/or `orchestration` row; status reflects baseline reachability |
| **Profile-only** | Set (or not) in policy builders; **no TE consumer today** | One `revision profile` row marked `unsupported` or `feature-gated` with reason **profile-only; no runtime consumer** |
| **Deprecated / reserved** | Kept for future wiring or FISCO overlay documentation | Same as profile-only; note target consumer in footnote |

### 2. Consumption table (normative for Phase 1 matrix rows)

| Field | Category | Consumer |
| --- | --- | --- |
| `revision` | consumed | evmone, `EthPrecompiles`, `warmTransactionEntry` (`cfg.eip3651`, precompile warm via `revision`) |
| `eip7702` | consumed | `executeMessage` authorization apply gate |
| `eip7623` | consumed | orchestration precheck/settlement + `calldata_floor_per_token` |
| `calldata_floor_per_token` | consumed | EIP-7623 helpers when `eip7623` |
| `eip1153`, `eip5656`, `eip6780` | consumed | evmone via `revision`; `eip6780` also read in `EthHost::selfdestruct` via `cfg.eip6780` (ADR-018) |
| `eip4844` | consumed | OPStack blob precheck (orchestration); evmone via revision |
| `eip2537`, `eip7212` | consumed | `PrecompileActive.h` reads `cfg.eip2537` / `cfg.eip7212`; FISCO `PrecompiledManager` also feature-gated |
| `warm_access` | profile-only | Runtime uses `rev >= BERLIN` for evmone; FISCO masks via `applyFiscoFeatureGates` |
| `eip3651` | consumed | `warmTransactionEntry` coinbase warm gate (`cfg.eip3651`; ADR-018) |
| `eip1559` | profile-only | Assigned by `revisionConfigFromRevision` (ADR-018); typed-tx gate in `Web3TypedTxKind.h` reads `cfg.eip1559` |
| `prague_post_execution` | deprecated / reserved | Struct default `false`; no TE consumer; profile snapshots only (`RevisionConfigProfileTest`). Future-removal candidate. |
| `eip7823` | profile-only until wired | Policy sets at OSAKA; verify consumer before marking consumed |

### 3. Profile builder contract (Phase 3)

For **every** `RevisionConfig` field, each builder (`EthPolicy`, `FiscoPolicy`, `makeIsthmusRevisionConfig`) must either:

1. **Assign explicitly** (`true`/`false`/derived), or  
2. Document in matrix as **intentionally default-off** with `unsupported` / `feature-gated`.

`RevisionConfigProfileTest` snapshots assignments per fork; it does **not** prove runtime behavior for profile-only fields.

### 4. Adding a new field

1. Classify consumed vs profile-only before merge.  
2. If consumed: kernel/orchestration implementation + matrix rows + tests.  
3. If profile-only: matrix row with **profile-only** reason; no `inherited` on kernel rows.  
4. Update this ADR consumption table in the same PR.

---

## Consequences

- Phase 1 matrix includes one row per `RevisionConfig` field without implying kernel inheritance for profile-only bits.
- `warm_access` must not be used as the runtime gate for EIP-2929 in new code; use `revision` or document migration in a future ADR.

---

## Compliance checklist

- [ ] New `RevisionConfig` field classified in PR description.
- [ ] Consumption table updated if category or consumer changes.
- [ ] Matrix row added/updated.
- [ ] `RevisionConfigProfileTest` extended for profile assignments.

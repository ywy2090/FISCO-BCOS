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
| `revision` | consumed | evmone, `PrecompileActive.h` / `forEachActivePrecompile` |
| `eip7702` | consumed | `executeMessage` authorization apply gate |
| `eip7623` | consumed | orchestration precheck/settlement + `calldata_floor_per_token` |
| `calldata_floor_per_token` | consumed | EIP-7623 helpers when `eip7623` |
| `eip1153`, `eip5656`, `eip6780` | consumed | evmone via `revision`; `eip6780` also read in `EthHost::selfdestruct` via `cfg.eip6780` (ADR-018) |
| `eip4844` | consumed | OPStack blob precheck (orchestration); evmone via revision |
| `eip2537`, `eip7212` | consumed | `PrecompileActive.h` reads `cfg.eip2537` / `cfg.eip7212`; FISCO `PrecompiledManager` also feature-gated |
| `eip2929` | consumed | `Eip2929Gate.h` → `WarmTransactionEntry`, `EthHost::access_*`, CREATE warm pin, 7702 delegation warm. **Scheme A:** FISCO may mask via `feature_evm_eip2929` while `revision` stays high — intentional **deviation** from geth (Host reports COLD; not pre-Berlin revision). Read only through `isEip2929Enabled()` in `eth/` production code. |
| `eip3651` | consumed | `warmTransactionEntry` coinbase warm gate (`isCoinbaseWarmEnabled`; ADR-018) |
| `eip1559` | consumed | `Eip1559Access.h` → typed-tx gate (`Web3TypedTxKind`), fee-cap precheck (`EthTxPrecheck` / `OpStackPrecheckPolicy`), `normalizeGasCaps`, OpStack gas refund |
| `eip7823` | profile-only until wired | Policy sets at OSAKA; verify consumer before marking consumed |

### 3. Profile builder contract (Phase 3)

For **every** `RevisionConfig` field, each builder (`EthChainPolicy`, `FiscoPolicy`, `makeIsthmusRevisionConfig`) must either:

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
- **`eip2929` (Scheme A):** TE EIP-2929 warm/cold tracking is gated by `eip2929` via `Eip2929Gate.h`. Do not read `cfg.eip2929` elsewhere in `eth/` production code. FISCO `feature_evm_eip2929=OFF` with high `revision` is a documented deviation (matrix `deviation` on BCOS 2929 rows); evmone still runs at `revision`, Host disables warm tracking.

---

## Compliance checklist

- [ ] New `RevisionConfig` field classified in PR description.
- [ ] Consumption table updated if category or consumer changes.
- [ ] Matrix row added/updated.
- [ ] `RevisionConfigProfileTest` extended for profile assignments.

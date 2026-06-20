# ADR-002: Capability Matrix Status Tokens and Class Mapping

**Status:** Accepted  
**Date:** 2026-06-20  
**Deciders:** bcos-evm architecture (inheritance contract)  
**Related:** `docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md`, `bcos-evm/capability-matrix.md`, ADR-001, ADR-003

---

## Context

The capability matrix (`bcos-evm/capability-matrix.md`) records, per chain path, whether each EIP sub-capability is shared from the ETH kernel or handled explicitly. Review found:

1. **Overloaded “inherited”** — kernel rows marked `inherited` were read as end-to-end EIP support even when revision profile or tx-input rows blocked baseline reachability (e.g. EIP-7702 on BCOS).
2. **Invalid tokens** — cells used `n/a`, `supported/wired`, or free-text statuses outside a closed set.
3. **Class vs status confusion** — design doc §4 “capability classes” (kernel, tx input, orchestration) were not mapped to matrix **Layer** and **status** columns.
4. **Implicit defaults** — `TransactionProperties` defaults (`warmDestination{true}`, `warmCoinbase{true}`) were documented as “unset” / `unsupported`, contradicting runtime behavior.

This ADR freezes the token vocabulary and mapping rules used in every matrix cell.

---

## Decision

### 1. Closed status token set

Every matrix cell (except **Test ref**) must use **exactly one** of these tokens, optionally followed by a short parenthetical reason:

| Token | When to use |
| --- | --- |
| **`inherited`** | **Kernel rows:** kernel-capable on that path (direct `executeMessage` test can trigger behavior with supplied inputs). **Non-kernel rows:** baseline-reachable on that path with default profile + normal tx decoding, without chain-specific reimplementation beyond documented hooks. |
| **`explicit`** | Chain-specific orchestration code implements the behavior (precheck, intrinsic gas, settlement, receipt routing) in the extension orchestrator. |
| **`feature-gated`** | Blocked by an unset `RevisionConfig` field and/or a FISCO `Features::Flag` until profile Phase 3 enables it or matrix records permanent opt-out. |
| **`unsupported`** | Intentionally not wired, not applicable on this path, or default-off behavior is expected. |
| **`deviation`** | Intentionally different from Ethereum reference behavior; **must** have a positive deviation test in **Test ref**. |

**Forbidden in matrix cells:** `n/a`, `supported`, `wired`, `partial`, `TBD`, or any sixth token without a new ADR.

### 2. Kernel-capable vs baseline-reachable

These terms apply inside **`inherited`** semantics:

| Term | Definition |
| --- | --- |
| **kernel-capable** | Logic in `executeMessage` and below; provable via kernel contract test with manual `RevisionConfig` + tx inputs. |
| **baseline-reachable** | On the path in ADR-001, default profile + normal decoding **observably trigger** the capability without chain-specific reimplementation. |

**Rule:** Kernel-layer rows may be `inherited` (kernel-capable) even when the same EIP is **not** baseline-reachable on that chain. The footnote must say so when relevant (see EIP-7702 apply row in the matrix).

**End-to-end EIP support** requires all relevant rows for that EIP (kernel, tx input, revision profile, orchestration) to be `inherited` or an accepted combination of `explicit` / `feature-gated` / `deviation` documented in the matrix — not merely a green kernel row.

### 3. Implicit-default footnotes (not a sixth token)

When behavior is effective because a struct default applies and the orchestrator does not assign the field:

- Cell status remains **`inherited`** (if behavior matches the contract).
- Parenthetical **must** cite the default source, e.g. `implicit-default: TransactionProperties::warmDestination{true}`.

Do **not** mark implicit-default behavior as `unsupported`.

### 4. Capability class → matrix layer → typical status

When classifying a new EIP (design doc §4), use this mapping:

| §4 Capability class | Matrix `Layer` | Typical statuses |
| --- | --- | --- |
| Kernel capability | `kernel` | `inherited` or `unsupported` |
| Tx input capability | `tx input` | `inherited` (+ implicit-default footnote if applicable) or `unsupported` |
| Tx orchestration capability | `orchestration` | `explicit`, `feature-gated`, `unsupported`, or `deviation` |
| Host extension capability | `host extension` | `inherited` (no chain hook), `deviation`, or `explicit` |
| Chain deviation | `orchestration` or `host extension` | `deviation` (positive test required) |

Additional layer values allowed when needed: `revision profile`, `revision profile + orchestration` (combined row only when tightly coupled).

### 5. Choosing `feature-gated` vs `deviation` vs `explicit`

| Situation | Token |
| --- | --- |
| Behavior will match Ethereum once profile/flag is enabled | `feature-gated` |
| Behavior intentionally differs from Ethereum (OP floor gas, BCOS 21000 debit) | `deviation` |
| Chain implements required orchestration that Ethereum also has, but in chain-specific code | `explicit` |
| Capability not planned on this path | `unsupported` |

Examples (normative references in matrix):

- EIP-7623 settlement on OPStack: **`deviation`** (OPStackFloorGas path).
- EIP-7623 settlement on ETH reference: **`explicit`** (shared helper, ETH orchestrator-owned).
- BCOS `BALANCE_TRANSFER_GAS` debit: **`deviation`** on BCOS column, **`unsupported`** on ETH/OP columns (not applicable).

### 6. Test ref column rules

| Matrix status | Test ref requirement |
| --- | --- |
| `inherited` (baseline-reachable, non-kernel row) | TE baseline-path or propagation test on that chain |
| `inherited` (kernel row) | Kernel contract test (e.g. direct `executeMessage`) |
| `deviation` | Positive deviation test **required** |
| `feature-gated` / `unsupported` | Optional; `-` or review link acceptable until Phase 3 |

**Rule (ADR-001):** ETH reference tests alone do **not** satisfy BCOS/OPStack baseline-reachable `inherited` rows.

### 7. CTest naming convention (Phase 2 default)

Until superseded by ADR or Open Decision #4:

- Kernel: `*ApplyAuthorization*`, `*WarmTransaction*`, direct `executeMessage` fixtures.
- BCOS baseline: tests calling `executeViaHost` or `TransactionExecutorImpl` harness.
- OPStack baseline: tests calling `opStackExecuteViaHost` or `OpStackTransactionExecutorImpl` harness.
- Matrix **Test ref** column lists primary CTest target or fixture path.

---

## Examples (normative patterns)

### EIP-7702 (multi-row)

| Row | BCOS typical | Why |
| --- | --- | --- |
| authorization apply (kernel) | `inherited` (kernel-capable; not baseline-reachable until profile + tx rows) | Kernel has `applyAuthorizations`; profile + fields gate TE path |
| tx field propagation | `feature-gated` | Fields wired via `FiscoTxInputBuilder`; requires `feature_evm_prague` + Web3 `0x04` (ADR-006) |
| revision enable | `feature-gated` | `FiscoPolicy` sets `eip7702` when `feature_evm_prague` + PRAGUE |

Do not mark BCOS kernel apply as baseline-reachable `inherited` without enabling profile and tx fields.

### EIP-2929 tx-entry (explicit on TE paths)

| Path | Destination warm | Token pattern |
| --- | --- | --- |
| OPStack TE | `applyDefaultTxProps` → `setWarmDestinationFromKind` | `inherited (explicit: …)` |
| ETH/BCOS | `setWarmDestinationFromKind` in orchestrator | `inherited (explicit in …)` |

---

## Consequences

### Positive

- Reviewers can reject matrix PRs that use forbidden tokens or conflate kernel-capable with end-to-end support.
- Phase 1 audit can extend the matrix row-by-row with consistent semantics.
- CI lint (future) can validate token vocabulary with a simple allow-list regex.

### Negative

- More rows per EIP (kernel / tx / profile / orchestration); see ADR-003 for granularity guidance.
- `inherited` requires reading footnotes for kernel rows — intentional trade-off for precision.

### Required follow-ups

| Item | Phase |
| --- | --- |
| ADR-003: sub-capability row granularity and rollup rules | Phase 1 — **Accepted** (`bcos-evm/docs/adr/003-sub-capability-row-granularity.md`) |
| `RevisionConfigProfileTest` aligned with `feature-gated` rows | Phase 3 |
| Optional markdown/CI lint: cell matches `^(inherited\|explicit\|feature-gated\|unsupported\|deviation)` | Phase 1 |

---

## Compliance checklist (for PR reviewers)

- [ ] Every matrix cell uses one of the five tokens (+ optional parenthetical).
- [ ] Kernel `inherited` rows note **kernel-capable / not baseline-reachable** when profile or tx rows block reachability.
- [ ] Implicit-default behavior is `inherited` with cited default, not `unsupported`.
- [ ] `deviation` rows have **Test ref** populated.
- [ ] BCOS/OP baseline claims are not supported by ETH reference tests alone.

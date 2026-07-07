# EEST Statetest Integration Design

**Date:** 2026-07-06  
**Status:** Approved — 2026-07-07 (sign-off: Harness H2–H7 plan)  
**Scope:** Eth reference path — full EEST `state_tests` integration via `EthEestStateGranular` (evmone-statetest parity) + manifest smoke dual-track  
**Architecture choice:** Approach 1 — Granular-first hybrid (granular nightly + manifest PR gate)

**Implementation plan:** `bcos-evm/docs/superpowers/plans/2026-07-07-eest-statetest-harness-h2-h7.md`

**Related documents:**

- `bcos-evm/test/eth-eest-test/eest-integration-matrix.md` — corpus × runner matrix
- `bcos-evm/docs/superpowers/plans/2026-07-06-eest-parity-loop-prompt.md` — **Loop agent prompt** for closing parity gaps
- `bcos-evm/docs/superpowers/specs/2026-06-30-gtest-state-block-eest-migration-design.md` — original GTest granular runner design (Chapter 1 / 3)
- `bcos-evm/docs/audits/2026-07-01-eth-vs-geth-parity.md` — execution parity audit (Phase 3 work packages)
- `bcos-evm/docs/superpowers/specs/2026-07-06-evm-execution-trace-design.md` — optional triage tooling (Phase 2 harness)

**Frozen decisions (brainstorming):**

| Item | Choice |
|------|--------|
| Spec scope | Harness (Phase 1–2) detailed + Parity roadmap/work packages (Phase 3), no function-level fix specs |
| Integration model | Granular-first hybrid; manifest retained for PR smoke / capability rows |
| Execution path | Sealed: `EthMessageAdapter::execute` → `applyEthMessage` (product TE path) |
| CI gate | PR = `specs-tests-smoke`; nightly = full granular tree (informational until parity milestones) |
| Blockchain tests | Out of scope (separate spec) |

---

## 1. Problem Statement

bcos-evm already implements `EthEestStateGranular` — a GTest dynamic-registration runner structurally equivalent to evmone `evmone-statetest`. However:

1. **CI runs only a smoke slice** (`cancun/` subdirectory via `EthEestStateGranularSmoke`).
2. **Fork selection is coarse** — default scan of `eth-cancun,eth-prague,eth-osaka` skips Berlin/London/Paris native dirs.
3. **Manifest full sweep** (`eth-eest-state-full.json`, 16 entries) passes at **4140/4140** (2026-07-07); granular full tree (H1) wired but lacks fork inference / historical profiles.
4. **Granular harness gaps:** no slow filter, no `-k`/multi-path CLI, triple-profile scan causes false fails on historical dirs, no nightly failure buckets.

This spec defines harness work (H2–H7) to reach evmone-statetest **operational** parity. Manifest execution parity (Phase 3 WP) is **closed**; remaining parity work targets **granular full tree + WP-HIST** (Berlin/London/Paris).

---

## 2. Verified Baseline (2026-07-07)

Environment: `build-bcos-evm-check`, EEST pin **v5.4.0**.

### Manifest (`EthExecutionSpecStateTestsFull`)

| Metric | Value |
|--------|-------|
| Manifest entries | 16 native EIP directories |
| Subtests executed | **4140** |
| Pass | **4140 (100%)** |
| Fail | **0** |

`ctest -L specs-tests-smoke` → **37/37** (includes block fixture ADR-015/ADR-028 oracle alignment).

### Granular (`EthEestStateGranularFull`)

| Metric | Value |
|--------|-------|
| H1 CTest | Wired — `${EEST}/fixtures/state_tests` recursive |
| Registered file-level cases | ~2723 (default triple-profile scan) |
| Manifest parity | Not 1:1 — triple profile + historical dirs produce extra fails/skips |

Historical manifest baseline (2026-06-21): core slice **519/1056 (49%)** — superseded.  
Loop parity closure: `bcos-evm/docs/superpowers/plans/2026-07-06-eest-parity-loop-1-report.md`, loop-2-report.

---

## 3. Goals and Non-Goals

### 3.1 Goals

**Phase 1–2 (Harness)**

1. Nightly CTest: `EthEestStateGranular` on full `${EEST}/fixtures/state_tests`.
2. CLI parity with evmone-statetest: slow-test default filter, `-k` name filter, multi-path input.
3. Per-case fork inference from JSON `network`/`post` (not fixed profile list).
4. Extend `ForkProfileRegistry` with Berlin, London, Paris (minimum for historical dirs).
5. Failure bucket reporting (stateRoot / expectException / logsHash / status).
6. Preserve manifest smoke as PR gate (`specs-tests-smoke`).

**Phase 3 (Parity roadmap — manifest closed 2026-07-07)**

7. ~~Close 4844 → 7623/7702 → 6780/7825~~ → **manifest 4140/4140 achieved**; WPs become **regression guards**.
8. Remaining parity: **granular full-tree** (M8) + **WP-HIST** historical dirs after H4/H5.
9. Optional: Execution Trace Phase 0 for failure triage (H8).

### 3.2 Non-Goals

- Replacing `applyEthMessage` with evmone `test/state::transition`.
- Abandoning manifest / capability-rows PR gate.
- Blockchain test integration (`evmone-blockchaintest` — separate spec).
- Function-level parity fix specifications (separate EIP tasks).
- Amsterdam / BPO transition networks in Phase 1 (deferred to Phase 2.5).

---

## 4. Architecture

```
fixtures/state_tests/**/*.json
        │
        ├─► EthEestStateGranular          ← nightly / local full (this spec)
        │       loadEestStateTestFile
        │       → fork infer / ForkProfileRegistry
        │       → EthMessageAdapter::execute
        │       → assertResult(full assertLevels)
        │
        └─► EthExecutionSpecStateTests    ← PR smoke / capability gate (unchanged)
                manifest JSON → same execution path
```

**Invariant:** Runners are thin GTest shells; execution path is sealed (`EthMessageAdapter`).

**Dual-track responsibilities:**

| Track | Runner | When | Gate |
|-------|--------|------|------|
| Granular | `EthEestStateGranular` | Nightly, local dev | Informational |
| Manifest | `EthExecutionSpecStateTests` | PR CI | Required (`specs-tests-smoke`) |

---

## 5. Harness Components (Phase 1–2)

| ID | Component | Description | Acceptance |
|----|-----------|-------------|------------|
| H1 | `EthEestStateGranularFull` CTest | `${EEST}/fixtures/state_tests` full tree | ✅ `ctest -R EthEestStateGranularFull` registers >0 cases |
| H2 | Slow-test filter | Default `gtest_filter` excludes evmone-known slow tests | Runtime acceptable on dev machine |
| H3 | CLI extensions | Multi-path, `-k <substring>`, retain `--fork-profiles` override | `./EthEestStateGranular cancun/ -k 7702` works |
| H4 | Fork inference | Read case `network`/`post`; `findByUpstreamFork`; no match → `GTEST_SKIP` | Berlin/London dirs not mass-skipped |
| H5 | ForkProfile expansion | Add Berlin, London, Paris profiles | Registry tests pass |
| H6 | Unsupported → SKIP | Loader throws unsupported format → `GTEST_SKIP` not file FAIL | Engine-only JSON does not abort suite |
| H7 | Failure buckets | Post-process or runner summary: stateRoot / status / expectException / logsHash | Nightly artifact JSON/MD |
| H8 | `--trace` (optional) | Flag/env hooks Execution Trace Phase 0 | Single failing case produces trace |

### 5.1 CI Labels

| CTest | Label |
|-------|-------|
| `EthEestStateGranularSmoke` | `specs-tests-smoke;eest-statetest` (existing) |
| `EthEestStateGranularFull` | `specs-tests-full;eest-statetest;nightly` (new) |
| `EthExecutionSpecStateTests` | `specs-tests-smoke` (existing) |
| `EthExecutionSpecStateTestsFull` | `specs-tests-full` (existing) |

---

## 6. Parity Roadmap (Phase 3)

Work packages define **scope, metric, audit ref, acceptance command** — not implementation.

### 6.1 Milestones

| ID | Milestone | Target | Depends on |
|----|-----------|--------|------------|
| M1 | Harness full tree + buckets | H1–H7 done | — |
| M1b | Manifest regression guard | 4140/4140 sustained | Loop closed ✅ |
| M8 | Granular manifest-16 dirs green | match manifest pass rate under fork inference | M1 |
| M9 | WP-HIST Berlin/London/Paris | granular dirs runnable, bucketed fails | M1 |

*(Legacy M2–M7 manifest parity milestones superseded by Loop 1–2 closure @ 4140/0.)*

### 6.2 Work Packages

| WP | Directory / scope | Manifest (2026-07-07) | Granular target | Notes | Verify |
|----|-------------------|----------------------|-----------------|-------|--------|
| WP-4844 | `cancun/eip4844_blobs` | **1092/1092** ✅ | match manifest | regression guard | `eth-eest-4844-*` manifests |
| WP-7623 | `prague/eip7623_*` | **483/483** ✅ | match manifest | regression guard | slice manifests |
| WP-7702 | `prague/eip7702_*` | **552/552** ✅ | match manifest | regression guard | slice manifests |
| WP-6780 | `cancun/eip6780_*` | **115/115** ✅ | match manifest | regression guard | `eth-eest-6780-*` |
| WP-7825 | `osaka/eip7825_*` | **35/35** ✅ | match manifest | regression guard | smoke + full dir |
| WP-HIST | Berlin/London/Paris dirs | not in manifest | runnable + bucketed | after H5 | `EthEestStateGranular berlin/` |

### 6.3 Failure Modes

| Mode | Detection | Typical root cause |
|------|-----------|-------------------|
| stateRoot mismatch | `assertLevels` stateRoot | State journal, gas settlement, warm access |
| wrong status | transitional pass, stateRoot fail | included vs rejected tx |
| expectException miss | expectException assert | precheck / pool rules |
| logsHash mismatch | logsHash assert | log collection / receipt |

---

## 7. Testing Strategy

| Level | Target | Command |
|-------|--------|---------|
| Unit | Loader, fork inference, skip logic | `GeneralStateTestLoaderTest`, new registry tests |
| Smoke | Curated manifest | `ctest -L specs-tests-smoke` |
| Slice | Single EIP dir | `ctest -R EthExecutionSpecSliceEip*` |
| Full manifest | 16-entry sweep | `ctest -R EthExecutionSpecStateTestsFull` |
| Full granular | evmone-style tree | `ctest -R EthEestStateGranularFull` |

---

## 8. Risks and Dependencies

| Risk | Mitigation |
|------|------------|
| Full tree runtime | H2 slow filter; `-k` for dev slices |
| Mass SKIP without H4/H5 | Fork inference before nightly gate |
| 4844 blocks aggregate >90% | WP-4844 prioritized over 7623/7702 polish |
| Trace not ready | H8 optional; env probes remain |
| Pin drift vs evmone | Document pin bump policy in matrix |

---

## 9. Deferred (Phase 2.5+)

- Amsterdam + BPO transition profiles (`OsakaToBPO1AtTime15k`, etc.)
- Static `state_tests/static/` full tree nightly
- Merge gate promotion: slice >95% → required CTest
- Manifest auto-generation from granular metadata

---

## 10. Document Maintenance

| Field | Update trigger |
|-------|----------------|
| §2 baseline tables | Re-run `EthExecutionSpecStateTestsFull` after parity sprint |
| §6 milestones | WP closure or regression |
| Fork profiles | `ForkProfileRegistry.cpp` changes |

**Last verified:** 2026-07-07 (`build-bcos-evm-check`, manifest 4140/0, H1 granular CTest wired)

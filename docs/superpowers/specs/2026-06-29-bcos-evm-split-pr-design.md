# bcos-evm Split PR Plan — feat-bcos-evm → fisco/release-3.18.0

**Date:** 2026-06-29  
**Status:** Draft v1 (brainstorm approved pending user review)  
**Source branch:** `feat-bcos-evm`  
**Target branch:** `fisco/release-3.18.0`  
**Split working branch:** `feat-bcos-evm-split-pr` (to be created from target)

---

## 1. Problem

`feat-bcos-evm` introduces the entire `bcos-evm/` module (~153 production `.h/.cpp` files, ~4,634 **valid_insertions** per `tools/.ci/check-commit.sh`) on top of `fisco/release-3.18.0`, which currently has **zero** `bcos-evm/` files.

A single PR cannot land safely:

| Constraint | Source |
| --- | --- |
| `valid_insertions ≤ 300` per commit | `tools/.ci/check-commit.sh` (`insert_limit=300`) |
| Reviewability | Architecture spans eth kernel + opstack orchestration |
| CI compile | Each merged PR must leave monorepo buildable |
| Scope control | FISCO `bcos/` and `transaction-executor/` land in follow-up waves |

---

## 2. Decision

Land **6 active PRs** (PR-0 … PR-6) targeting `fisco/release-3.18.0`, then **2 deferred PRs** (PR-F, PR-TE).

| Wave | PRs | Scope |
| --- | --- | --- |
| **Active** | PR-0 … PR-6 | `bcos-evm/eth/`, `bcos-evm/opstack/`, CMake bootstrap, `test/eth/` + `test/opstack/` |
| **Deferred** | PR-F | `bcos-evm/bcos/` |
| **Deferred** | PR-TE | `transaction-executor/`, executor WASM link |

**Explicitly out of PR diffs:**

- All `.md` / ADR / review-pack (design narrative lives in GitHub PR description only)
- `bcos-evm/bcos/**` except `Placeholder.cpp` until PR-F
- `transaction-executor/**` until PR-TE
- `bcos-evm/test/bcos/**` until PR-F

---

## 3. check-commit.sh discipline

### 3.1 Measured scope (feat-bcos-evm vs release-3.18.0)

| Scope | Prod files | raw `+` | **valid_insertions** | min prod commits `⌈valid/300⌉` |
| --- | ---: | ---: | ---: | ---: |
| **Active wave** eth + opstack | 143 | 12,476 | **4,634** | **16** |
| `bcos-evm/eth/` | 91 | 9,083 | 3,686 | 13 |
| `bcos-evm/opstack/` | 52 | 3,393 | 948 | 4 |
| **Deferred** `bcos/` | 28 | 2,278 | 781 | 3 |
| **Deferred** `transaction-executor/` | 31 | 2,903 | 1,315 | 5 |
| **Tests** (excl. test/bcos) | 154 | 21,111 | 0 (excluded) | — |

**valid_insertions formula** (per commit, production paths only):

```text
valid = insertions(ignore-space-change)
      − new_files × 20
      − // comment lines
      − empty + lines
      − standalone { or } lines
      − #include lines
```

**Fail when:** `valid > 300` **and** git raw insertions `> 300`.

**Paths excluded from check:** any path matching `test`, `tools/`, `sample/`, `benchmark/`, `fisco-bcos/`, `.github/`.

### 3.2 Commit pairing

| Commit type | Rule |
| --- | --- |
| **Production** | Only non-`test/` `.h/.hpp/.cpp`; each commit `valid ≤ 300` |
| **Test** | Separate commit(s); path under `test/` → valid=0 for script |
| **Rename** | `git mv` batches; often valid≈0; still one logical commit per batch |
| **Header + impl** | Same commit (never split signature from first implementation) |

### 3.3 Original commit audit

Of commits touching in-scope production paths on `feat-bcos-evm`:

| Result | Count |
| --- | ---: |
| PASS (valid ≤ 300) | 149 |
| OVER (valid > 300, must split) | 8 |
| SKIP (no in-scope prod files) | 172 |

Top OVER commits (valid → min splits):

| valid | splits | Topic |
| ---: | ---: | --- |
| 1,545 | 6 | ethereum→eth layout rename |
| 597 | 2 | module directory reorganize |
| 592 | 2 | execution layer type rename |
| 523 | 2 | precompile skeleton |
| 438 | 2 | remove legacy hostcontext |
| 420 | 2 | P1 layout relocate |
| 406 | 2 | execution trace logging |
| 327 | 2 | EIP-7702 apply/delegatecall |

---

## 4. Architecture (active wave)

```text
                    ┌─────────────────────────────────────┐
                    │         bcos-evm-eth (eth/)           │
                    │  State, EthHost, ExecutionFrame,    │
                    │  TxPipeline, CallTargetResolver,    │
                    │  PrecompileRouter, gas helpers      │
                    └──────────────┬──────────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
    ┌─────────▼─────────┐  ┌───────▼────────┐  ┌────────▼────────┐
    │ bcos-evm-bcos     │  │ eth/reference  │  │ bcos-evm-op     │
    │ Placeholder.cpp   │  │ EthReference   │  │ OpStack lifecycle│
    │ (until PR-F)      │  │ Bridge         │  │ settlement/fee  │
    └───────────────────┘  └────────────────┘  └─────────────────┘

Invariant: eth/ never #includes bcos/ or opstack/.
```

**Port interfaces (PR-0 skeleton):**

- `ChainCallTargetPort.h` / `CallTargetResolver.h`
- `OrchestrationProfile.h` (Eth + OpStack profiles; Fisco profile deferred)
- `ExecutionSession.h` / `TxPipelineContext.h`

---

## 5. PR dependency graph

```text
PR-0 → PR-1 → PR-2 → PR-3 → PR-4 → PR-6
                              ↓
                         (later) PR-F (bcos/)
                              ↓
                         (later) PR-TE (transaction-executor)
```

| Phase | Parallelism |
| --- | --- |
| T0 | PR-0 only (bootstrap) |
| T1 | PR-1 (layout; blocks path-dependent work) |
| T2 | PR-2 → PR-3 (sequential) |
| T3 | PR-4 (after PR-3) |
| T4 | PR-6 (after PR-2 + PR-4) |
| T5 | PR-F ∥ PR-TE prep; PR-TE after PR-F recommended |

---

## 6. Active PR catalogue

### Summary table (check-commit.sh metrics)

| PR | Branch | valid | min prod commits | **est prod** | **est test** | **est total** |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| **PR-0** | `pr-0-bcos-evm-interfaces` | 648 | 3 | 4–5 | 1–2 | **6–7** |
| **PR-1** | `pr-1-bcos-evm-layout` | 368 | 2 | 4–5 | 3–4 | **7–9** |
| **PR-2** | `pr-2-bcos-evm-execution-frame` | 1,400 | 5 | 7–8 | 5–7 | **12–15** |
| **PR-3** | `pr-3-bcos-evm-orchestration` | 1,179 | 4 | 5–6 | 4–5 | **9–11** |
| **PR-4** | `pr-4-bcos-evm-opstack-fee` | 948 | 4 | 6–7 | 6–8 | **12–15** |
| **PR-6** | `pr-6-bcos-evm-geth-parity` | 110 | 1 | 1–2 | 2–3 | **3–5** |
| **Total** | | **4,653** | **19** | **27–33** | **21–29** | **49–62** |

---

### PR-0 — Bootstrap + interface skeleton

**Goal:** Introduce compilable `bcos-evm/` on `release-3.18.0` with zero behavior change to existing TE.

| Item | Detail |
| --- | --- |
| valid target | ~648 |
| prod commits | 4–5 |
| test commits | 1–2 (link smoke only) |

**Deliverables:**

1. Root `CMakeLists.txt` + `bcos-evm/CMakeLists.txt` (three static libs)
2. `bcos-evm/bcos/Placeholder.cpp` — empty TU for `bcos-evm-bcos`
3. `bcos-evm/opstack/Placeholder.cpp` — replaced incrementally in PR-4
4. Minimal `eth/` compile set: `RevisionConfig`, `State`, `EthHost` stubs, VM factory
5. Port headers + stub `.cpp`: `ChainCallTargetPort`, `CallTargetResolver`, `OrchestrationProfile`, `ExecutionSession`, `TxPipelineContext`

**Must NOT touch:** `transaction-executor/`, `bcos-evm/bcos/` (except Placeholder).

**Compile gate:**

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j
cmake --build build -j   # full monorepo; old TE unchanged
```

**GitHub PR title:** `[bcos-evm] Bootstrap three-library layout and orchestration port skeleton`

---

### PR-1 — Layout + constants

**Goal:** eth directory layout + shared constants; mostly renames.

| Item | Detail |
| --- | --- |
| valid target | ~368 |
| prod commits | 4–5 (includes rename batches with valid≈0) |
| test commits | 3–4 |

**Deliverables:**

- `git mv` eth subdirectories (execution/, pipeline/, gas/, precompiled/, reference/)
- `Eip2929Access.h` / implementation
- `ProtocolGas.h`, precompile index constants
- `TxIntrinsicGas` rename + `WarmTransactionEntry` fix

**Source commits (reference):** `098fa2aa2`, `e1106638c`, `1dde72a4a`, `3804d25b7`

---

### PR-2 — Eth execution kernel

**Goal:** `ExecutionFrame` unified pipeline, `PrecompileRouter`, `State` ownership.

| Item | Detail |
| --- | --- |
| valid target | ~1,400 |
| prod commits | 7–8 |
| test commits | 5–7 |

**Deliverables:**

- `ExecutionFrame.cpp` step unification + dual-track merge (split `063366771` 1250 valid → ~5 commits)
- `PrecompileRouter` envelope + table-driven `EthPrecompiles` (split `8627d6ad0` 363 valid → 2 commits)
- `EthHost::call` → `runExecutionFrame(Nested)`
- Tests: `ExecutionFrameTest`, `PrecompileRouter*`, `PrecompileEnvelopeTest`

**Source commits (reference):** `c70c63093`, `063366771`, `8627d6ad0`, `3797aceca`

---

### PR-3 — Orchestration pipeline + profiles

**Goal:** `runTxPipeline`, typed `OrchestrationProfile`, Eth/OpStack reference bridges.

| Item | Detail |
| --- | --- |
| valid target | ~1,179 |
| prod commits | 5–6 |
| test commits | 4–5 |

**Deliverables:**

- `OrchestrationProfile` Eth + OpStack bind (split `738f30e8b` 460 valid → 2 commits)
- `ChainPrecheckPolicy` + `FrameTargetResolver` (split `2978e439a` ~2349 lines → ~8 commits across PR-3)
- Post-execute error normalization (`58bfb9db6`)
- `OpStackPrecheckPolicy` consolidate (`4a3331db9`)
- `EthReferenceBridge`, `TxPipeline`, `TxExecutionAdapter`
- Tests: `TxPipelineTest`, `OrchestrationErrorPolicyTest`

**Exclude:** `FiscoOrchestrationProfile`, `FiscoPrecheckPolicy` → PR-F

---

### PR-4 — OpStack fee chain

**Goal:** Full `opstack/` production code; replace opstack Placeholder.

| Item | Detail |
| --- | --- |
| valid target | ~948 |
| prod commits | 6–7 |
| test commits | 6–8 |

**Deliverables:**

- `OpStackTxLifecycle` (split `1cdccf22c` 629 valid → 3 commits)
- `CallTargetResolver` + `OpStackChainCallTargetAdapter` + L1 port (split `d3b06b8db` 2265 valid → ~8 commits; **exclude** Fisco adapter → PR-F)
- `TxFeeSettlement` / EIP-1559 (split `db6242a17` 1038 valid → 4 commits)
- `OpStackPreDebitPlan`, `OpStackSettlement`, `OpStackPostSettlementPlan`, fee projection
- Jovian fork, deposit fixes, checkpoint revert
- Tests: `OpStackSettlement*`, `OpStackFeeTest`, `L1Block*`, `Deposit*`

**Exclude:** FISCO-specific settlement hooks → PR-F

---

### PR-6 — geth parity fixes

**Goal:** Small targeted fixes; mostly already PASS under check-commit.

| Item | Detail |
| --- | --- |
| valid target | ~110 |
| prod commits | 1–2 |
| test commits | 2–3 |

**Deliverables:**

- EIP-7702 authority ecrecover (`ad4c1f0f9`, valid=4)
- Nested CREATE nonce before checkpoint (`ef67f0742`, valid=11)
- Nested insufficient balance GAP-004 (`a0437ccf6`, valid=5)
- EIP-1153 tx entry reset (`e722d80a8`, valid=15)
- EIP-3651 coinbase warm (`7b50707fc`, valid=11)
- Top-level CREATE legacy gate (`a81d1273a`, valid=84)

---

## 7. Compile gate (all active PRs)

| PR range | Required build targets | TE / executor |
| --- | --- | --- |
| PR-0 … PR-6 | `bcos-evm-eth bcos-evm-bcos bcos-evm-op` + full `cmake --build build` | **Unchanged** (release-3.18.0 TE) |
| PR-F | above + real `bcos-evm-bcos` | still unchanged |
| PR-TE | above + `transaction-executor executor` | **Switch** TE link to bcos-evm |

**Per-commit rules:**

- Never delete/move a `.cpp` without same-commit reference update
- Never land header-only API change without stub or impl in same commit
- Run `bash tools/.ci/check-commit.sh` (or pre-commit hook) before each prod commit

---

## 8. Deferred PRs

### PR-F — FISCO bcos/ adapters

| Metric | Value |
| --- | --- |
| valid | ~781 |
| min prod commits | 3 |
| est total | ~8–10 |

**Contents:** `FiscoExecutionBridge`, `FiscoOrchestrationProfile`, `FiscoChainCallTargetAdapter`, `FiscoAddressDerivation`, remove `Placeholder.cpp`.

### PR-TE — transaction-executor integration

| Metric | Value |
| --- | --- |
| valid | ~1,315 |
| min prod commits | 5 |
| est total | ~12–15 |

**Contents:** TE CMake link `bcos-evm` + `bcos-evm-op`, remove legacy `HostContext` path, `EthTxGasSettlementTest`, executor WASM link.

**Recommended order:** PR-6 merged → PR-F → PR-TE.

---

## 9. Workflow

```bash
# 1. Create split branch from target
git checkout -b feat-bcos-evm-split-pr fisco/release-3.18.0

# 2. Topic branch per PR
git checkout -b pr-0-bcos-evm-interfaces feat-bcos-evm-split-pr

# 3. Per production commit
#    stage non-test .h/.cpp only → commit → verify valid ≤ 300
#    stage test/ → separate test: commit

# 4. Before PR push
cmake --build build --target bcos-evm-eth bcos-evm-op bcos-evm-bcos -j
cmake --build build -j
```

**Merge order:** PR-0 → PR-1 → PR-2 → PR-3 → PR-4 → PR-6 → (later) PR-F → PR-TE.

After all active PRs merge, `bcos-evm/eth` + `bcos-evm/opstack` on `release-3.18.0` should be functionally equivalent to `feat-bcos-evm` minus deferred paths.

---

## 10. GitHub PR-0 description template

Use on GitHub (not committed to repo):

- Architecture mermaid (eth kernel + opstack + Placeholder bcos)
- PR dependency graph (6 active + 2 deferred)
- check-commit discipline summary
- Compile gate commands
- Link to follow-up PR-F / PR-TE

---

## 11. Test plan (active wave)

- [ ] Each PR: `cmake --build build -j` green
- [ ] Each prod commit: `valid_insertions ≤ 300`
- [ ] PR-0: three libraries link
- [ ] PR-2: `ctest -R 'ExecutionFrame|PrecompileRouter'`
- [ ] PR-3: `ctest -R 'TxPipeline|OrchestrationErrorPolicy'`
- [ ] PR-4: `ctest -R 'OpStack|L1Block|Deposit|Blob|7702|PostSettlement'`
- [ ] PR-6: characterization tests for 7702 / CREATE nonce / GAP-004

---

## 12. Self-review checklist

- [x] No TBD sections
- [x] Scope matches user constraints (no docs in PRs, defer bcos/ + TE)
- [x] Metrics from `check-commit.sh` formula, not raw line guesses
- [x] Compile gate defined per PR
- [x] PR-0 explains bootstrap (release has no bcos-evm today)
- [ ] User review pending before `writing-plans` implementation plan

---

## 13. Next step

After user approves this spec:

1. Invoke **writing-plans** skill → `docs/superpowers/plans/2026-06-29-bcos-evm-split-pr-plan.md`
2. Execute PR-0 on `feat-bcos-evm-split-pr`

**Do not commit this spec file to PRs targeting `fisco/release-3.18.0`** — it is local planning metadata under `docs/superpowers/specs/`.

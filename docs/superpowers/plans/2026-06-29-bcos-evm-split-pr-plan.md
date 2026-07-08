# bcos-evm Split PR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land `feat-bcos-evm` eth + opstack changes onto `fisco/release-3.18.0` as 6 reviewable PRs (49–62 commits), each passing `check-commit.sh` and full monorepo compile, without touching `bcos/` (except Placeholder) or `transaction-executor/` until follow-up PRs.

**Architecture:** Bootstrap empty `bcos-evm/` on release-3.18.0 (which has zero bcos-evm files today), then incrementally import production code from `feat-bcos-evm` in dependency order: CMake → eth state/VM → ExecutionFrame → pipeline/profiles → opstack fee chain → geth parity fixes. Tests commit separately (valid=0 for check-commit).

**Tech Stack:** C++20, CMake 3.28+, evmone, Boost.Test, FISCO-BCOS monorepo build, `tools/.ci/check-commit.sh`

**Spec:** `docs/superpowers/specs/2026-06-29-bcos-evm-split-pr-design.md`

## Global Constraints

- `valid_insertions ≤ 300` per **production** commit (`tools/.ci/check-commit.sh`, `insert_limit=300`)
- Production paths only: non-`test/` `.h/.hpp/.cpp`; exclude `tools/`, `sample/`, `benchmark/`, `fisco-bcos/`, `.github/`
- Test commits: paths under `bcos-evm/test/` → separate commits; not counted toward valid limit
- **No `.md` / ADR files** in PR diffs to `fisco/release-3.18.0`
- **Do not modify** `transaction-executor/**` until PR-TE (deferred)
- **Do not import** `bcos-evm/bcos/**` except `bcos-evm/bcos/Placeholder.cpp` until PR-F (deferred)
- **Do not import** `bcos-evm/test/bcos/**` until PR-F
- Each PR merge gate: `cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j && cmake --build build -j`
- Source of truth for file contents: branch `feat-bcos-evm` (use `git checkout feat-bcos-evm -- <paths>`)
- Merge order: PR-0 → PR-1 → PR-2 → PR-3 → PR-4 → PR-6 → (later PR-F → PR-TE)

---

## File structure (active wave)

| Directory | Responsibility | First appears |
| --- | --- | --- |
| `bcos-evm/CMakeLists.txt` | Three static libs: eth, bcos (placeholder), op | PR-0 |
| `bcos-evm/bcos/Placeholder.cpp` | Empty TU for `bcos-evm-bcos` | PR-0 |
| `bcos-evm/opstack/Placeholder.cpp` | Empty TU until PR-4 replaces opstack | PR-0 |
| `bcos-evm/eth/state/` | State, EthHost, EvmStateReader | PR-0 (minimal) → PR-2 (full) |
| `bcos-evm/eth/vm/` | VMFactory, VMInstance | PR-0 |
| `bcos-evm/eth/ports/` | ChainCallTargetPort | PR-0 |
| `bcos-evm/eth/pipeline/` | TxPipeline, OrchestrationProfile, ExecutionSession | PR-0 (skeleton) → PR-3 (full) |
| `bcos-evm/eth/execution/` | ExecutionFrame, CallTargetResolver | PR-0 (headers) → PR-2 (full) |
| `bcos-evm/eth/eip/` | Intrinsic gas, EIP helpers, TxFeeSettlement | PR-1 → PR-4 |
| `bcos-evm/eth/precompiled/` | PrecompileRouter, EthPrecompiles | PR-2 |
| `bcos-evm/eth/reference/` | EthReferenceBridge, EthOrchestrationProfile | PR-3 |
| `bcos-evm/opstack/` | OpStack lifecycle, settlement, L1 predeploys | PR-4 |
| `bcos-evm/test/` | eth + opstack ctest targets | each PR (test commits) |

---

### Task 0: Worktree and split branch

**Files:**
- Create: none (git operations only)

- [ ] **Step 1: Fetch and verify target**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
rtk git fetch fisco ywy2090
rtk git rev-parse fisco/release-3.18.0 feat-bcos-evm
```

Expected: both refs resolve.

- [ ] **Step 2: Create split integration branch from target**

```bash
rtk git checkout fisco/release-3.18.0
rtk git checkout -b feat-bcos-evm-split-pr
rtk git checkout -b pr-0-bcos-evm-interfaces
```

- [ ] **Step 3: Confirm release has no bcos-evm**

```bash
rtk git ls-tree -r --name-only HEAD -- bcos-evm/ | wc -l
```

Expected: `0`

---

### Task 1: valid_insertions check helper

**Files:**
- Create: `tools/ci/check-valid-insertions.sh`

**Interfaces:**
- Produces: shell script exit 0 when staged/HEAD commit valid ≤ 300; prints breakdown

- [ ] **Step 1: Create helper script**

```bash
cat > tools/ci/check-valid-insertions.sh << 'EOF'
#!/bin/bash
# Local pre-commit helper mirroring tools/.ci/check-commit.sh check_PR_limit math.
set -euo pipefail
INSERT_LIMIT=300
LICENSE_LINE=20
BASE="${1:-HEAD^}"
HEAD="${2:-HEAD}"
need_check_files=$(git diff --numstat "$BASE" "$HEAD" \
  | awk '{if ($1!=0) print $0;}' \
  | sed 's/{.*> //g;s/}//g' \
  | awk '{print $3}' \
  | grep -vE 'sample/|benchmark/|test|tools/|fisco-bcos/|\.github/' \
  | grep -E '\.(h|hpp|c|cpp)$' || true)
if [ -z "$need_check_files" ]; then
  echo "OK: no production .h/.cpp insertions (valid=0)"
  exit 0
fi
new_files=$(git diff "$BASE" "$HEAD" $need_check_files | grep -c 'new file mode' || true)
empty_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*$' || true)
block_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*[\{\}]\s*$' || true)
include_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\#include' || true)
comment_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*//' || true)
insertions=$(git diff --ignore-space-change --shortstat "$BASE" "$HEAD" $need_check_files | awk '{print $4}')
insertions=${insertions:-0}
git_ins=$(git diff --shortstat "$BASE" "$HEAD" $need_check_files | awk '{print $4}')
git_ins=${git_ins:-0}
valid=$((insertions - new_files * LICENSE_LINE - comment_lines - empty_lines - block_lines - include_lines))
echo "valid_insertions=$valid (limit=$INSERT_LIMIT) raw=$insertions git=$git_ins new_files=$new_files"
if [ "$valid" -gt "$INSERT_LIMIT" ] && [ "$git_ins" -gt "$INSERT_LIMIT" ]; then
  echo "FAIL: valid_insertions $valid > $INSERT_LIMIT"
  exit 1
fi
echo "OK"
EOF
chmod +x tools/ci/check-valid-insertions.sh
```

- [ ] **Step 2: Do NOT commit this helper in PR-0** (lives under `tools/` excluded from PR valid check; optional local-only). Skip commit or add to a separate tooling commit if desired.

---

## PR-0 — Bootstrap + interface skeleton

**Branch:** `pr-0-bcos-evm-interfaces` → `fisco/release-3.18.0`  
**Target:** valid ~648, 4–5 prod commits + 1–2 test commits

---

### Task 2: PR-0 Commit 1 — CMake bootstrap + placeholders

**Files:**
- Create: `bcos-evm/CMakeLists.txt` (from feat-bcos-evm)
- Create: `bcos-evm/bcos/Placeholder.cpp`
- Create: `bcos-evm/opstack/Placeholder.cpp`
- Modify: `CMakeLists.txt` (add `add_subdirectory(bcos-evm)` under `if(FULLNODE)`)

**Interfaces:**
- Produces: targets `bcos-evm-eth`, `bcos-evm-bcos`, `bcos-evm-op`, alias `bcos-evm` → bcos

- [ ] **Step 1: Import CMake from feat-bcos-evm**

```bash
rtk git checkout feat-bcos-evm -- bcos-evm/CMakeLists.txt
```

- [ ] **Step 2: Add root CMakeLists entry**

In `CMakeLists.txt`, inside `if(FULLNODE)` after `find_package(blst CONFIG REQUIRED)`:

```cmake
    add_subdirectory(bcos-evm)
```

(Match line placement in feat-bcos-evm `CMakeLists.txt` around the executor block.)

- [ ] **Step 3: Create Placeholder translation units**

`bcos-evm/bcos/Placeholder.cpp`:

```cpp
/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
namespace bcos::evm::bcos {
void fiscoOrchestrationPlaceholder() {}
}  // namespace bcos::evm::bcos
```

`bcos-evm/opstack/Placeholder.cpp`:

```cpp
/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
namespace bcos::evm::opstack {
void opStackOrchestrationPlaceholder() {}
}  // namespace bcos::evm::opstack
```

- [ ] **Step 4: Stage production files only and verify valid**

```bash
rtk git add bcos-evm/CMakeLists.txt bcos-evm/bcos/Placeholder.cpp bcos-evm/opstack/Placeholder.cpp CMakeLists.txt
bash tools/ci/check-valid-insertions.sh
```

Expected: `OK` (valid ≪ 300)

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add three-library CMake skeleton and domain placeholders

Bootstrap bcos-evm-eth/bcos/op static libraries on release-3.18.0.
FISCO and OpStack shells use Placeholder.cpp until PR-4/PR-F.

EOF
)"
```

- [ ] **Step 6: Configure build (once per worktree)**

```bash
cmake -B build -S . -DFULLNODE=ON -DWITH_TESTS=OFF
```

Adjust flags to match your local release-3.18.0 build. `WITH_TESTS=OFF` for PR-0 until test cmake lands.

- [ ] **Step 7: Build placeholders (eth lib empty until Task 3)**

```bash
cmake --build build --target bcos-evm-bcos bcos-evm-op -j
```

Expected: both libraries link (may be empty until eth sources added).

---

### Task 3: PR-0 Commit 2 — eth state + VM minimal compile set

**Files:**
- Import from `feat-bcos-evm`:

```text
bcos-evm/eth/state/State.cpp
bcos-evm/eth/state/State.hpp
bcos-evm/eth/state/EthHost.cpp
bcos-evm/eth/state/EthHost.hpp
bcos-evm/eth/state/EvmStateReader.hpp
bcos-evm/eth/state/BlockInfo.hpp
bcos-evm/eth/state/Transaction.hpp
bcos-evm/eth/state/StateDiff.hpp
bcos-evm/eth/state/EvmHostHooks.h
bcos-evm/eth/state/HashUtils.hpp
bcos-evm/eth/vm/VMFactory.h
bcos-evm/eth/vm/VMInstance.cpp
bcos-evm/eth/RevisionConfig.h
bcos-evm/eth/EVMCResult.h
bcos-evm/eth/EVMCResult.cpp
bcos-evm/eth/Transfer.h
```

- [ ] **Step 1: Checkout file batch**

```bash
rtk git checkout feat-bcos-evm -- \
  bcos-evm/eth/state/ \
  bcos-evm/eth/vm/ \
  bcos-evm/eth/RevisionConfig.h \
  bcos-evm/eth/EVMCResult.h \
  bcos-evm/eth/EVMCResult.cpp \
  bcos-evm/eth/Transfer.h
```

- [ ] **Step 2: If valid > 300, split into two commits**

Run after staging:

```bash
rtk git add bcos-evm/eth/
bash tools/ci/check-valid-insertions.sh
```

If FAIL: unstage `EthHost.cpp` + `State.cpp` → commit headers/vm first; second commit adds `.cpp` bodies.

- [ ] **Step 3: Build eth library**

```bash
cmake --build build --target bcos-evm-eth -j
```

Expected: `bcos-evm-eth` links. Fix missing includes by adding dependent headers from feat-bcos-evm (same PR commit — do not leave broken build).

- [ ] **Step 4: Commit production batch**

```bash
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add minimal eth state and VM layer for bcos-evm-eth

Imports State, EthHost, RevisionConfig, EVMCResult, VMInstance — minimal
compile closure for the shared kernel library.

EOF
)"
```

---

### Task 4: PR-0 Commit 3 — orchestration port headers (skeleton)

**Files:**
- Import from `feat-bcos-evm`:

```text
bcos-evm/eth/ports/ChainCallTargetPort.h
bcos-evm/eth/execution/CallTargetResolver.h
bcos-evm/eth/execution/CallTargetResolver.cpp
bcos-evm/eth/pipeline/ExecutionSession.h
bcos-evm/eth/pipeline/StateTransitionContext.h
bcos-evm/eth/pipeline/OrchestrationProfile.h
bcos-evm/eth/reference/EthOrchestrationProfile.h
bcos-evm/opstack/OpStackOrchestrationProfile.h
```

If `OrchestrationProfile.h` pulls too many deps, import paired `.cpp` stubs from feat-bcos-evm in the **same commit**.

- [ ] **Step 1: Checkout port skeleton**

```bash
rtk git checkout feat-bcos-evm -- \
  bcos-evm/eth/ports/ChainCallTargetPort.h \
  bcos-evm/eth/execution/CallTargetResolver.h \
  bcos-evm/eth/execution/CallTargetResolver.cpp \
  bcos-evm/eth/pipeline/ExecutionSession.h \
  bcos-evm/eth/pipeline/StateTransitionContext.h \
  bcos-evm/eth/pipeline/OrchestrationProfile.h \
  bcos-evm/eth/reference/EthOrchestrationProfile.h \
  bcos-evm/opstack/OpStackOrchestrationProfile.h
```

- [ ] **Step 2: Resolve compile — add dependency headers in same commit**

```bash
cmake --build build --target bcos-evm-eth bcos-evm-op -j 2>&1 | head -40
```

For each missing header, `rtk git checkout feat-bcos-evm -- <path>` until build passes. Keep total valid ≤ 300; if over, move `OpStackOrchestrationProfile.h` to Commit 4.

- [ ] **Step 3: Verify valid and commit**

```bash
bash tools/ci/check-valid-insertions.sh
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add ChainCallTarget and OrchestrationProfile port skeleton

Introduces ADR-024/027 port headers and minimal CallTargetResolver TU.
OpStack profile header only; full bind lands in PR-3/PR-4.

EOF
)"
```

---

### Task 5: PR-0 Commit 4 — gas + execution header stubs (if budget remains)

**Files (import if valid budget allows in one commit, else split):**

```text
bcos-evm/eth/eip/ProtocolGas.h
bcos-evm/eth/eip/TxIntrinsicGas.h
bcos-evm/eth/execution/EvmCallFrame.h
bcos-evm/eth/execution/FrameScope.h
bcos-evm/eth/execution/InnerExecute.h
```

Skip `.cpp` implementations — land in PR-2.

- [ ] **Step 1–4:** Same pattern: checkout → build eth → check-valid → commit

```bash
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add execution and gas header stubs for pipeline PRs

Header-only forward declarations for ExecutionFrame and TxIntrinsicGas.
Implementations land in PR-1/PR-2.

EOF
)"
```

---

### Task 6: PR-0 Commit 5 (test) — link smoke test

**Files:**
- Create: `bcos-evm/test/CMakeLists.txt` (minimal — only builds dummy or defers ctest)
- OR defer tests to PR-1 when `EthTests.cmake` lands

**Recommended for PR-0:** skip test cmake; verify compile only.

- [ ] **Step 1: Full monorepo compile gate**

```bash
cmake --build build -j
```

Expected: PASS (transaction-executor unchanged; bcos-evm links as unused static libs).

- [ ] **Step 2: Open PR**

```bash
rtk git push -u ywy2090 pr-0-bcos-evm-interfaces
gh pr create --base release-3.18.0 --head pr-0-bcos-evm-interfaces \
  --title "[bcos-evm] Bootstrap three-library layout and orchestration port skeleton" \
  --body-file /dev/stdin << 'EOF'
## Summary
- Introduce bcos-evm/{eth,bcos,opstack} on release-3.18.0 (was empty)
- Placeholder bcos/op until PR-F / PR-4
- Port skeleton: ChainCallTargetPort, OrchestrationProfile, ExecutionSession

## Deferred
- bcos-evm/bcos/ (PR-F), transaction-executor/ (PR-TE), all .md docs

## Test plan
- [x] cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op
- [x] cmake --build build -j
EOF
```

---

## PR-1 — Layout + constants

**Branch:** `pr-1-bcos-evm-layout` (from `feat-bcos-evm-split-pr` after PR-0 merge)  
**Target:** valid ~368, 7–9 commits total

### Task 7: PR-1 rename batches (valid ≈ 0 each)

**Strategy:** Use `git mv` or `git checkout feat-bcos-evm --` for directory batches; one commit per batch.

| Commit | Paths | Notes |
| --- | --- | --- |
| P1-C1 | Move `bcos-evm/eth/orchestration/` → `bcos-evm/eth/pipeline/` (if any legacy paths exist) | valid≈0 |
| P1-C2 | Ensure `bcos-evm/eth/execution/` layout matches feat-bcos-evm | valid≈0 |
| P1-C3 | Ensure `bcos-evm/eth/eip/`, `bcos-evm/eth/precompiled/` directories exist | import empty dirs + headers |

For each commit:

- [ ] **Step 1:** Apply batch from feat-bcos-evm
- [ ] **Step 2:** `cmake --build build --target bcos-evm-eth -j`
- [ ] **Step 3:** `bash tools/ci/check-valid-insertions.sh`
- [ ] **Step 4:** Commit with `ref(bcos-evm): ...` prefix

**Source commits to mine:** `098fa2aa2`, `04e82dcad` (split into 3–4 rename commits)

---

### Task 8: PR-1 constants (valid commits)

| Commit | Files from feat-bcos-evm | Est. valid |
| --- | --- | ---: |
| P1-C4 | `bcos-evm/eth/execution/Eip2929Access.h`, any `.cpp` | ~86 |
| P1-C5 | `bcos-evm/eth/eip/ProtocolGas.h`, precompile index constants in `RevisionConfig.h` | ~154 |
| P1-C6 | `bcos-evm/eth/eip/TxIntrinsicGas.h`, `WarmTransactionEntry.h` rename fixes | ~40 |

- [ ] **Each commit:** import → build → check-valid → commit

**Reference source commits:** `e1106638c`, `1dde72a4a`, `3804d25b7`

---

### Task 9: PR-1 test commits

| Commit | Files |
| --- | --- |
| P1-T1 | `bcos-evm/test/CMakeLists.txt`, `bcos-evm/test/cmake/EthTests.cmake` (partial — gas tests only) |
| P1-T2 | `bcos-evm/test/eth/Eip2929OpcodeGasTest.cpp`, `Eip1559AccessTest.cpp` |
| P1-T3 | `bcos-evm/test/eth/EthIntrinsicGasFailureCharacterizationTest.cpp` (if present) |

```bash
cmake -B build -S . -DFULLNODE=ON -DWITH_TESTS=ON
cmake --build build --target Eip2929OpcodeGasTest -j
ctest -R Eip2929 -C Debug --output-on-failure
```

Test commits: no valid check needed.

---

## PR-2 — Eth execution kernel

**Branch:** `pr-2-bcos-evm-execution-frame`  
**Target:** valid ~1,400, 12–15 commits

### Task 10: PR-2 production commit slices

Split oversized source commits per spec §3.3:

| Commit | Files (from feat-bcos-evm) | Max valid | Source ref |
| --- | --- | ---: | --- |
| P2-C1 | `PrecompileRouter.h`, `PrecompileRouter.cpp` (router only) | 300 | `8627d6ad0` part 1 |
| P2-C2 | `EthPrecompiles.cpp`, `EthBuiltinRegistry.cpp`, `PrecompiledContract.cpp` | 300 | `8627d6ad0` part 2 |
| P2-C3 | `ExecutionFrame.h`, route/dispatch helpers in `ExecutionFrame.cpp` (part 1) | 300 | `063366771` part 1 |
| P2-C4 | `ExecutionFrame.cpp` checkpoint + transfer steps (part 2) | 300 | `063366771` part 2 |
| P2-C5 | `ExecutionFrame.cpp` finalize + VM execute (part 3) | 300 | `063366771` part 3 |
| P2-C6 | `ExecutionFrame.cpp` dual-track merge remainder | 300 | `063366771` part 4 |
| P2-C7 | `EthHost.cpp` delegate to `runExecutionFrame(Nested)` | 300 | `c70c63093` |
| P2-C8 | `State.cpp` ownership hardening | 300 | `8627d6ad0` |

**Per commit workflow:**

```bash
# 1. Extract from feat-bcos-evm (manual edit or partial checkout + edit)
# 2. Production stage only
rtk git add bcos-evm/eth/precompiled/ bcos-evm/eth/execution/  # adjust paths
bash tools/ci/check-valid-insertions.sh
cmake --build build --target bcos-evm-eth -j
rtk git commit -m "feat(bcos-evm): ..."
# 3. Test commit (separate)
rtk git add bcos-evm/test/eth/ExecutionFrameTest.cpp bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "test(bcos-evm): ExecutionFrame characterization for <topic>"
```

**PR-2 test gate:**

```bash
ctest -R 'ExecutionFrame|PrecompileRouter|PrecompileEnvelope' --output-on-failure
```

---

## PR-3 — Orchestration pipeline

**Branch:** `pr-3-bcos-evm-orchestration`  
**Target:** valid ~1,179, 9–11 commits

### Task 11: PR-3 production commit slices

| Commit | Files | Max valid | Source ref |
| --- | --- | ---: | --- |
| P3-C1 | `eth/pipeline/StateTransitionExecute.h`, `TxPipeline.cpp`, `adoptEvmcResult.h` | 300 | pipeline core |
| P3-C2 | `eth/pipeline/ChainPrecheckPolicy.h`, eth side only | 300 | `2978e439a` part 1 |
| P3-C3 | `eth/execution/FrameTargetResolver.cpp`, `FrameTargetResolver.h` | 300 | `2978e439a` part 2 |
| P3-C4 | `eth/reference/EthOrchestrationProfile.cpp`, `EthOrchestrationProfile.h` | 300 | `738f30e8b` part 1 |
| P3-C5 | `opstack/OpStackOrchestrationProfile.cpp` (header may exist from PR-0) | 300 | `738f30e8b` part 2 |
| P3-C6 | `eth/pipeline/OrchestrationErrorPolicy.h`, post-execute normalization | 300 | `58bfb9db6` |
| P3-C7 | `eth/reference/EthReferenceBridge.cpp`, `EthReferenceBridge.h` | 300 | bridge |
| P3-C8 | `eth/execution/TxExecutionAdapter.cpp`, `TxExecutionAdapter.h` | 300 | adapter |
| P3-C9 | `opstack/OpStackPrecheckPolicy.cpp`, `OpStackPrecheckPolicy.h` | 300 | `4a3331db9` |

**Exclude from all PR-3 commits:**

```text
bcos-evm/bcos/FiscoOrchestrationProfile.*
bcos-evm/bcos/FiscoPrecheckPolicy.*
```

**PR-3 test gate:**

```bash
ctest -R 'TxPipeline|OrchestrationErrorPolicy|TxExecutionAdapter' --output-on-failure
```

---

## PR-4 — OpStack fee chain

**Branch:** `pr-4-bcos-evm-opstack-fee`  
**Target:** valid ~948, 12–15 commits

### Task 12: Remove opstack Placeholder in first PR-4 commit

- [ ] **Step 1:** Delete `bcos-evm/opstack/Placeholder.cpp` in same commit that adds first real opstack `.cpp`

---

### Task 13: PR-4 production commit slices

| Commit | Files | Max valid | Source ref |
| --- | --- | ---: | --- |
| P4-C1 | `OpStackTxLifecycle.h`, `OpStackTxLifecycle.cpp` (part 1) | 300 | `1cdccf22c` |
| P4-C2 | `OpStackTxLifecycle.cpp` (part 2) + `OpStackPipelineInternals.h` | 300 | `1cdccf22c` |
| P4-C3 | `CallTargetResolver.cpp` deepening (eth side, no Fisco adapter) | 300 | `d3b06b8db` part 1 |
| P4-C4 | `OpStackChainCallTargetAdapter.cpp`, `.h` | 300 | `d3b06b8db` part 2 |
| P4-C5 | `opstack/l1/L1BlockStorage.cpp`, `L1BlockPredeploy.cpp` | 300 | `d3b06b8db` part 3 |
| P4-C6 | `eth/gas/TxFeeSettlement.h`, `Eip1559Access.h`, settlement helpers (part 1) | 300 | `db6242a17` |
| P4-C7 | `TxFeeSettlement` consumers in eth (part 2) | 300 | `db6242a17` |
| P4-C8 | `fee/OpStackPreDebitPlan.cpp`, `.h` | 300 | `523f06eff` |
| P4-C9 | `OpStackSettlement.cpp`, `OpStackSettlementView.cpp` | 300 | settlement |
| P4-C10 | `fee/OpStackPostSettlementPlan.cpp`, `OpStackPostSettlementInputs.h` | 300 | post-settlement |
| P4-C11 | `OpStackNormalFeeSettlement.cpp`, fee projection | 300 | `74cefcbd3` |
| P4-C12 | `OpStackForkSchedule.h` Jovian + deposit/checkpoint fixes | 300 | `371e5b659`, `922e37a6a`, `a3897f380` |

**Exclude:**

```text
bcos-evm/bcos/FiscoChainCallTargetAdapter.*
```

**PR-4 test gate:**

```bash
ctest -R 'OpStack|L1Block|Deposit|Blob|7702|PostSettlement|PreDebit' --output-on-failure
```

---

## PR-6 — geth parity fixes

**Branch:** `pr-6-bcos-evm-geth-parity`  
**Target:** valid ~110, 3–5 commits

### Task 14: PR-6 cherry-pick small fixes from feat-bcos-evm

Each fix is already PASS under check-commit (valid ≤ 84). Prefer one fix per prod commit:

| Commit | feat-bcos-evm ref | Files | valid |
| --- | --- | --- | ---: |
| P6-C1 | `ad4c1f0f9` | `bcos-evm/eth/Eip7702.cpp` | 4 |
| P6-C2 | `a0437ccf6` | `bcos-evm/eth/execution/FrameValueTransfer.h`, `ExecutionFrame.cpp` | 5 |
| P6-C3 | `ef67f0742` | `CallTargetResolver.cpp`, `ExecutionFrame.cpp` | 11 |
| P6-C4 | `e722d80a8` | `WarmTransactionEntry.h`, fixture-related eth files | 15 |
| P6-C5 | `a81d1273a`, `7b50707fc` | CREATE gate + coinbase warm | 95 |

```bash
# Example for one fix:
rtk git checkout feat-bcos-evm -- bcos-evm/eth/Eip7702.cpp
cmake --build build --target bcos-evm-eth -j
bash tools/ci/check-valid-insertions.sh
rtk git commit -m "fix(bcos-evm): require ecrecover for EIP-7702 authority resolution"
```

**PR-6 test gate:**

```bash
ctest -R '7702|ExecutionFrame|EthIncludedTxVmerr' --output-on-failure
```

---

## PR integration workflow (after each PR merge)

### Task 15: Rebase next topic branch

- [ ] **Step 1: Update integration branch**

```bash
rtk git checkout feat-bcos-evm-split-pr
rtk git merge --ff-only pr-N-...   # after GitHub merge
```

- [ ] **Step 2: Create next topic branch**

```bash
rtk git checkout -b pr-1-bcos-evm-layout feat-bcos-evm-split-pr
```

Repeat through PR-6.

---

## Final verification (after PR-6)

### Task 16: Diff equivalence check

- [ ] **Step 1: Compare in-scope paths vs feat-bcos-evm**

```bash
rtk git diff feat-bcos-evm -- bcos-evm/eth/ bcos-evm/opstack/ bcos-evm/CMakeLists.txt \
  | rtk grep -E '^\+\+\+|^---' | head -20
```

Expected: no unexpected diffs (except `bcos/Placeholder.cpp` and deferred paths).

- [ ] **Step 2: Full test sweep**

```bash
cmake --build build -j
cd build && ctest -R 'ExecutionFrame|TxPipeline|OpStack|PrecompileRouter' --output-on-failure
```

---

## Deferred waves (outline only — separate plans)

| PR | valid | min commits | Trigger |
| --- | ---: | ---: | --- |
| PR-F `bcos/` | 781 | 3+ | After PR-6 merged |
| PR-TE `transaction-executor/` | 1,315 | 5+ | After PR-F merged |

Do not start PR-F or PR-TE until PR-6 is on `release-3.18.0`.

---

## Spec coverage self-review

| Spec section | Plan task |
| --- | --- |
| §2 six active PRs | Tasks 2–14 |
| §3 check-commit | Task 1 + every commit step |
| §7 compile gate | Every task build step |
| §8 deferred PR-F/TE | Deferred section |
| §9 workflow | Task 0, 15 |
| PR-0 bootstrap | Tasks 2–6 |
| No docs in PRs | No `.md` in any checkout list |
| No TE until PR-TE | Global constraints |

**Placeholder scan:** None — all commits name explicit paths.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-29-bcos-evm-split-pr-plan.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task (Task 0 → Task 6 for PR-0), review between tasks  
2. **Inline Execution** — execute tasks in this session using executing-plans, batch through PR-0 then checkpoint

**Which approach?**

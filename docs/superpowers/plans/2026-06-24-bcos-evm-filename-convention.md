# bcos-evm Filename Convention — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify `bcos-evm/` source filenames to PascalCase basenames (Phase 1+1b), mirror test directory structure (Phase 2), and enforce via CI — zero runtime semantic change.

**Architecture:** Mechanical `git mv` renames + global `#include`/CMake path updates. `eth/state/` Legacy Enclave: PascalCase basenames in Phase 1b; `.hpp` extension remains until Phase 3. Function identifiers (`executeMessage()`, `debitIntrinsicGas()`) unchanged.

**Tech Stack:** C++20, CMake, CTest, Bash CI scripts, GitHub Actions (`capability-gate.yml`).

**Spec:** `docs/superpowers/specs/2026-06-24-bcos-evm-filename-convention-design.md`

## Global Constraints

- **Zero semantic change** — rename/`#include`/CMake only; no logic edits.
- **PascalCase basename** for all scoped files; no snake_case, no camelCase filenames.
- **Function names stay camelCase** — only file paths change.
- **`git mv`** for all renames (preserve blame). On case-insensitive FS (macOS), use two-step rename for case-only changes: `git mv foo.h tmp.h && git mv tmp.h Foo.h`.
- **Phase 3/4 out of scope** — do not change `State.hpp` → `State.h` or `include/bcos-evm/*.hpp`.
- **`eth/` must never `#include` `bcos/` or `opstack/`** — unchanged by this plan.
- CI: basename enforced everywhere; `.hpp` extension allowed only under `eth/state/` and `include/bcos-evm/`.

### Build & Test Conventions

- Run from repo root: `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`
- Build dir: `build/` (create if missing: `cmake -B build -DTESTS=ON`)
- Build: `cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op 2>&1 | rtk err`
- Full test: `ctest --test-dir build -R "bcos-evm|Orchestration|ExecuteMessage|DebitIntrinsic|PragueState" --output-on-failure 2>&1 | rtk err`
- Filename gate: `bash bcos-evm/tools/ci/check-filename-convention.sh`

### File Map (renames)

| Old | New |
| --- | --- |
| `eth/ExecuteMessage.h/.cpp` | `eth/ExecuteMessage.h/.cpp` |
| `eth/execution/WarmTransactionEntry.h` | `eth/execution/WarmTransactionEntry.h` |
| `eth/orchestration/adoptEvmcResult.h` | `eth/orchestration/AdoptEvmcResult.h` |
| `eth/orchestration/debitIntrinsicGas.h` | `eth/orchestration/DebitIntrinsicGas.h` |
| `eth/orchestration/buildExecuteMessageInput.h` | `eth/orchestration/BuildExecuteMessageInput.h` |
| `eth/orchestration/captureSettlementSnapshot.h` | `eth/orchestration/CaptureSettlementSnapshot.h` |
| `eth/orchestration/normalizeIncludedTxVmerr.h` | `eth/orchestration/NormalizeIncludedTxVmerr.h` |
| `opstack/OpStackBlobTxIntent.h` | `opstack/OpStackBlobTxIntent.h` |
| `eth/state/BloomFilter.hpp/.cpp` | `eth/state/BloomFilter.hpp/.cpp` |
| `eth/state/HashUtils.hpp` | `eth/state/HashUtils.hpp` |
| `eth/state/Errors.hpp` | `eth/state/Errors.hpp` |
| `eth/state/Transition.hpp/.cpp` | `eth/state/Transition.hpp/.cpp` |

---

### Task 1: CI filename convention gate

**Files:**
- Create: `bcos-evm/tools/ci/check-filename-convention.sh`
- Modify: `.github/workflows/capability-gate.yml`

**Interfaces:**
- Produces: exit 0 when all scoped files comply; exit 1 with file list on violation.

- [ ] **Step 1: Create the gate script**

Create `bcos-evm/tools/ci/check-filename-convention.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

HPP_ALLOWLIST=(eth/state include/bcos-evm)
status=0

is_hpp_allowed() {
  local relpath="$1"
  for prefix in "${HPP_ALLOWLIST[@]}"; do
    [[ "$relpath" == "$prefix"/* ]] && return 0
  done
  return 1
}

while IFS= read -r -d '' file; do
  relpath="${file#./}"
  base="$(basename "$file")"
  stem="${base%.*}"
  ext="${base##*.}"

  # PascalCase basename: starts uppercase, alphanumeric only
  if [[ ! "$stem" =~ ^[A-Z][A-Za-z0-9]*$ ]]; then
    echo "ERROR: non-PascalCase basename: $relpath" >&2
    status=1
    continue
  fi

  case "$ext" in
    cpp) ;;
    h) ;;
    hpp)
      if ! is_hpp_allowed "$relpath"; then
        echo "ERROR: .hpp not allowed outside eth/state/ or include/bcos-evm/: $relpath" >&2
        status=1
      fi
      ;;
    *)
      continue
      ;;
  esac
done < <(find eth bcos opstack test specs-tests \
  \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print0 2>/dev/null)

if [[ $status -eq 0 ]]; then
  echo "filename-convention gate: OK"
fi
exit $status
```

- [ ] **Step 2: Make executable and wire CI**

```bash
chmod +x bcos-evm/tools/ci/check-filename-convention.sh
```

Add to `.github/workflows/capability-gate.yml` in `matrix-lint` steps (after revision gate):

```yaml
      - name: Filename convention gate
        run: bash bcos-evm/tools/ci/check-filename-convention.sh
```

- [ ] **Step 3: Verify gate fails pre-rename (expected)**

Run: `bash bcos-evm/tools/ci/check-filename-convention.sh`
Expected: FAIL listing `ExecuteMessage.h`, `debitIntrinsicGas.h`, `BloomFilter.hpp`, etc.

---

### Task 2: Phase 1 — orchestration + ExecuteMessage renames

**Files:**
- Rename: 9 files (see File Map, excluding state + opstack)
- Modify: all `#include` references (~50 files), `bcos-evm/CMakeLists.txt`, `bcos-evm/test/CMakeLists.txt`, `bcos-evm/eth/orchestration/OrchestrationPipeline.cpp`, ADR-019 file layout

**Interfaces:**
- Consumes: nothing
- Produces: new include paths e.g. `#include "bcos-evm/eth/ExecuteMessage.h"`

- [ ] **Step 1: Two-step git mv for case-only renames (macOS-safe)**

```bash
cd bcos-evm
git mv eth/ExecuteMessage.h eth/_ExecuteMessage.h && git mv eth/_ExecuteMessage.h eth/ExecuteMessage.h
git mv eth/executeMessage.cpp eth/_ExecuteMessage.cpp && git mv eth/_ExecuteMessage.cpp eth/ExecuteMessage.cpp
git mv eth/execution/WarmTransactionEntry.h eth/execution/WarmTransactionEntry.h
git mv eth/orchestration/adoptEvmcResult.h eth/orchestration/AdoptEvmcResult.h
git mv eth/orchestration/debitIntrinsicGas.h eth/orchestration/DebitIntrinsicGas.h
git mv eth/orchestration/buildExecuteMessageInput.h eth/orchestration/BuildExecuteMessageInput.h
git mv eth/orchestration/captureSettlementSnapshot.h eth/orchestration/CaptureSettlementSnapshot.h
git mv eth/orchestration/normalizeIncludedTxVmerr.h eth/orchestration/NormalizeIncludedTxVmerr.h
```

- [ ] **Step 2: Bulk update `#include` paths**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
rg -l 'executeMessage\.h' bcos-evm include transaction-executor 2>/dev/null | while read f; do
  sed -i '' 's|executeMessage\.h|ExecuteMessage.h|g' "$f"
done
rg -l 'warmTransactionEntry\.h' bcos-evm | while read f; do
  sed -i '' 's|warmTransactionEntry\.h|WarmTransactionEntry.h|g' "$f"
done
for old new in \
  adoptEvmcResult AdoptEvmcResult \
  debitIntrinsicGas DebitIntrinsicGas \
  buildExecuteMessageInput BuildExecuteMessageInput \
  captureSettlementSnapshot CaptureSettlementSnapshot \
  normalizeIncludedTxVmerr NormalizeIncludedTxVmerr; do
  rg -l "${old}\.h" bcos-evm | while read f; do
    sed -i '' "s|${old}\.h|${new}.h|g" "$f"
  done
done
```

- [ ] **Step 3: Update CMakeLists**

In `bcos-evm/CMakeLists.txt` line 13:
```cmake
    eth/ExecuteMessage.cpp
```

In `bcos-evm/test/CMakeLists.txt`, replace all:
```
../eth/executeMessage.cpp  →  ../eth/ExecuteMessage.cpp
```

- [ ] **Step 4: Update `@file` comments in renamed headers**

In `eth/ExecuteMessage.h`, change `@file ExecuteMessage.h` → `@file ExecuteMessage.h`.

- [ ] **Step 5: Build smoke test**

Run: `cmake --build build --target bcos-evm-eth 2>&1 | rtk err`
Expected: PASS

Run: `cmake --build build --target DebitIntrinsicGasTest ExecuteMessageSmokeTest 2>&1 | rtk err`
Expected: PASS

- [ ] **Step 6: Verify no stale references**

Run: `rg 'executeMessage\.h|debitIntrinsicGas\.h|warmTransactionEntry\.h' bcos-evm include transaction-executor`
Expected: zero matches (except this plan/spec docs)

---

### Task 3: Phase 1b + opstack semantic rename

**Files:**
- Rename: 7 files (6 state + `opstack/OpStackBlobTxIntent.h`)
- Modify: `eth/state/` internal includes, `bcos-evm/CMakeLists.txt`, `test/CMakeLists.txt`, `opstack/OpStackPreCheck.cpp`

- [ ] **Step 1: git mv state snake_case files**

```bash
cd bcos-evm/eth/state
git mv BloomFilter.hpp BloomFilter.hpp
git mv BloomFilter.cpp BloomFilter.cpp
git mv HashUtils.hpp HashUtils.hpp
git mv Errors.hpp Errors.hpp
git mv Transition.hpp Transition.hpp
git mv Transition.cpp Transition.cpp
cd ../..
git mv opstack/OpStackBlobTxIntent.h opstack/OpStackBlobTxIntent.h
```

- [ ] **Step 2: Update includes**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
for old new in \
  bloom_filter BloomFilter \
  hash_utils HashUtils \
  errors Errors \
  transition Transition; do
  rg -l "${old}\.(hpp|cpp)" bcos-evm | while read f; do
    sed -i '' "s|${old}\.hpp|${new}.hpp|g; s|${old}\.cpp|${new}.cpp|g" "$f"
  done
done
sed -i '' 's|opstack/Eip4844\.h|opstack/OpStackBlobTxIntent.h|g' bcos-evm/opstack/OpStackPreCheck.cpp
```

- [ ] **Step 3: Update CMakeLists source paths**

In `bcos-evm/CMakeLists.txt`:
```cmake
    eth/state/BloomFilter.cpp
    eth/state/Transition.cpp
```

In `bcos-evm/test/CMakeLists.txt`, replace all:
```
../eth/state/BloomFilter.cpp  →  ../eth/state/BloomFilter.cpp
../eth/state/Transition.cpp    →  ../eth/state/Transition.cpp
```

- [ ] **Step 4: Build + run state-heavy tests**

Run: `cmake --build build --target PragueStateTest NestedCallHostTest 2>&1 | rtk err`
Run: `ctest --test-dir build -R "^PragueState$|^NestedCallHost$" --output-on-failure 2>&1 | rtk err`
Expected: PASS

- [ ] **Step 5: Verify no stale snake_case refs**

Run: `rg 'bloom_filter|hash_utils|errors\.hpp|/transition\.(hpp|cpp)|opstack/Eip4844\.h' bcos-evm --glob '*.{h,hpp,cpp}'`
Expected: zero matches in source (docs may reference old names historically)

---

### Task 4: Phase 2 — test directory mirror

**Files:**
- Move: 5 test `.cpp` from `test/` root to subdirs
- Modify: `bcos-evm/test/CMakeLists.txt` (5 `add_executable` source paths)

- [ ] **Step 1: git mv test files**

```bash
cd bcos-evm/test
git mv EthHostExtensionHooksTest.cpp eth/
git mv ExecuteMessageSmokeTest.cpp eth/
git mv ExecuteViaHostSmokeTest.cpp bcos/
git mv FiscoHostExtensionTest.cpp bcos/
git mv StateJournalRevertTest.cpp state/
```

- [ ] **Step 2: Update CMakeLists.txt paths**

| Variable block | Old source path | New source path |
| --- | --- | --- |
| `StateJournalRevertTest` | `StateJournalRevertTest.cpp` | `state/StateJournalRevertTest.cpp` |
| `EthHostExtensionHooksTest` | `EthHostExtensionHooksTest.cpp` | `eth/EthHostExtensionHooksTest.cpp` |
| `FiscoHostExtensionTest` | `FiscoHostExtensionTest.cpp` | `bcos/FiscoHostExtensionTest.cpp` |
| `ExecuteViaHostSmokeTest` | `ExecuteViaHostSmokeTest.cpp` | `bcos/ExecuteViaHostSmokeTest.cpp` |
| `ExecuteMessageSmokeTest` | `ExecuteMessageSmokeTest.cpp` | `eth/ExecuteMessageSmokeTest.cpp` |

CTest `NAME` values unchanged.

- [ ] **Step 3: Verify test root is clean**

Run: `ls bcos-evm/test/*.cpp 2>/dev/null`
Expected: no such files (or empty)

- [ ] **Step 4: Build moved tests**

Run: `cmake --build build --target StateJournalRevertTest ExecuteMessageSmokeTest FiscoHostExtensionTest 2>&1 | rtk err`
Expected: PASS

---

### Task 5: Documentation sync + final gate

**Files:**
- Modify: `bcos-evm/docs/adr/019-orchestration-pipeline.md` §File layout
- Modify: `bcos-evm/docs/architecture-overview.md` (add Legacy Enclave note)
- Already exists: `bcos-evm/docs/adr/020-filename-convention.md`

- [ ] **Step 1: Update ADR-019 file layout block**

Replace camelCase filenames with PascalCase equivalents per spec appendix A.

- [ ] **Step 2: Add Legacy Enclave note to architecture-overview.md**

Add under `eth/state/` description (~1 paragraph):

> `eth/state/` is a Legacy Enclave: basenames are PascalCase (Phase 1b); `.hpp` extension migrates to `.h` in Phase 3 (ADR-020).

- [ ] **Step 3: Run filename gate — must PASS**

Run: `bash bcos-evm/tools/ci/check-filename-convention.sh`
Expected: `filename-convention gate: OK`

- [ ] **Step 4: Full build + spot-check ctest**

Run: `cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op 2>&1 | rtk err`
Run: `ctest --test-dir build -R "Orchestration|DebitIntrinsic|ExecuteMessage|PragueState|OpStackPreCheck" --output-on-failure 2>&1 | rtk err`
Expected: all PASS

---

## Self-Review Checklist

| Spec requirement | Task |
| --- | --- |
| PascalCase orchestration headers | Task 2 |
| ExecuteMessage rename (33 refs) | Task 2 |
| eth/state snake_case → PascalCase | Task 3 |
| OpStackBlobTxIntent semantic rename | Task 3 |
| test/ directory mirror | Task 4 |
| CI gate script + workflow | Task 1 |
| ADR-019 + architecture-overview sync | Task 5 |
| Phase 3/4 deferred (no State.hpp→.h) | Global Constraints |

No placeholders. All rename paths explicit.

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-24-bcos-evm-filename-convention.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — 每个 Task 派一个 subagent，Task 间 review，迭代快

**2. Inline Execution** — 本会话内按 Task 顺序执行，checkpoint 处暂停确认

**你想用哪种方式开始实施？**

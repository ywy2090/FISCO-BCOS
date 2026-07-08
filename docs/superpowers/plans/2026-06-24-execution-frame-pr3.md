# ExecutionFrame PR3 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Build policy:** Tasks 1–4 write code/docs + commit — skip cmake/ctest unless noted. Task 5 runs unified regression gate.

**Goal:** Close the ExecutionFrame migration trilogy: update architecture docs to reflect unified frame execution, mark spec implementation complete, consolidate duplicate parity test helpers.

**Architecture:** PR1/PR2 landed `eth/execution/ExecutionFrame` and both adapters delegate to `runExecutionFrame`. PR3 is documentation + test hygiene — no production behavior changes.

**Tech Stack:** Markdown docs, Boost.Test fixtures, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-06-24-execution-frame-design.md` (§9 PR3, §11)

**Prerequisite:** PR2 complete (`274047e4a`).

## Global Constraints

- **No production code behavior changes** in PR3 unless a test-only refactor requires a header include fix.
- `eth/execution/` seam rules unchanged (ADR-005).
- Keep `PrecompileRouterEnvelope` ctest registered — do not delete the test binary; dedupe helpers only.
- ADR-021 is **optional** — prefer a short §追加 in `architecture-overview.md` + spec status update over a new ADR file unless review pack needs it.

### Build & Test Conventions

- Task 5 only: `cmake -B build -DTESTS=ON`, build `ExecutionFrameTest` + `PrecompileRouterEnvelopeTest`, run PR2 parity gate.

---

### Task 1: Update spec implementation status

**Files:**
- Modify: `docs/superpowers/specs/2026-06-24-execution-frame-design.md`

- [ ] **Step 1: Set status to implemented**

Change header block:

```markdown
**Implementation status:** **Done** — `eth/execution/ExecutionFrame.*`; `executeMessage` + `EthHost::call` delegate to `runExecutionFrame` (PR1–PR2, commits `ea1e4f2dc..274047e4a`).
```

- [ ] **Step 2: Commit**

```bash
rtk git add docs/superpowers/specs/2026-06-24-execution-frame-design.md
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): Mark ExecutionFrame spec implementation complete

EOF
)"
```

---

### Task 2: Update `architecture-overview.md` — ExecutionFrame layer

**Files:**
- Modify: `bcos-evm/docs/architecture-overview.md`

- [ ] **Step 1: Add ExecutionFrame to kernel diagram and §3 execution flow**

In mermaid + ASCII kernel box, add:

```text
runExecutionFrame()     统一帧执行 deep module（ExecutionFrame PR1–2）
EthHost::call()         evmc 嵌套帧 adapter → runExecutionFrame(Nested)
```

Update §3 text: `executeMessage` is now a **thin tx adapter** (warm, 7702 auth, nonce bump, finalize_self_destructs) delegating frame body to `runExecutionFrame(TopLevel)`.

Add new subsection **§3.1 Frame execution (ExecutionFrame)**:

```text
runTxPipeline → executeMessage (tx adapter)
                    └─ runExecutionFrame(TopLevel)
evmone callback → EthHost::call (nested adapter)
                    └─ runExecutionFrame(Nested)
                         └─ PrecompileRouter::dispatchPrecompile (step ③, sole call site)
```

- [ ] **Step 2: Fix §7 reviewer question #6**

Replace dual-track bullet with:

```markdown
6. ~~**内核帧语义双轨**~~ **Done (ExecutionFrame PR1–2)**：`executeMessage` 与 `EthHost::call` 均 delegate 至 `runExecutionFrame`；PrecompileRouter 仍保留 transfer→checkpoint→dispatch 信封（与 geth 已知偏差，非 Frame 范围）。
```

Update §7 item 2 note: ActivePrecompileSet done (`070434886`).

- [ ] **Step 3: Extend §8 file index**

Add rows:

| ExecutionFrame module | `eth/execution/EvmCallFrame.h` / `.cpp` |
| Frame helpers | `eth/execution/RouteMessage.*`, `FrameValueTransfer.h`, `ResolveExecutionCode.h`, `FrameCaller.h` |
| Frame parity tests | `test/eth/ExecutionFrameTest.cpp` |

Bump **校验** date to 2026-06-25.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/docs/architecture-overview.md
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): Document ExecutionFrame unified frame layer in architecture overview

EOF
)"
```

---

### Task 3: Update review + integration docs

**Files:**
- Modify: `bcos-evm/docs/architecture-review-post-orchestration-2026-06-23.md` (candidate 1 status)
- Modify: `bcos-evm/docs/module-integration-from-block-execution.md` (kernel box mentions ExecutionFrame)

- [ ] **Step 1: Mark architecture-review candidate 1 Done**

At §候选 1 header, add status line:

```markdown
**Status:** ✅ Done (ExecutionFrame PR1–2, `ea1e4f2dc..274047e4a`). See `architecture-overview.md` §3.1.
```

- [ ] **Step 2: Add ExecutionFrame to module-integration kernel layer**

In §1 mermaid `Kernel` node, append:

```text
runExecutionFrame + ExecutionFrame helpers
```

One sentence in kernel description: nested frames enter via `EthHost::call` → `runExecutionFrame(Nested)`; tx-level frames via `executeMessage` → `runExecutionFrame(TopLevel)`.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/docs/architecture-review-post-orchestration-2026-06-23.md \
  bcos-evm/docs/module-integration-from-block-execution.md
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): Mark ExecutionFrame complete in review and integration docs

EOF
)"
```

---

### Task 4: Consolidate parity test helpers

**Files:**
- Create: `bcos-evm/test/fixtures/EthFrameParityHelpers.h`
- Modify: `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`
- Modify: `bcos-evm/test/eth/ExecutionFrameTest.cpp`

**Interfaces:**
- Produces: shared `makeBaseInput`, `runDepth0`, `runDepth1`, `valueTransferMessage`, `balanceTarget`, address helpers

- [ ] **Step 1: Create shared header**

Extract duplicated helpers from `PrecompileRouterEnvelopeTest.cpp` (canonical) into `test/fixtures/EthFrameParityHelpers.h` under `namespace bcos::evm::test`.

Include: `ExecuteMessage.h`, `EthHost.hpp`, `InMemoryStateView.h`, evmone.

- [ ] **Step 2: Slim both test files**

Replace anonymous-namespace duplicates with:

```cpp
#include "fixtures/EthFrameParityHelpers.h"
```

Keep test case bodies unchanged. `ExecutionFrameTest`-specific helpers (`runFrameNested`, `runFrameTopLevel`, `FrameTestHost`) stay local.

- [ ] **Step 3: Skip build (Task 5)**

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/fixtures/EthFrameParityHelpers.h \
  bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp \
  bcos-evm/test/eth/ExecutionFrameTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): Extract shared EthFrame parity test helpers

EOF
)"
```

---

### Task 5: PR3 regression gate

- [ ] **Step 1: Build + test**

```bash
cmake -B build -DTESTS=ON 2>&1 | rtk err
cmake --build build --target ExecutionFrameTest PrecompileRouterEnvelopeTest -j8 2>&1 | rtk err
ctest --test-dir build -R "PrecompileRouterEnvelope|ExecutionFrame|ResolveExecutionCode|RouteMessage" --output-on-failure 2>&1 | rtk err
```

Expected: all PASS.

- [ ] **Step 2: Record in `.superpowers/sdd/pr3-task-5-report.md`**

Fix compile issues if any; commit fixes separately.

---

## PR3 Done Checklist

- [ ] Spec implementation status = Done
- [ ] `architecture-overview.md` documents ExecutionFrame; §7 dual-track removed
- [ ] Review + integration docs updated
- [ ] Shared `EthFrameParityHelpers.h`; envelope + ExecutionFrameTest deduped
- [ ] Parity gate green

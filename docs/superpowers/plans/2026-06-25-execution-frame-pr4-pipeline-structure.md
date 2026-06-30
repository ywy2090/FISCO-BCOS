# ExecutionFrame PR4 — Pipeline 结构重组 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Build policy:** Task 1 records baseline. Tasks 2–6 refactor `ExecutionFrame.cpp` with gate after each task. Task 7 runs full regression. Task 8 docs-only.

**Goal:** Delete internal dual-track in `ExecutionFrame.cpp` by extracting named step functions and two thin sequence orchestrators (`runTopLevelSteps` / `runNestedSteps`), with **zero behavior change** (RR6/RR7 frozen).

**Architecture:** Single public `runExecutionFrame(scope)` dispatches to scope-specific sequences. Shared steps live in `ExecutionFrame.cpp` anonymous namespace; early-return steps return `std::optional<FrameResult>`. `finalizeFrame(scope, …)` owns vm post-processing with internal scope branches for commit/nonce.

**Tech Stack:** C++17, Boost.Test, CMake/CTest, evmone, existing `eth/execution/*` helpers.

**Spec:** `docs/superpowers/specs/2026-06-25-execution-frame-pr4-pipeline-structure.md`

**Prerequisite:** PR1–PR2 Done (`executeMessage` + `EthHost::call` delegate to `runExecutionFrame`).

## Global Constraints

- **Zero behavior change** — existing `ExecutionFrameTest` + `PrecompileRouter*Test` are the semantic oracle; do not add step-level unit tests unless gate fails.
- **`ExecutionFrame.h` unchanged** — no public interface diff.
- **`ExecuteMessage.cpp` / `EthHost.cpp` unchanged** — adapters untouched.
- **RR6 frozen:** Nested `tryPrecompile` before `prepareNestedMessage`.
- **RR7 frozen:** TopLevel CREATE = checkpoint → bindCreate → transfer → initAccount; Nested CREATE = bindCreate → checkpoint → initAccount → transfer.
- **RR4 frozen:** Nested adapter ignores `fr.gasRefund` (no adapter changes in this PR).
- **`eth/execution/` must not `#include` `bcos/` or `opstack/`** (ADR-005).
- All new step functions stay in **`ExecutionFrame.cpp` anonymous namespace** — no new `.cpp` files per step.

### File map

| File | PR4 role |
| --- | --- |
| `bcos-evm/eth/execution/ExecutionFrame.cpp` | **Only production file modified** — step extract + sequences |
| `bcos-evm/eth/execution/EvmCallFrame.h` | **No changes** |
| `bcos-evm/eth/ExecuteMessage.cpp` | **No changes** |
| `bcos-evm/eth/state/EthHost.cpp` | **No changes** |
| `docs/superpowers/specs/2026-06-24-execution-frame-design.md` | PR4 status note (Task 8) |
| `bcos-evm/docs/architecture-overview.md` | Optional one-liner (Task 8) |

### Build & test commands

From repo root (adjust `build` path if needed):

```bash
cmake -B build -DTESTS=ON 2>&1 | rtk err
cmake --build build --target ExecutionFrameTest PrecompileRouterEnvelopeTest \
  PrecompileRouterCharacterizationTest PrecompileRouterEquivalenceTest -j8 2>&1 | rtk err
ctest --test-dir build -R "ExecutionFrame|PrecompileRouterEnvelope|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err
```

Smoke (Task 7):

```bash
ctest --test-dir build -R "RouteMessage|ResolveExecutionCode|FrameValueTransfer" --output-on-failure 2>&1 | rtk err
```

---

### Task 1: Baseline gate

**Files:**
- Read-only: `bcos-evm/eth/execution/ExecutionFrame.cpp`
- Report: `.superpowers/sdd/pr4-task-1-baseline-report.md` (create)

- [ ] **Step 1: Configure and build gate targets**

```bash
cmake -B build -DTESTS=ON 2>&1 | rtk err
cmake --build build --target ExecutionFrameTest PrecompileRouterEnvelopeTest \
  PrecompileRouterCharacterizationTest PrecompileRouterEquivalenceTest -j8 2>&1 | rtk err
```

- [ ] **Step 2: Run gate tests**

```bash
ctest --test-dir build -R "ExecutionFrame|PrecompileRouterEnvelope|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err
```

Expected: all PASS. If any FAIL, **stop PR4** — fix pre-existing failures before refactor.

- [ ] **Step 3: Record baseline**

Create `.superpowers/sdd/pr4-task-1-baseline-report.md`:

```markdown
# PR4 Task 1 — Baseline gate
Date: YYYY-MM-DD
ExecutionFrame: PASS/FAIL
PrecompileRouterEnvelope: PASS/FAIL
PrecompileRouterCharacterization: PASS/FAIL
PrecompileRouterEquivalence: PASS/FAIL
Notes: (any flakes or env issues)
```

- [ ] **Step 4: Commit baseline report only** (skip if user did not request commits; otherwise)

```bash
rtk git add .superpowers/sdd/pr4-task-1-baseline-report.md
rtk git commit -m "$(cat <<'EOF'
chore(bcos-evm): Record ExecutionFrame PR4 baseline gate

EOF
)"
```

---

### Task 2: `FrameWork` + early-return step skeleton

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`

**Interfaces:**
- Produces: `FrameWork`, `guardDelegatePrecompile`, `tryPrecompileAtTarget`, `transferOrFail`, `logFrameDoneIfNested`

- [ ] **Step 1: Add `#include <optional>` and `FrameWork`**

After existing includes in `ExecutionFrame.cpp`, add:

```cpp
#include <optional>
```

Inside `namespace {`, after `resolveCreateAddress`, add:

```cpp
struct FrameWork
{
    FrameContext& ctx;
    evmc_message const& originalMsg;
    RoutedMessage routed;
    bcos::bytes code;
    state::EthHost& host;

    evmc_message& callMessage() noexcept { return routed.message; }
};

void logFrameDoneIfNested(evmc_message const& originalMsg, evmc::Result const& result)
{
    if (originalMsg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call done") << LOG_KV("depth", originalMsg.depth)
                       << LOG_KV("status", trace::evmcStatus(result.status_code))
                       << LOG_KV("gasLeft", result.gas_left);
    }
}

std::optional<FrameResult> guardDelegatePrecompile(FrameWork const& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind == EVMC_DELEGATECALL && work.routed.hasPrecompileTarget &&
        work.ctx.extension != nullptr && !work.ctx.extension->allowDelegateCallToPrecompile())
    {
        return FrameResult{.result = makeFrameResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas)};
    }
    return std::nullopt;
}

std::optional<FrameResult> tryPrecompileAtTarget(
    FrameWork& work, evmc_address const& target, FrameScope scope)
{
    auto& callMessage = work.callMessage();
    if (isCreateKind(callMessage.kind))
    {
        return std::nullopt;
    }
    if (isDelegated7702Message(work.originalMsg) && callMessage.kind != EVMC_CALL)
    {
        return std::nullopt;
    }
    bool const skipVt =
        work.ctx.extension != nullptr && work.ctx.extension->skipHostValueTransfer();
    auto out = precompiled::dispatchPrecompile(
        {work.ctx.state, work.ctx.revisionConfig, work.ctx.extension, callMessage, target, skipVt});
    if (out.outcome == precompiled::PrecompileDispatchOutcome::NotApplicable)
    {
        return std::nullopt;
    }
    (void)scope;
    return FrameResult{
        .result = std::move(out.result), .gasRefund = out.gasRefund, .precompileHit = true};
}

std::optional<FrameResult> transferOrFail(FrameWork& work, FrameScope scope)
{
    if (!transferFrameValue(work.ctx.state, work.ctx.revisionConfig, work.ctx.extension,
            work.callMessage(), scope))
    {
        work.ctx.state.revert();
        return FrameResult{.result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, 0)};
    }
    return std::nullopt;
}

void prepareNestedMessage(FrameWork& work)
{
    auto const callerAddress =
        resolveCallerAddress(work.ctx.executionAddress, work.routed.message);
    if (work.ctx.extension != nullptr)
    {
        work.ctx.extension->setCallerAddress(callerAddress);
        work.ctx.extension->prepareMessage(work.ctx.revisionConfig.revision, work.routed.message);
    }
}

void bindCreateForInit(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    bindCreateMessageForInit(work.host, callMessage,
        bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), work.ctx.state);
}

void checkpointFrame(FrameWork& work) { work.ctx.state.checkpoint(); }

void initializeCreateAccount(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    initializeCreateTargetAccount(work.ctx.state, callMessage.recipient,
        work.ctx.revisionConfig.revision, work.ctx.revisionConfig.eip2929);
}
```

Do **not** wire these into `runExecutionFrame` yet — old paths remain.

- [ ] **Step 2: Build to verify compile**

```bash
cmake --build build --target ExecutionFrameTest -j8 2>&1 | rtk err
```

Expected: build succeeds (unused-function warnings OK).

- [ ] **Step 3: Run ExecutionFrame gate**

```bash
ctest --test-dir build -R ExecutionFrame --output-on-failure 2>&1 | rtk err
```

Expected: PASS (behavior unchanged).

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Add ExecutionFrame PR4 step skeleton (unused)

EOF
)"
```

---

### Task 3: Extract `runVm` + `finalizeFrame`

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`

**Interfaces:**
- Consumes: `FrameWork`, `makeFrameResult`, `resolveCreateAddress` (existing)
- Produces: `runVm(FrameWork&) -> evmc::Result`, `finalizeFrame(FrameWork&, FrameScope, evmc::Result) -> FrameResult`

- [ ] **Step 1: Add `runVm`**

```cpp
evmc::Result runVm(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (work.code.empty())
    {
        work.code = resolveExecutionCode(work.ctx.state, work.ctx.revisionConfig, callMessage);
    }
    return work.ctx.vm.execute(work.host, work.ctx.revisionConfig.revision, callMessage,
        work.code.data(), work.code.size());
}
```

- [ ] **Step 2: Add `finalizeFrame` — merge duplicated post-vm logic from lines 117–170 and 238–285**

```cpp
FrameResult finalizeFrame(FrameWork& work, FrameScope scope, evmc::Result result)
{
    auto& callMessage = work.callMessage();

    if (result.status_code == EVMC_SUCCESS && isCreateKind(callMessage.kind))
    {
        auto raw = result.release_raw();
        if (!applyCreateCodeDepositGas(raw, work.ctx.revisionConfig.revision) &&
            raw.release != nullptr)
        {
            raw.release(&raw);
            raw.release = nullptr;
            raw.output_data = nullptr;
            raw.output_size = 0;
        }
        if (raw.status_code == EVMC_SUCCESS)
        {
            result = evmc::Result(raw);
        }
        else
        {
            result = makeFrameResult(raw.status_code, raw.gas_left);
        }
    }

    if (result.status_code == EVMC_SUCCESS)
    {
        state::installCreatedContractCode(work.ctx.state, callMessage, result.raw());
        if (isCreateKind(callMessage.kind))
        {
            evmc_address createAddr = scope == FrameScope::TopLevel ?
                                          resolveCreateAddress(callMessage, result.raw()) :
                                          callMessage.recipient;
            work.host.markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = callMessage.recipient;
            }
        }

        if (scope == FrameScope::TopLevel)
        {
            if (work.ctx.fixNonceInit && isCreateKind(callMessage.kind))
            {
                auto createAddr = resolveCreateAddress(callMessage, result.raw());
                if (!state::isZeroAddress(createAddr))
                {
                    work.ctx.state.set_nonce(createAddr, 1);
                }
            }
        }
        else
        {
            work.ctx.state.commit();
            if (!isCreateKind(callMessage.kind))
            {
                auto const nextExecution = state::isZeroAddress(callMessage.code_address) ?
                                               callMessage.recipient :
                                               callMessage.code_address;
                if (!state::isZeroAddress(nextExecution))
                {
                    work.ctx.executionAddress = nextExecution;
                }
            }
        }
    }
    else
    {
        work.ctx.state.revert();
    }

    logFrameDoneIfNested(work.originalMsg, result);
    return FrameResult{.result = std::move(result)};
}
```

- [ ] **Step 3: Build + ExecutionFrame gate**

```bash
cmake --build build --target ExecutionFrameTest -j8 2>&1 | rtk err
ctest --test-dir build -R ExecutionFrame --output-on-failure 2>&1 | rtk err
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Extract runVm and finalizeFrame for PR4 pipeline

EOF
)"
```

---

### Task 4: Implement `runTopLevelSteps` + switch TopLevel path

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`

**Interfaces:**
- Consumes: all Task 2–3 step functions
- Produces: `runTopLevelSteps(FrameContext&, evmc_message, EthHost&) -> FrameResult`

- [ ] **Step 1: Add `runTopLevelSteps` (must match current `runTopLevelExecutionFrame` order exactly)**

```cpp
FrameResult runTopLevelSteps(FrameContext& ctx, evmc_message message, state::EthHost& host)
{
    FrameWork work{ctx, message, routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::TopLevel),
        {}, host};

    if (auto early = guardDelegatePrecompile(work))
    {
        return *early;
    }

    work.code = resolveExecutionCode(ctx.state, ctx.revisionConfig, work.callMessage());
    if (work.code.empty())
    {
        auto& callMessage = work.callMessage();
        auto const target = work.routed.hasPrecompileTarget ?
                                work.routed.precompileTarget :
                                (state::isZeroAddress(callMessage.code_address) ?
                                        callMessage.recipient :
                                        callMessage.code_address);
        if (auto early = tryPrecompileAtTarget(work, target, FrameScope::TopLevel))
        {
            return *early;
        }
    }

    checkpointFrame(work);

    if (!isCreateKind(work.callMessage().kind))
    {
        if (auto early = transferOrFail(work, FrameScope::TopLevel))
        {
            return *early;
        }
    }
    else
    {
        // RR7 TopLevel: checkpoint → bindCreate → transfer → initAccount
        bindCreateForInit(work);
        if (auto early = transferOrFail(work, FrameScope::TopLevel))
        {
            return *early;
        }
        initializeCreateAccount(work);
    }

    auto result = runVm(work);
    return finalizeFrame(work, FrameScope::TopLevel, std::move(result));
}
```

- [ ] **Step 2: Point TopLevel dispatch at `runTopLevelSteps`**

In `runExecutionFrame`, change:

```cpp
if (scope == FrameScope::TopLevel)
{
    return runTopLevelSteps(ctx, message, host);
}
```

Keep `runTopLevelExecutionFrame` in file but **unreferenced** temporarily (or delete in Task 5 after Nested migrates).

- [ ] **Step 3: Build + full §7.1 gate**

```bash
cmake --build build --target ExecutionFrameTest PrecompileRouterEnvelopeTest -j8 2>&1 | rtk err
ctest --test-dir build -R "ExecutionFrame|PrecompileRouterEnvelope" --output-on-failure 2>&1 | rtk err
```

Expected: PASS. Pay attention to:
- `top_level_create_checkpoint_before_bind_order`
- `top_level_frame_does_not_commit_before_adapter_nonce_bump`
- `top_level_precompile_*`

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Wire TopLevel frame through runTopLevelSteps pipeline

EOF
)"
```

---

### Task 5: Implement `runNestedSteps` + delete dead code

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`

**Interfaces:**
- Produces: `runNestedSteps(...) -> FrameResult`
- Deletes: `runTopLevelExecutionFrame`, Nested inline body in `runExecutionFrame`

- [ ] **Step 1: Add `runNestedSteps`**

```cpp
FrameResult runNestedSteps(FrameContext& ctx, evmc_message message, state::EthHost& host)
{
    FrameWork work{ctx, message, routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::Nested),
        {}, host};

    if (auto early = guardDelegatePrecompile(work))
    {
        return *early;
    }

    // RR6: tryPrecompile before prepareNestedMessage
    {
        auto& callMessage = work.callMessage();
        auto const target = state::isZeroAddress(callMessage.code_address) ?
                                callMessage.recipient :
                                callMessage.code_address;
        if (auto early = tryPrecompileAtTarget(work, target, FrameScope::Nested))
        {
            return *early;
        }
    }

    prepareNestedMessage(work);

    if (isCreateKind(work.callMessage().kind))
    {
        bindCreateForInit(work);
    }

    checkpointFrame(work);

    if (isCreateKind(work.callMessage().kind))
    {
        initializeCreateAccount(work);
    }

    if (auto early = transferOrFail(work, FrameScope::Nested))
    {
        return *early;
    }

    auto result = runVm(work);
    auto fr = finalizeFrame(work, FrameScope::Nested, std::move(result));

    // Nested CREATE attempt nonce bump (frozen: not success-dependent)
    auto& callMessage = work.callMessage();
    if (isCreateKind(callMessage.kind) && !state::isZeroAddress(callMessage.sender) &&
        work.originalMsg.depth > 0)
    {
        ctx.state.set_nonce(callMessage.sender, ctx.state.get_nonce(callMessage.sender) + 1);
        if (ctx.extension != nullptr &&
            std::memcmp(callMessage.sender.bytes, ctx.txOrigin.bytes, sizeof(callMessage.sender.bytes)) != 0)
        {
            ctx.extension->bumpContractCreateNonce(callMessage.sender);
        }
    }

    return fr;
}
```

- [ ] **Step 2: Replace `runExecutionFrame` body with dispatch-only**

```cpp
FrameResult runExecutionFrame(
    FrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    if (scope == FrameScope::TopLevel)
    {
        return runTopLevelSteps(ctx, message, host);
    }
    return runNestedSteps(ctx, message, host);
}
```

- [ ] **Step 3: Delete `runTopLevelExecutionFrame` and entire old Nested inline block**

Verify `ExecutionFrame.cpp` has **no** duplicated `applyCreateCodeDepositGas` / `installCreatedContractCode` blocks outside `finalizeFrame`.

- [ ] **Step 4: Build + §7.1 gate**

```bash
cmake --build build --target ExecutionFrameTest PrecompileRouterEnvelopeTest \
  PrecompileRouterCharacterizationTest PrecompileRouterEquivalenceTest -j8 2>&1 | rtk err
ctest --test-dir build -R "ExecutionFrame|PrecompileRouterEnvelope|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err
```

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Merge ExecutionFrame internal dual-track into pipeline steps

Delete runTopLevelExecutionFrame; TopLevel and Nested share step functions
with scope-specific sequences (RR6/RR7 preserved).

EOF
)"
```

---

### Task 6: Deletion test + code review checklist

**Files:**
- Read-only audit: `bcos-evm/eth/execution/ExecutionFrame.cpp`

- [ ] **Step 1: Verify deletion test**

Confirm:
- [ ] `runTopLevelExecutionFrame` **absent**
- [ ] Exactly **one** `applyCreateCodeDepositGas` call site (inside `finalizeFrame`)
- [ ] Exactly **one** `precompiled::dispatchPrecompile` call site (inside `tryPrecompileAtTarget`)
- [ ] `runTopLevelSteps` / `runNestedSteps` each ≤ ~45 lines
- [ ] `ExecutionFrame.h` unchanged (`git diff bcos-evm/eth/execution/EvmCallFrame.h` empty)

- [ ] **Step 2: Verify adapter files unchanged**

```bash
rtk git diff bcos-evm/eth/ExecuteMessage.cpp bcos-evm/eth/state/EthHost.cpp
```

Expected: no diff.

- [ ] **Step 3: Record in `.superpowers/sdd/pr4-task-6-review.md`**

```markdown
# PR4 Task 6 — Deletion test
- runTopLevelExecutionFrame removed: YES/NO
- Single dispatchPrecompile site: YES/NO
- Single finalize path: YES/NO
- Adapters untouched: YES/NO
```

---

### Task 7: Full regression gate

**Files:**
- Report: `.superpowers/sdd/pr4-task-7-gate-report.md`

- [ ] **Step 1: Run §7.1 + §7.2 smoke**

```bash
ctest --test-dir build -R "ExecutionFrame|PrecompileRouterEnvelope|PrecompileRouterCharacterization|PrecompileRouterEquivalence|RouteMessage|ResolveExecutionCode|FrameValueTransfer" --output-on-failure 2>&1 | rtk err
```

Expected: all PASS.

- [ ] **Step 2: Optional broader eth smoke** (if time permits)

```bash
ctest --test-dir build -R "ExecuteMessageSmoke|EipPrecompileRevisionGate" --output-on-failure 2>&1 | rtk err
```

- [ ] **Step 3: Write gate report + commit if requested**

---

### Task 8: Documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-06-25-execution-frame-pr4-pipeline-structure.md`
- Modify: `docs/superpowers/specs/2026-06-24-execution-frame-design.md`
- Modify: `bcos-evm/docs/architecture-overview.md` (optional one-liner)

- [ ] **Step 1: Mark PR4 spec Implemented**

In `2026-06-25-execution-frame-pr4-pipeline-structure.md` header:

```markdown
**Status:** Implemented
**Implementation:** `ExecutionFrame.cpp` pipeline steps + `runTopLevelSteps` / `runNestedSteps`
```

- [ ] **Step 2: Update parent spec §9 PR4 bullet**

Add after PR4 section in `2026-06-24-execution-frame-design.md`:

```markdown
**Status:** ✅ Done (PR4 pipeline structure, YYYY-MM-DD).
```

- [ ] **Step 3: Add architecture-overview §3.1 note**

```markdown
**PR4（2026-06-25）：** `ExecutionFrame.cpp` 内部 implementation 双轨已合并为命名 step + 两个 sequence 函数；RR6/RR7 scope 执行序冻结不变。
```

- [ ] **Step 4: Commit docs**

```bash
rtk git add docs/superpowers/specs/2026-06-25-execution-frame-pr4-pipeline-structure.md \
  docs/superpowers/specs/2026-06-24-execution-frame-design.md \
  bcos-evm/docs/architecture-overview.md
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): Mark ExecutionFrame PR4 pipeline structure complete

EOF
)"
```

---

## PR4 Done Checklist

- [ ] Task 1 baseline gate green
- [ ] `runTopLevelExecutionFrame` deleted
- [ ] `runTopLevelSteps` + `runNestedSteps` wired
- [ ] Shared steps: `guardDelegatePrecompile`, `tryPrecompileAtTarget`, `transferOrFail`, `runVm`, `finalizeFrame`
- [ ] §7.1 test matrix green
- [ ] `ExecutionFrame.h` / adapters unchanged
- [ ] Spec + architecture-overview updated

---

## Self-review (spec coverage)

| Spec § | Task |
| --- | --- |
| §2.1 结构合并 | Task 5 |
| §2.1 命名 step | Task 2–3 |
| §2.1 两个 sequence | Task 4–5 |
| §4.2 optional early-return | Task 2 |
| §4.3 TopLevel sequence | Task 4 |
| §4.4 Nested sequence | Task 5 |
| §4.5 finalizeFrame scope | Task 3 |
| §7.1 gate | Task 1, 4, 5, 7 |
| §9 success criteria | Task 6 checklist |
| §2.2 Non-goals (no semantic RR6/RR7 change) | Global Constraints |

No placeholders. All step functions named with signatures. Gate commands use exact CTest names from `EthTests.cmake` / `CrossTests.cmake`.

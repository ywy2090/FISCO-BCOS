# ExecutionFrame PR2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Build policy:** Tasks 1–7 write code + **tests** + commit — **skip running** `cmake`/`ctest`. Task 8 runs unified build, fixes compile/test failures until gate green.

**Goal:** Migrate `executeMessage` to a thin tx adapter delegating frame semantics to `runExecutionFrame(TopLevel)`, eliminating the tx-entry dual-track while preserving tx-level behavior (7702 auth, warm, sender nonce bump, `finalize_self_destructs`, precompile early-return).

**Architecture:** PR1 landed `eth/execution/` helpers and Nested production path. PR2 closes the loop: harden TopLevel Frame parity (RR8 route/code resolution, §4.2 guard, commit boundary), wire `executeMessage` per spec §5.1, delete duplicated helpers from `ExecuteMessage.cpp`, extend parity tests for depth=0 production path. After PR2, **both** `executeMessage` and `EthHost::call` delegate to Frame — no third frame implementation.

**Tech Stack:** C++20, evmc, evmone, Boost.Test, CMake/CTest, `bcos-evm-eth` static library.

**Spec:** `docs/superpowers/specs/2026-06-24-execution-frame-design.md` (§5.1, §7, §8.1 PR2+, §9 PR2)

**Prerequisite:** PR1 merged or rebased (`ea1e4f2dc` — `ExecutionFrame PR1` complete).

**Follow-up (out of scope):** PR3 docs/cleanup (`architecture-overview.md`, deprecate `runDepth0`/`runDepth1` helpers).

## Global Constraints

- `eth/execution/` must **never** `#include` `bcos/` or `opstack/` headers (ADR-005 / ADR-019).
- Chain behavior enters Frame only via `FrameContext::extension` (`state::EvmHostHooks*`).
- `FrameScope` is passed by the adapter; Frame must **not** branch on `message.depth` for semantics (trace/logging only).
- PR2 **freezes** TopLevel CREATE order as checkpoint → bindCreate (RR7); Nested CREATE as bindCreate → checkpoint (RR7).
- `runExecutionFrame` is synchronous and does **not** catch exceptions.
- Nested adapter (`EthHost::call`) **ignores** `FrameResult::gasRefund` (RR4) — unchanged in PR2.
- TopLevel adapter (`executeMessage`) **consumes** `fr.gasRefund` on `fr.precompileHit` (spec §7) — unchanged.
- After PR2, `ExecuteMessage.cpp` must **not** call `precompiled::dispatchPrecompile` directly — only `ExecutionFrame.cpp` step ③.
- `TxPipelineHooks::txRunEvmExecutionOverride` unchanged.
- 7702 **tx-level** auth (`applyAuthorizations`, sender pre-bump, `warmDelegationTarget`) stays in `executeMessage` adapter **before** `runExecutionFrame` (spec §4.3).
- Sender nonce bump (depth=0 success) stays in adapter **after** Frame returns (spec §4.1) — ordering vs `state.commit()` must match pre-PR2 behavior (see Task 2).

### Execution Policy (user override — PR2 batch build)

**Tasks 1–7:** write implementation **and test cases** (TDD-style: add tests in the same task as the feature); commit each task. **Do not run** `cmake`, `cmake --build`, or `ctest` — skipping execution means **no compile/test runs**, not skipping test authorship.

| Do in Tasks 1–7 | Do NOT in Tasks 1–7 |
| --- | --- |
| Write `.cpp` / `.h` production code | `cmake -B build` |
| Write Boost.Test cases per task spec | `cmake --build …` |
| Register new targets in `EthTests.cmake` | `ctest …` |
| Commit | Block on compile/test failures |

**Task 8 only:** unified configure → build all touched targets → fix compile errors → run full regression gate → fix test failures until PR2 gate green.

Implementer report contract (Tasks 1–7): status, files changed, commit hash, self-review notes, **list of test cases added** — **no** build/test run output until Task 8.

### Build & Test Conventions

**All commands below run only in Task 8** (Tasks 1–7 skip them).

- Run from repo root: `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`
- Configure: `cmake -B build -DTESTS=ON 2>&1 | rtk err`
- Build library: `cmake --build build --target bcos-evm-eth -j8 2>&1 | rtk err`
- Build one test: `cmake --build build --target <TargetName> -j8 2>&1 | rtk err`
- Run one ctest: `ctest --test-dir build -R "^<CTestName>$" --output-on-failure 2>&1 | rtk err`
- PR2 gate: `ctest --test-dir build -R "PrecompileRouterEnvelope|ExecutionFrame|ResolveExecutionCode|RouteMessage|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`
- Full eth + cross gate: `ctest --test-dir build -R "PrecompileRouter|Eip|Eth|Tx|ExecuteMessage|DebitIntrinsic|ExecutionFrame|RouteMessage|FrameValueTransfer|ResolveExecutionCode|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`

### File Map (PR2)

| Path | Responsibility |
| --- | --- |
| `bcos-evm/eth/execution/ExecutionFrame.cpp` | TopLevel hardening: §4.2 guard, commit boundary, precompile target |
| `bcos-evm/eth/execution/ResolveExecutionCode.h` | Parity fixes if tests expose gaps |
| `bcos-evm/eth/execution/RouteMessage.cpp` | TopLevel route parity (verify only unless tests fail) |
| `bcos-evm/eth/ExecuteMessage.cpp` | Thin tx adapter per §5.1; delete frame body + dead helpers |
| `bcos-evm/test/eth/ResolveExecutionCodeTest.cpp` | **New** — code resolution parity matrix |
| `bcos-evm/test/eth/ExecutionFrameTest.cpp` | Add depth=0 / executeMessage parity cases |
| `bcos-evm/test/cmake/EthTests.cmake` | Register `ResolveExecutionCodeTest` |

**Unchanged in PR2:** `EthHost.cpp` (already thin from PR1), `PrecompileRouter` envelope, `TxPipeline` hooks.

### Scope selection (executeMessage adapter)

Production tx path: `input.message.depth == 0` → `FrameScope::TopLevel`.

Legacy/test path (`depth > 0` calling `executeMessage` directly): use `FrameScope::Nested` — production nested frames always enter via `EthHost::call`; direct `executeMessage` at depth>0 is non-production and should align with Nested semantics going forward (spec §4.1: scope decided by call site, not `message.depth` inside Frame).

```cpp
auto const scope = input.message.depth == 0
    ? execution::FrameScope::TopLevel
    : execution::FrameScope::Nested;
```

---

### Task 1: `ResolveExecutionCode` parity unit tests

**Files:**
- Create: `bcos-evm/test/eth/ResolveExecutionCodeTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`

**Interfaces:**
- Consumes: `execution::resolveExecutionCode` (`ResolveExecutionCode.h`)
- Produces: registered ctest `ResolveExecutionCode`

Mirror old helpers from `ExecuteMessage.cpp` in test anonymous namespace for oracle comparison only (deleted in Task 7):

```cpp
evmc_address resolveCodeAddress(evmc_message const& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
}

bcos::bytes resolveExecutableCodeLegacy(state::State& state, bcos::bytes code, bool eip7702Enabled)
{
    if (!eip7702Enabled || code.empty())
    {
        return code;
    }
    if (auto const delegate = parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
    {
        return state.get_code(*delegate);
    }
    return code;
}

bcos::bytes resolveCodeLegacyPath(state::State& state,
    bcos::evm_standard::RevisionConfig const& cfg, evmc_message const& msg)
{
    if (state::isCreateKind(msg.kind))
    {
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }
    auto const addr = resolveCodeAddress(msg);
    auto code = state.get_code(addr);
    return resolveExecutableCodeLegacy(state, std::move(code), cfg.eip7702);
}
```

- [ ] **Step 1: Write failing tests**

Create `bcos-evm/test/eth/ResolveExecutionCodeTest.cpp`:

1. **`create_returns_initcode`** — EVMC_CREATE with input bytes; legacy path == `resolveExecutionCode`.
2. **`identity_precompile_empty_code`** — address `0x04`, empty code; both return `{}`.
3. **`regular_contract_bytecode`** — non-empty code at arbitrary address; both return same bytes.
4. **`eip7702_delegation_bytecode`** — account code is delegation prefix + target; with `eip7702=true`, both resolve to delegate target code.

Register in `EthTests.cmake`:

```cmake
add_executable(ResolveExecutionCodeTest eth/ResolveExecutionCodeTest.cpp)
target_include_directories(ResolveExecutionCodeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(ResolveExecutionCodeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME ResolveExecutionCode COMMAND ResolveExecutionCodeTest)
```

- [ ] **Step 2: Skip build/test run (deferred to Task 8)**

Test file and CMake registration from Step 1 **must be complete**. Do **not** run cmake/ctest yet.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/test/eth/ResolveExecutionCodeTest.cpp bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): Add ResolveExecutionCode parity oracle tests

EOF
)"
```

---

### Task 2: TopLevel Frame hardening — commit boundary + §4.2 guard + precompile target

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`
- Modify: `bcos-evm/eth/execution/ResolveExecutionCode.h` (only if Task 1 exposed gaps)

**Interfaces:**
- Consumes: `routeMessage`, `resolveExecutionCode`, `transferFrameValue`, `dispatchPrecompile`
- Produces: TopLevel path ready for production `executeMessage` wiring

**Critical parity — commit boundary:**

Pre-PR2 `executeMessage` order on success (L292–332):

```text
install code / markCreated / fixNonceInit
→ sender nonce bump (depth==0 only)
→ gasRefund = state.get_refund()
→ state.commit()
→ finalize_self_destructs()
```

PR1 `runTopLevelExecutionFrame` commits **inside** Frame without sender bump — **must fix before Task 5**.

Change TopLevel success path in `ExecutionFrame.cpp`:

```cpp
// REMOVE ctx.state.commit() from runTopLevelExecutionFrame success branch.
// KEEP ctx.state.revert() on failure branches.
// Nested path unchanged — still commits inside Frame.
```

Adapter (Task 5) will commit after sender nonce bump.

**§4.2 unified guard** — add to `runTopLevelExecutionFrame` precompile step (match Nested):

```cpp
if (!state::isCreateKind(callMessage.kind) &&
    !(isDelegated7702Message(originalMsg) && callMessage.kind != EVMC_CALL))
{
    // dispatchPrecompile ...
}
```

**Precompile target** — prefer routed target when set:

```cpp
auto const target = routed.hasPrecompileTarget ? routed.precompileTarget :
    (state::isZeroAddress(callMessage.code_address) ? callMessage.recipient :
                                                      callMessage.code_address);
```

- [ ] **Step 1: Write failing test for deferred commit**

Add to `ExecutionFrameTest.cpp`:

```cpp
BOOST_AUTO_TEST_CASE(top_level_frame_does_not_commit_before_adapter_nonce_bump)
{
    // Direct runExecutionFrame(TopLevel) on successful CALL with value 0.
    // After Frame returns SUCCESS, state should still be in checkpoint (not committed)
    // so adapter can bump sender nonce then commit.
    // Implementation: set sender nonce=5, run frame, verify nonce still 5 after Frame,
    // then manually commit and verify state diff captures frame effects.
}
```

Use a minimal CALL to empty EOA (non-precompile) that succeeds with minimal gas.

- [ ] **Step 2: Implement TopLevel hardening in `ExecutionFrame.cpp`**

Apply commit deferral, §4.2 guard, precompile target fix.

- [ ] **Step 3: Skip build/test run (deferred to Task 8)**

Test cases from Step 1 **must be written**. Do **not** run cmake/ctest yet.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp bcos-evm/eth/execution/ResolveExecutionCode.h \
  bcos-evm/test/eth/ExecutionFrameTest.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Harden TopLevel ExecutionFrame for executeMessage migration

EOF
)"
```

---

### Task 3: RouteMessage TopLevel parity tests

**Files:**
- Modify: `bcos-evm/test/eth/RouteMessageTest.cpp`

**Interfaces:**
- Consumes: `execution::routeMessage(..., FrameScope::TopLevel)`
- Produces: RR8 gate tests (spec §8.1 PR2)

- [ ] **Step 1: Add TopLevel route parity cases**

Add to `RouteMessageTest.cpp`:

1. **`top_level_call_zero_code_address_fills_recipient`** — zero `code_address` → equals `recipient` (matches `resolveCodeAddress`).
2. **`top_level_call_marks_identity_precompile_target`** — CALL to `0x04` with empty code → `hasPrecompileTarget == true`.
3. **`top_level_create_skips_precompile_target`** — EVMC_CREATE does not set precompile target.

- [ ] **Step 2: Skip build/test run (deferred to Task 8)**

Test file and CMake registration from Step 1 **must be complete**. Do **not** run cmake/ctest yet.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/test/eth/RouteMessageTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): Add RouteMessage TopLevel parity cases for PR2 gate

EOF
)"
```

---

### Task 4: Extract 7702 tx-level auth helper (ExecuteMessage.cpp)

**Files:**
- Modify: `bcos-evm/eth/ExecuteMessage.cpp` (anonymous namespace only — no behavior change)

**Interfaces:**
- Produces: `apply7702TxAuthorizationsIfNeeded(state, input, codeAddress)` callable from thin adapter

Extract L208–223 into:

```cpp
void apply7702TxAuthorizationsIfNeeded(state::State& state, ExecuteMessageInput const& input,
    evmc_address const& codeAddress)
{
    if (state::isCreateKind(input.message.kind))
    {
        return;
    }
    if (!input.revisionConfig.eip7702 || !input.authorizationListPresent ||
        input.authorizations.empty())
    {
        return;
    }
    state.checkpoint();
    if (!state::isZeroAddress(input.message.sender))
    {
        auto const senderNonce = state.get_nonce(input.message.sender);
        state.set_nonce(input.message.sender, senderNonce + 1);
    }
    applyAuthorizations(state, input.authorizations, input.blockInfo.chainId);
    if (input.revisionConfig.warm_access && !state::isZeroAddress(codeAddress))
    {
        warmDelegationTarget(state, codeAddress);
    }
    state.commit();
}
```

- [ ] **Step 1: Extract helper (no call-site change yet)**

Replace inline block with function call at same location.

- [ ] **Step 2: Skip build run (deferred to Task 8)**

Refactor only — no new tests. Do **not** run cmake yet.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/ExecuteMessage.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Extract 7702 tx-level auth helper in ExecuteMessage

EOF
)"
```

---

### Task 5: Wire thin `executeMessage` adapter (production migration)

**Files:**
- Modify: `bcos-evm/eth/ExecuteMessage.cpp`

**Interfaces:**
- Consumes: `runExecutionFrame`, `FrameContext`, `apply7702TxAuthorizationsIfNeeded`, deferred TopLevel commit from Task 2
- Produces: thin adapter per spec §5.1; **removes** direct `dispatchPrecompile` call and inline frame body (L200–340)

- [ ] **Step 1: Replace frame body with Frame delegation**

After host setup and 7702 auth, replace L200–340 with:

```cpp
#include "bcos-evm/eth/execution/EvmCallFrame.h"

// ... inside executeMessage, after apply7702TxAuthorizationsIfNeeded ...

execution::FrameContext frameCtx{state, *input.vm, input.revisionConfig, input.extension,
    txContext.tx_origin, host.execution_address_ref(), input.fixNonceInit};

auto const scope = input.message.depth == 0 ? execution::FrameScope::TopLevel :
                                                execution::FrameScope::Nested;
auto fr = execution::runExecutionFrame(frameCtx, input.message, scope, host);

output.result = std::move(fr.result);
output.logs = host.take_logs();

if (fr.precompileHit)
{
    EVM_LOG(TRACE) << LOG_DESC("executeMessage precompile")
                   << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                   << LOG_KV("gasLeft", output.result.gas_left);
    output.gasRefund = fr.gasRefund;
    output.stateDiff = state.build_diff();
    return output;
}

if (output.result.status_code == EVMC_SUCCESS)
{
    if (input.message.depth == 0 && !state::isZeroAddress(input.message.sender))
    {
        bool const authPrebumped = !isCreateKind(input.message.kind) &&
            input.revisionConfig.eip7702 && input.authorizationListPresent &&
            !input.authorizations.empty();
        if (!authPrebumped)
        {
            state.set_nonce(input.message.sender, state.get_nonce(input.message.sender) + 1);
        }
    }
    output.gasRefund = static_cast<int64_t>(state.get_refund());
    state.commit();
    state.finalize_self_destructs();
    output.stateDiff = state.build_diff();
}
else
{
    output.gasRefund = static_cast<int64_t>(state.get_refund());
    output.stateDiff = state.build_diff();
}
```

Remove `#include "bcos-evm/eth/precompiled/PrecompileRouter.h"` if no longer referenced.

Remove unused locals: `bcos::bytes code`, inline precompile block, `applyTopLevelValueTransfer` call site, CREATE/vm/finalize inline logic.

- [ ] **Step 2: Skip build/test run (deferred to Task 8)**

Test file and CMake registration from Step 1 **must be complete**. Do **not** run cmake/ctest yet.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/ExecuteMessage.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Delegate executeMessage to runExecutionFrame

EOF
)"
```

---

### Task 6: `ExecutionFrameTest` depth=0 production parity

**Files:**
- Modify: `bcos-evm/test/eth/ExecutionFrameTest.cpp`

**Interfaces:**
- Consumes: `executeMessage`, `runExecutionFrame`, envelope test helpers

- [ ] **Step 1: Add depth=0 parity cases**

Add cases mirroring `PrecompileRouterEnvelopeTest`:

1. **`top_level_precompile_insufficient_balance_matches_envelope_test`** — compare `executeMessage` depth0 vs envelope oracle (status, gas, balances).

2. **`top_level_successful_value_transfer_matches_envelope_test`** — same for successful identity precompile transfer.

3. **`top_level_sender_nonce_bump_on_success`** — successful CALL depth=0 bumps sender nonce by 1 (unless 7702 auth prebump).

4. **`top_level_precompile_hit_skips_finalize_self_destructs`** — precompile hit returns early; verify no self-destruct finalize side effects (smoke: empty diff stable).

Reuse `makeBaseInput` / `valueTransferMessage` patterns from `PrecompileRouterEnvelopeTest.cpp`.

- [ ] **Step 2: Skip build/test run (deferred to Task 8)**

Test file and CMake registration from Step 1 **must be complete**. Do **not** run cmake/ctest yet.

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/test/eth/ExecutionFrameTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): Add ExecutionFrame PR2 depth=0 production parity tests

EOF
)"
```

---

### Task 7: Delete dead helpers from `ExecuteMessage.cpp`

**Files:**
- Modify: `bcos-evm/eth/ExecuteMessage.cpp`

Remove now-unused anonymous-namespace helpers:

- `resolveCodeAddress`
- `resolveExecutableCode`
- `applyTopLevelValueTransfer`
- `makeInsufficientBalanceResult` (if Frame returns same status; keep if adapter still uses it for early paths)
- `resolveCreateAddress` (if only used by deleted finalize block — Frame owns this)

Keep: `resolveState`, `buildTxContext`, `toStateTransaction`, `apply7702TxAuthorizationsIfNeeded`, `isCreateKind`.

Verify no `#include` for `PrecompileRouter.h`, `Transfer.h` if unused.

- [ ] **Step 1: Delete dead code**

- [ ] **Step 2: Static check (no compile)**

Run: `rg 'dispatchPrecompile' bcos-evm/eth --glob '!execution/ExecutionFrame.cpp' 2>&1 | rtk err`

Expected: **no matches** in `ExecuteMessage.cpp` or `EthHost.cpp`

- [ ] **Step 3: Skip build run (deferred to Task 8)**

Cleanup only — no new tests. Do **not** run cmake yet.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/ExecuteMessage.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Remove duplicated frame helpers from ExecuteMessage

EOF
)"
```

---

### Task 8: Unified build + test fix + PR2 regression gate

**Files:**
- Fix as needed: any file from Tasks 1–7 that fails compile or tests
- Report: `.superpowers/sdd/task-8-report.md`

**This is the only task that runs cmake/ctest.** Iterate build → test → fix until PR2 gate green.

- [ ] **Step 1: Configure**

Run: `cmake -B build -DTESTS=ON 2>&1 | rtk err`

- [ ] **Step 2: Build all PR2 targets**

Run:

```bash
cmake --build build --target bcos-evm-eth ResolveExecutionCodeTest ExecutionFrameTest \
  RouteMessageTest NestedCallHostTest PragueStateTest -j8 2>&1 | rtk err
```

Fix compile errors in source (Tasks 1–7 code) until build PASS.

- [ ] **Step 3: Seam audit**

Run: `rg '#include "bcos-evm/(bcos|opstack)/' bcos-evm/eth/execution 2>&1 | rtk err`

Expected: no matches

Run: `rg 'dispatchPrecompile' bcos-evm/eth --glob '!execution/ExecutionFrame.cpp' 2>&1 | rtk err`

Expected: no matches in `ExecuteMessage.cpp` or `EthHost.cpp`

- [ ] **Step 4: PR2 parity gate — run and fix until green**

Run: `ctest --test-dir build -R "PrecompileRouterEnvelope|ExecutionFrame|ResolveExecutionCode|RouteMessage|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`

Fix failures in implementation or tests (common: Task 2 commit boundary, Task 5 adapter assembly, ResolveExecutionCode parity).

- [ ] **Step 5: Full eth + cross unit gate**

Run: `ctest --test-dir build -R "PrecompileRouter|Eip|Eth|Tx|ExecuteMessage|DebitIntrinsic|ExecutionFrame|RouteMessage|FrameValueTransfer|ResolveExecutionCode|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`

Note: `ExecuteMessageSmoke` / `EthTransactionExecutorFixture` pre-existing failures are not PR2 regressions if unchanged from baseline.

- [ ] **Step 6: FISCO / OpStack smoke**

Run: `ctest --test-dir build -R "FiscoExecutionBridgeSmoke|Bcos7702|Bcos7212|OpStackExecutionBridgeSmoke" --output-on-failure 2>&1 | rtk err`

Expected: PASS

- [ ] **Step 7: Commit fixes (if any)**

```bash
rtk git add -u
rtk git commit -m "$(cat <<'EOF'
fix(bcos-evm): PR2 unified build and test gate fixes

EOF
)"
```

Only if Step 2–6 required code changes.

- [ ] **Step 8: Record results**

Document build output, test pass/fail counts, fixes applied in `.superpowers/sdd/task-8-report.md`.

---

## Spec Coverage Matrix

| Spec requirement | Task |
| --- | --- |
| §5.1 thin `executeMessage` adapter | Task 5 |
| §4.3 7702 tx-level auth outside Frame | Task 4, Task 5 |
| §7 precompileHit early return + gasRefund asymmetry | Task 5 |
| §7 sender nonce bump + commit + finalize_self_destructs order | Task 2, Task 5, Task 6 |
| §9 PR2 delete dead ExecuteMessage helpers | Task 7 |
| §9 PR2 gate: routeMessage(TopLevel) parity | Task 3 |
| §9 PR2 gate: resolveExecutionCode parity | Task 1, Task 2 |
| §8.1 depth=0 production parity | Task 6 |
| §6 sole Frame dispatchPrecompile call site | Task 5, Task 7 |
| RR7 TopLevel CREATE order preserved | Task 2 (verify), Task 6 |
| RR8 TopLevel route parity | Task 3 |
| §11 success criteria 1 (both adapters delegate) | Task 5 |
| §11 success criteria 4 (seam audit) | Task 8 |
| Unified build + test fix | Task 8 only |

## PR2 Done Checklist

- [ ] `executeMessage` delegates to `runExecutionFrame` (TopLevel for depth=0)
- [ ] No `dispatchPrecompile` in `ExecuteMessage.cpp`
- [ ] Dead helpers removed from `ExecuteMessage.cpp`
- [ ] TopLevel commit deferred to adapter (sender nonce before commit)
- [ ] `ResolveExecutionCodeTest` + RouteMessage TopLevel + ExecutionFrame depth=0 green
- [ ] `PrecompileRouterEnvelope` + cross `PrecompileRouter*` green
- [ ] FISCO/OpStack smoke green
- [ ] Ready for PR3 plan (docs + helper cleanup)

---

## Self-Review Notes

**Placeholder scan:** clean — all tasks name files, commands, and code blocks.

**Type consistency:** `FrameContext` 7-arg ctor with `fixNonceInit` matches PR1 `ExecutionFrame.h`. `RoutedMessage` / `FrameScope` unchanged.

**Risk flagged in plan:** Task 2 commit boundary is the highest-risk parity item; Task 6 test 3 explicitly gates sender nonce ordering.

**Execution policy:** Tasks 1–7 write tests but skip running cmake/ctest; Task 8 unified build + fix loop.

**Known non-goals:** PR3 doc updates, removing `runDepth0`/`runDepth1`, `Transition.cpp` bypass, `ExecuteMessageSmoke` nonce debt fix.

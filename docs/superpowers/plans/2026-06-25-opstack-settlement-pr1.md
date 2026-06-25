# OpStackSettlement PR1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the OpStack normal-path `txData` shadow frame by making `TxPipelineContext` the sole gas/message truth source and routing settlement through `OpStackSettlement::finalizeNormal`.

**Architecture:** Add `OpStackSettlement` deep module in `bcos-evm/opstack/`. Narrow `OpStackTxExecutionData` → `OpStackFeeContext` (drop `m_message`, `m_gasLimit`, `m_state`). Change `buyGas`/`refundGas` to read `ctx`. Remove `txFinalizeGasSettlement` from `OpStackPipelineHookBinder`. Deposit branch: compile-fix only (use `ctx`/`input` for gas), no `finalizeDeposit` yet.

**Tech Stack:** C++20, Boost.Test, CMake/CTest, `bcos-evm-op` static lib, coroutines (`bcos::task::Task`).

**Spec:** `docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md`  
**ADR:** `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

## Global Constraints

- **PR1 scope:** normal L2 path only; deposit logic stays inline in bridge (no `finalizeDeposit`).
- **Seam discipline (ADR-005):** `OpStackSettlement` lives in `opstack/`; do not add `bcos/` includes under `eth/`.
- **Do not change** `runTxPipeline` step order or `eth/orchestration/` kernel.
- **`bcos-evm-op` sources:** `file(GLOB_RECURSE opstack/*.cpp)` — new `OpStackSettlement.cpp` is picked up automatically; only add explicit test target in `test/cmake/OpStackTests.cmake`.
- **Deposit compile-fix allowed;** deposit behavioral refactor deferred to PR2.
- **执行策略（2026-06-24）：** Task 1–8 **跳过编译**；测试用例**必须编写**；Task 10 统一编译并修复问题。

### Build & Test Conventions

From repo root (adjust build dir if yours differs):

```bash
cmake -B build -DTESTS=ON
cmake --build build --target OpStackSettlementTest OpStackPipelineHookBinderTest OpStackIntrinsicGasSyncTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
cd build && ctest -R 'OpStackSettlement|OpStackPipelineHookBinder|OpStackIntrinsicGasSync' --output-on-failure
```

Transaction-executor E2E (Task 8):

```bash
cmake --build build --target TestOpStackTransactionExecutorFixture -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
cd build && ctest -R TestOpStackTransactionExecutorFixture --output-on-failure
```

---

## File Map (PR1)

| File | Responsibility |
| --- | --- |
| `bcos-evm/opstack/OpStackSettlement.h` | `GasPoolHooks`, `OpStackSettlementResult`, `finalizeNormal` declaration |
| `bcos-evm/opstack/OpStackSettlement.cpp` | exitKind table, gas math, refund orchestration, gas pool return |
| `bcos-evm/opstack/OpStackTxFeeLedger.h/.cpp` | `buyGas(ctx, feeCtx)`, `refundGas(ctx, feeCtx)` |
| `bcos-evm/opstack/OpStackPipelineHookBinder.h/.cpp` | floor + intrinsic hooks only; **remove** `applySettlement` |
| `bcos-evm/opstack/OpStackExecutionBridge.cpp` | normal path calls `finalizeNormal`; deposit minimal compile fixes |
| `bcos-evm/test/opstack/OpStackSettlementTest.cpp` | module tests (no evmone) |
| `bcos-evm/test/cmake/OpStackTests.cmake` | register `OpStackSettlementTest` |
| `bcos-evm/test/opstack/OpStackPipelineHookBinderTest.cpp` | remove `post_settle` case; fix `m_gasLimit` references |

---

### Task 1: Characterization — lock normal-path early-exit gas behavior

**Files:**
- Create: `bcos-evm/test/opstack/OpStackSettlementCharacterizationTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Produces: documented expected `gasUsed` / balance deltas for `IntrinsicRejected`, `GasAffordRejected`, `Completed` on **current** code (pre-refactor). Used as oracle in Task 6.

- [ ] **Step 1: Add characterization test (current bridge behavior)**

Create `bcos-evm/test/opstack/OpStackSettlementCharacterizationTest.cpp`:

```cpp
#define BOOST_TEST_MODULE OpStackSettlementCharacterizationTest

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "state/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(characterize_intrinsic_reject_gas_used_is_zero)
{
    // Minimal tx: gas below intrinsic → IntrinsicRejected, gasUsed == 0
    // Use opStackExecute with funded sender; assert output.gasUsed == 0
    // COPY exact expected values into comment block for Task 6 oracle
}
}  // namespace
```

Implement fully (mirror patterns from `OpStackIntrinsicGasSyncTest.cpp`: `InMemoryEvmStateReader`, `makeIsthmusRevisionConfig`, `task::syncWait(opStackExecute(...))`).

Capture at minimum:
- `IntrinsicRejected` → `output.gasUsed == 0`
- `Completed` simple CALL with known gas → `output.gasUsed > 0`

- [ ] **Step 2: Register in OpStackTests.cmake**

```cmake
add_executable(OpStackSettlementCharacterizationTest
    opstack/OpStackSettlementCharacterizationTest.cpp)
target_link_libraries(OpStackSettlementCharacterizationTest PRIVATE bcos-evm-op)
add_test(NAME OpStackSettlementCharacterization COMMAND OpStackSettlementCharacterizationTest)
```

- [ ] **Step 3: Build and run (baseline green)**

```bash
cmake --build build --target OpStackSettlementCharacterizationTest -j8
cd build && ctest -R OpStackSettlementCharacterization --output-on-failure
```

Expected: PASS on **pre-refactor** tree.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/opstack/OpStackSettlementCharacterizationTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "$(cat <<'EOF'
test(opstack): Add settlement characterization oracle for PR1 refactor

EOF
)"
```

---

### Task 2: Introduce `OpStackFeeContext` and `OpStackSettlement` skeleton

**Files:**
- Create: `bcos-evm/opstack/OpStackSettlement.h`
- Create: `bcos-evm/opstack/OpStackSettlement.cpp`
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.h`

**Interfaces:**
- Produces:
  - `struct GasPoolHooks { std::function<bool(uint64_t)> subGas; std::function<void(uint64_t,uint64_t)> returnGas; };`
  - `struct OpStackSettlementResult { int64_t gasUsed; uint64_t gasRemaining; uint64_t maxUsedGas; };`
  - `using OpStackFeeContext = OpStackTxFeeLedger::OpStackFeeContext;` (after rename)
  - `OpStackSettlementResult finalizeNormal(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx, TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool);`

- [ ] **Step 1: Rename struct in OpStackTxFeeLedger.h**

Replace `OpStackTxExecutionData` with `OpStackFeeContext`. **Delete** these members:

```cpp
// DELETE:
evmc_message m_message{};
int64_t m_gasLimit{0};
state::State* m_state{nullptr};
```

Add transitional alias (remove in PR2):

```cpp
using OpStackTxExecutionData = OpStackFeeContext;  // PR1 compat for deposit comments/tests
```

- [ ] **Step 2: Create OpStackSettlement.h**

```cpp
#pragma once
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include <functional>

namespace bcos::evm {

struct GasPoolHooks {
    std::function<bool(uint64_t)> subGas;
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> returnGas;
};

struct OpStackSettlementResult {
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx,
    OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind,
    OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool);

}  // namespace bcos::evm
```

- [ ] **Step 3: Stub OpStackSettlement.cpp**

```cpp
#include "bcos-evm/opstack/OpStackSettlement.h"
namespace bcos::evm {
OpStackSettlementResult finalizeNormal(
    TxPipelineContext const&, OpStackFeeContext&, TxPipelineExitKind,
    OpStackTxFeeLedger&, GasPoolHooks const&)
{
    return {};  // Task 4 fills in
}
}  // namespace bcos::evm
```

- [ ] **Step 4: Build bcos-evm-op (expect compile errors in callers — fixed next tasks)**

```bash
cmake --build build --target bcos-evm-op -j8
```

Note failing files for Task 3–6.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.h bcos-evm/opstack/OpStackSettlement.cpp bcos-evm/opstack/OpStackTxFeeLedger.h
rtk git commit -m "$(cat <<'EOF'
feat(opstack): Add OpStackSettlement skeleton and narrow OpStackFeeContext

EOF
)"
```

---

### Task 3: `buyGas` / `refundGas` read `ctx` (TDD)

**Files:**
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.h`
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.cpp`
- Modify: `bcos-evm/opstack/OpStackPipelineHookBinder.h` (`HookBindingContext` uses `OpStackFeeContext&`)
- Test: `bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp` (new)

**Interfaces:**
- Consumes: `TxPipelineContext` from `eth/orchestration/TxPipelineContext.h`
- Produces:
  - `task::Task<bool> buyGas(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx);`
  - `task::Task<void> refundGas(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx);`

- [ ] **Step 1: Write failing test**

`bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp`:

```cpp
#define BOOST_TEST_MODULE OpStackTxFeeLedgerCtxTest
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "state/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(buyGas_uses_ctx_message_not_fee_context_copy)
{
    bcos::evm::state::test::InMemoryEvmStateReader stateView;
    evmc_address sender{};
    sender.bytes[19] = 0x42;
    stateView.insert_account(sender, bcos::evm::state::Account{.balance = bcos::u256(1'000'000)});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 50'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    bcos::evm::TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    bcos::evm::OpStackFeeContext feeCtx;
    feeCtx.m_gasTipCap = 1;
    feeCtx.m_gasFeeCap = 100;
    feeCtx.m_hasGasFeeCap = true;
    feeCtx.m_blockInfo.baseFee = 0;

    bcos::evm::OpStackTxFeeLedger ledger;
    auto ok = bcos::task::syncWait(ledger.buyGas(ctx, feeCtx));
    BOOST_REQUIRE(ok);
    BOOST_CHECK(stateView.get_account(sender)->balance < bcos::u256(1'000'000));
}
```

Register in `OpStackTests.cmake`.

- [ ] **Step 2: Run test — expect FAIL** (old `buyGas(OpStackTxExecutionData&)` signature)

- [ ] **Step 3: Update signatures and implementation**

`OpStackTxFeeLedger.h`:

```cpp
task::Task<bool> buyGas(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx);
task::Task<void> refundGas(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx);
```

`OpStackTxFeeLedger.cpp` replacements:

| Old | New |
| --- | --- |
| `data.m_state` | `&ctx.state` |
| `data.m_message.sender` | `ctx.message.sender` |
| `data.m_message.value` | `ctx.message.value` |
| `data.m_gasLimit` | `ctx.originalGasLimit` |

Keep `feeCtx` for: `m_effectiveGasPrice`, `m_l1CostCharged`, `m_operatorCostLimit`, `m_gasUsed`, `m_gasRemaining`, `m_evmcResult`, caps, blob, blockInfo.

- [ ] **Step 4: Run test — expect PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackTxFeeLedger.h bcos-evm/opstack/OpStackTxFeeLedger.cpp bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp bcos-evm/test/cmake/OpStackTests.cmake bcos-evm/opstack/OpStackPipelineHookBinder.h
rtk git commit -m "$(cat <<'EOF'
feat(opstack): buyGas/refundGas read TxPipelineContext as truth source

EOF
)"
```

---

### Task 4: Implement `finalizeNormal` (move `applySettlement` + early-exit table)

**Files:**
- Modify: `bcos-evm/opstack/OpStackSettlement.cpp`
- Modify: `bcos-evm/opstack/OpStackPipelineHookBinder.cpp` (move logic from `applySettlement`)
- Delete usage: `OpStackPipelineHookBinder::applySettlement` (implementation moves to settlement)

**Interfaces:**
- Consumes: `postExecuteGasSettlement` from `opstack/fee/OpStackGasSettlement.h`
- Produces: `finalizeNormal` full implementation

- [ ] **Step 1: Write failing OpStackSettlementTest — Completed path**

`bcos-evm/test/opstack/OpStackSettlementTest.cpp`:

```cpp
#define BOOST_TEST_MODULE OpStackSettlementTest
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(finalize_normal_completed_matches_post_execute_settlement)
{
    bcos::evm::state::test::InMemoryEvmStateReader stateView;
    evmc_message msg{};
    msg.gas = 100'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    bcos::evm::TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 5'000;
    ctx.evmcResult = bcos::evm::EVMCResult(raw, bcos::protocol::TransactionStatus::None);
    ctx.exitKind = bcos::evm::TxPipelineExitKind::Completed;

    bcos::evm::OpStackFeeContext feeCtx;
    feeCtx.m_floorDataGas = 0;
    bcos::evm::OpStackTxFeeLedger ledger;
    bcos::evm::GasPoolHooks pool{};

    auto result = bcos::evm::finalizeNormal(
        ctx, feeCtx, ctx.exitKind, ledger, pool);

    auto const stateRefund = revision.eip1559 ? 5'000u : 0u;
    auto const expected = bcos::evm::postExecuteGasSettlement(100'000u, 80'000u, stateRefund, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
}
```

- [ ] **Step 2: Run — expect FAIL**

- [ ] **Step 3: Implement finalizeNormal**

Port from `OpStackPipelineHookBinder::applySettlement` + bridge early-exit block:

```cpp
OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool)
{
    OpStackSettlementResult out{};
    if (exitKind == TxPipelineExitKind::IntrinsicRejected ||
        exitKind == TxPipelineExitKind::GasAffordRejected)
    {
        feeCtx.m_gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        feeCtx.m_gasUsed = 0;
        out.gasUsed = 0;
        out.gasRemaining = feeCtx.m_gasRemaining;
        // full buyGas refund via refundGas in bridge after return — see Task 5
        return out;
    }

    if (exitKind == TxPipelineExitKind::Completed)
    {
        auto const stateRefund = ctx.revisionConfig.eip1559 ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) : 0u;
        auto const settlement = postExecuteGasSettlement(
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)),
            stateRefund, feeCtx.m_floorDataGas);
        feeCtx.m_gasRemaining = settlement.gasRemaining;
        feeCtx.m_maxUsedGas = settlement.maxUsedGas;
        feeCtx.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
        out.gasUsed = feeCtx.m_gasUsed;
        out.gasRemaining = settlement.gasRemaining;
        out.maxUsedGas = settlement.maxUsedGas;
        return out;
    }

    // RulesRejected / ExceptionHandled: port current bridge else-branch (applySettlement)
    // Use characterization oracle from Task 1
    ...
}
```

- [ ] **Step 4: Add intrinsic_reject test**

```cpp
BOOST_AUTO_TEST_CASE(finalize_normal_intrinsic_reject_gas_used_zero)
{
    // ctx.exitKind = IntrinsicRejected → result.gasUsed == 0
}
```

- [ ] **Step 5: Register OpStackSettlementTest in OpStackTests.cmake; run PASS**

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.cpp bcos-evm/test/opstack/OpStackSettlementTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(opstack): Implement finalizeNormal settlement module

EOF
)"
```

---

### Task 5: Wire `OpStackExecutionBridge` normal path

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecutionBridge.cpp`
- Modify: `bcos-evm/opstack/OpStackPipelineHookBinder.cpp`

**Interfaces:**
- Consumes: `finalizeNormal`, `buyGas(ctx, feeCtx)`, `GasPoolHooks`

- [ ] **Step 1: Remove hook settlement from OpStackPipelineHookBinder.cpp**

Delete `applySettlement` function body and `hooks.txFinalizeGasSettlement` assignment.

Remove `applySettlement` declaration from `OpStackPipelineHookBinder.h`.

- [ ] **Step 2: Refactor normal branch in OpStackExecutionBridge.cpp**

Replace `OpStackTxExecutionData txData` init — stop copying `m_message`, `m_gasLimit`, `m_state`:

```cpp
OpStackFeeContext feeCtx;
feeCtx.m_call = input.call;
// ... rollup, caps, blockInfo, flags (no message/gasLimit/state)
```

Normal path after `runTxPipeline`:

```cpp
GasPoolHooks gasPool{
    .subGas = input.gasPoolSubGasHook,
    .returnGas = input.gasPoolReturnGasHook,
};

auto buyGasOk = co_await input.opTxExecutor.buyGas(ctx, feeCtx);
// ...

runTxPipeline(ctx, hooks, errorPolicy);

auto settled = finalizeNormal(ctx, feeCtx, ctx.exitKind, input.opTxExecutor, gasPool);
co_await input.opTxExecutor.refundGas(ctx, feeCtx);

output.gasUsed = settled.gasUsed;
// guard.gasRemaining / gasUsed from settled
```

Remove inline early-exit if/else (lines 194–209) and `GasPoolReturnGuard` manual field writes — `finalizeNormal` + `refundGas` own settlement; keep guard only if still needed for exceptional early return before finalize (document in code comment).

- [ ] **Step 3: Deposit branch compile-fix only**

Replace `txData.m_gasLimit` with `ctx.originalGasLimit` or `input.message.gas`:

```cpp
// was: txData.m_gasUsed = std::max<int64_t>(0, txData.m_gasLimit);
feeCtx.m_gasUsed = std::max<int64_t>(0, ctx.originalGasLimit);
```

Update `returnDepositPoolGas(input, feeCtx)` signature if it used `m_gasLimit`.

- [ ] **Step 4: Build and run OpStack tests**

```bash
cmake --build build --target bcos-evm-op OpStackIntrinsicGasSyncTest OpStackSettlementCharacterizationTest -j8
cd build && ctest -R 'OpStack' --output-on-failure
```

Expected: characterization + intrinsic sync still PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackExecutionBridge.cpp bcos-evm/opstack/OpStackPipelineHookBinder.cpp bcos-evm/opstack/OpStackPipelineHookBinder.h
rtk git commit -m "$(cat <<'EOF'
feat(opstack): Wire finalizeNormal; remove hook settlement from pipeline

EOF
)"
```

---

### Task 6: Fix downstream tests and callers

**Files:**
- Modify: `bcos-evm/test/opstack/OpStackPipelineHookBinderTest.cpp`
- Modify: any file referencing `buyGas(txData)` or `m_gasLimit` on fee context (grep `OpStackTxExecutionData`, `applySettlement`)

- [ ] **Step 1: Update OpStackPipelineHookBinderTest**

- Remove `post_settle_updates_tx_data_gas` (covered by `OpStackSettlementTest`).
- In `pre_debit_entry_floor_rejects`: remove `txData.m_gasLimit = ...`; rely on `ctx.message.gas` only (floor precheck reads `orchestrationCtx.message`).

- [ ] **Step 2: Grep and fix**

```bash
rtk grep -r "OpStackTxExecutionData\|applySettlement\|buyGas(txData\|m_gasLimit" bcos-evm/
```

Fix `OpStackIntrinsicGasSyncTest` / smoke tests if they set removed fields.

- [ ] **Step 3: Run full opstack ctest suite**

```bash
cd build && ctest -R OpStack --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/opstack/
rtk git commit -m "$(cat <<'EOF'
test(opstack): Update hook binder tests for OpStackSettlement PR1

EOF
)"
```

---

### Task 7: Transaction-executor E2E smoke

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` (only if existing tests fail; otherwise add one assertion)

- [ ] **Step 1: Run existing fixture**

```bash
cmake --build build --target TestOpStackTransactionExecutorFixture -j8
cd build && ctest -R TestOpStackTransactionExecutorFixture --output-on-failure
```

- [ ] **Step 2: If no normal-path gas assertion, add to existing success case**

In `operator_fee_recipient_gets_fee_on_success` or similar:

```cpp
BOOST_REQUIRE(receipt->gasUsed() > 0);
// sender balance decreased vs pre-state
```

- [ ] **Step 3: Commit if changed**

```bash
rtk git add transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp
rtk git commit -m "$(cat <<'EOF'
test(te): Assert gasUsed on OpStack normal executor path

EOF
)"
```

---

### Task 8: Documentation and ADR status

**Files:**
- Modify: `docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md`
- Modify: `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

- [ ] **Step 1: Mark spec implemented (PR1)**

```markdown
**Status:** Implemented (PR1)
**Implementation:** `OpStackSettlement::{finalizeNormal}`, commits `<hash>`
```

- [ ] **Step 2: ADR-021 status → Implemented**

- [ ] **Step 3: Close spec §10 open items** (GasPoolReturnGuard resolved; characterization done)

- [ ] **Step 4: Commit**

```bash
rtk git add docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md
rtk git commit -m "$(cat <<'EOF'
docs(opstack): Mark ADR-021 OpStack settlement PR1 implemented

EOF
)"
```

---

### Task 9: PR1 regression gate

> **编译步骤已合并至 Task 10。** Task 1–8 仅编写代码与测试，不执行 build/ctest。

- [ ] **Step 1: Compliance checklist** (spec §11)

- [ ] Normal path: no `feeCtx.m_message` / `m_gasLimit` / `m_state` reads
- [ ] No `applySettlement` call sites
- [ ] `finalizeNormal` sole normal settlement entry
- [ ] `eth/` unchanged

---

### Task 10: 统一编译并修复测试问题

**策略：** Task 1–8 跳过所有 `cmake`/`ctest` 步骤；本 Task 一次性构建并修复编译/链接/测试失败。

**Files:** 视编译错误而定（预期：`bcos-evm-op`、OpStack 相关 test target、TE fixture）

- [ ] **Step 1: 配置并构建**

```bash
cmake -B build -DTESTS=ON
cmake --build build --target bcos-evm-op \
  OpStackSettlementTest OpStackTxFeeLedgerCtxTest \
  OpStackSettlementCharacterizationTest OpStackPipelineHookBinderTest \
  OpStackIntrinsicGasSyncTest RefundIsthmusTest BlobGasBalanceTest \
  TestOpStackTransactionExecutorFixture -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```

- [ ] **Step 2: 运行 OpStack 回归**

```bash
cd build && ctest -R 'OpStackSettlement|OpStackTxFeeLedgerCtx|OpStackPipelineHookBinder|OpStackIntrinsicGasSync|OpStackSettlementCharacterization|RefundIsthmus|BlobGasBalance' --output-on-failure
```

- [ ] **Step 3: TE E2E**

```bash
cd build && ctest -R TestOpStackTransactionExecutorFixture --output-on-failure
```

- [ ] **Step 4: 修复所有编译/测试失败直至全绿**

- [ ] **Step 5: Commit（用户请求时）**

```bash
rtk git add bcos-evm/ docs/superpowers/
rtk git commit -m "$(cat <<'EOF'
feat(opstack): OpStackSettlement PR1 — ctx single source for normal path

EOF
)"
```

---

## Spec Self-Review

| Spec requirement | Task |
| --- | --- |
| `finalizeNormal` deep module | Task 2, 4 |
| `ctx` gas truth source | Task 3, 4, 5 |
| Delete shadow fields | Task 2 |
| Remove hook settlement | Task 5 |
| Deposit PR1 untouched behavior | Task 5 compile-fix only |
| G3 tests | Task 1, 4, 6, 7 |
| ADR-021 | Task 8 |
| Characterization before refactor | Task 1 |
| PR2 `finalizeDeposit` | Out of scope |

No TBD placeholders in task steps.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-25-opstack-settlement-pr1.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?

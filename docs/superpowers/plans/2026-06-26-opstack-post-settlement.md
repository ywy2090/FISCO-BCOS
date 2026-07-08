# OpStack Post-Settlement Plan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deepen OpStack post-execute fee settlement into `planOpStackPostSettlement` (ADR-026 Appendix C), eliminate `projectNormalReceiptMeta` double `operatorCostFunc` invocation, and thin `OpStackTxFeeLedger::refundGas` to gate → plan → apply → return plan.

**Architecture:** Mirror Appendix B (`OpStackPreDebitPlan`): sync State-free plan module + view mapper + thin ledger adapter. `settleNormal` returns `NormalSettleOutcome { settled, feePlan }`; receipt projection reads plan fields only. Single PR, zero behavior change outside composition locality.

**Tech Stack:** C++17+, CMake 3.28, Boost.Test, `bcos-evm-op` static lib, `bcos-task` coroutines, `eth/gas/TxFeeSettlement.h`.

**Spec / ADR:** `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md` Appendix C (§C.1–C.10)

## Global Constraints

- `planOpStackPostSettlement` **must not** `#include` `State`, `TxPipelineContext`, `OpStackSettlementView.h`, or `eth/` orchestration headers beyond `TxFeeSettlement` / `FeeInputsProjection`.
- Post path **must not** invoke `l1CostFunc`; `l1FeeRouted` is passthrough of `inputs.l1CostCharged` (sidecar snapshot from `buyGas`).
- `operatorCostFunc` invoked **at most once** per normal tx (inside plan); `projectNormalReceiptMeta` **must not** call the hook.
- Skip guards (`deposit`, zero-fee `call`) stay in `refundGas` adapter only; return default-constructed `OpStackPostSettlementPlan{}`.
- Plan does **not** read fork schedule; `isOpStackIsthmus` gates receipt `operatorFee` write only (unchanged orchestration policy).
- ADR-025 abort path **must not** call `refundGas` (`isNormalPreExecutionReject` → `abortNormalAfterBuyGas` only).
- New `opstack/fee/*.cpp` picked up by `file(GLOB_RECURSE)` in `bcos-evm/CMakeLists.txt` — no main-library CMake edit unless glob excludes `fee/`.
- `eth/` **must not** gain new OP-specific fields.

## File Structure Map

| File | Responsibility |
| --- | --- |
| `bcos-evm/opstack/fee/OpStackPostSettlementPlan.h` | Types, `planOpStackPostSettlement` declaration; reuses `OpStackFeeHooks` from PreDebit header |
| `bcos-evm/opstack/fee/OpStackPostSettlementPlan.cpp` | Plan implementation |
| `bcos-evm/opstack/fee/OpStackPostSettlementInputs.h` | `toOpStackPostSettlementInputs(view, settled)` mapper |
| `bcos-evm/opstack/OpStackTxFeeLedger.h/.cpp` | `refundGas` → returns plan; delete `refundIsthmusOperatorCost` |
| `bcos-evm/opstack/OpStackNormalFeeSettlement.cpp` | `NormalSettleOutcome`, `settleNormal`, thinned `projectNormalReceiptMeta` |
| `bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp` | Plan oracle matrix (incl. ex-RefundIsthmus) |
| `bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp` | Integration routing; update for `refundGas` return type |
| `bcos-evm/test/cmake/OpStackTests.cmake` | Add characterization target; remove `RefundIsthmusTest`; add `.cpp` to `OpStackIntrinsicGasSyncTest` source list |
| `.github/workflows/capability-gate.yml` | Replace `RefundIsthmus` ctest filter with `OpStackPostSettlementCharacterization` |

---

### Task 1: Plan module + characterization tests (TDD)

**Files:**
- Create: `bcos-evm/opstack/fee/OpStackPostSettlementPlan.h`
- Create: `bcos-evm/opstack/fee/OpStackPostSettlementPlan.cpp`
- Create: `bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Produces: `OpStackPostSettlementInputs`, `OpStackPostSettlementPlan`, `planOpStackPostSettlement(inputs, hooks) noexcept`

- [ ] **Step 1: Create plan header**

```cpp
// bcos-evm/opstack/fee/OpStackPostSettlementPlan.h
#pragma once

#include "bcos-evm/eth/eip/TxFeeSettlement.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"  // OpStackFeeHooks
#include <bcos-utilities/Common.h>

namespace bcos::evm {

struct OpStackPostSettlementInputs {
    gas::FeeInputs fee;
    int64_t gasUsed{0};
    int64_t gasRemaining{0};
    uint64_t blockTime{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
};

struct OpStackPostSettlementPlan {
    gas::FeeSettlementPlan core1559;
    bcos::u256 l1FeeRouted{0};
    bcos::u256 operatorFeeCharged{0};
    bcos::u256 senderOperatorRefund{0};
};

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs,
    OpStackFeeHooks const& hooks) noexcept;

}  // namespace bcos::evm
```

- [ ] **Step 2: Write failing characterization test**

Create `bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp` with oracle `oraclePreRefactorRefundGas` mirroring current `OpStackTxFeeLedger.cpp` lines 104–127 (1559 + L1 passthrough + operator charge + Isthmus refund). Include cases from ADR §C.9 matrix. Minimal starter:

```cpp
#define BOOST_TEST_MODULE OpStackPostSettlementCharacterizationTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/TxFeeSettlement.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include <boost/test/included/unit_test.hpp>
#include <functional>

namespace bcos::evm::test {
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::planPostExecution;
using bcos::evm_standard::revisionConfigFromRevision;

namespace {
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);

OpStackPostSettlementPlan oraclePreRefactorRefundGas(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPostSettlementPlan oracle;
    oracle.core1559 =
        planPostExecution(inputs.fee, inputs.gasUsed, inputs.gasRemaining);
    oracle.l1FeeRouted = inputs.l1CostCharged;
    if (hooks.operatorCostFunc != nullptr)
    {
        auto const used = static_cast<uint64_t>(std::max<int64_t>(0, inputs.gasUsed));
        oracle.operatorFeeCharged = (*hooks.operatorCostFunc)(used, inputs.blockTime);
        if (oracle.operatorFeeCharged < inputs.operatorCostLimit)
        {
            oracle.senderOperatorRefund =
                inputs.operatorCostLimit - oracle.operatorFeeCharged;
        }
    }
    return oracle;
}

void assertPlansEqual(
    OpStackPostSettlementPlan const& plan, OpStackPostSettlementPlan const& oracle)
{
    BOOST_CHECK_EQUAL(plan.core1559.unusedRefund, oracle.core1559.unusedRefund);
    BOOST_CHECK_EQUAL(plan.core1559.coinbaseTip, oracle.core1559.coinbaseTip);
    BOOST_CHECK_EQUAL(plan.core1559.baseFeeAmount, oracle.core1559.baseFeeAmount);
    BOOST_CHECK_EQUAL(plan.l1FeeRouted, oracle.l1FeeRouted);
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, oracle.operatorFeeCharged);
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, oracle.senderOperatorRefund);
}
}  // namespace

BOOST_AUTO_TEST_CASE(isthmus_refund_limit_minus_used)  // ex-RefundIsthmusTest
{
    std::function<bcos::u256(uint64_t, uint64_t)> op = [](uint64_t gas, uint64_t) {
        return bcos::u256(gas + 1000);
    };
    OpStackFeeHooks hooks{.operatorCostFunc = &op};
    OpStackPostSettlementInputs inputs{
        .fee = FeeInputs{.revision = kLondon, .gasLimit = 1'618},
        .gasUsed = 500,
        .gasRemaining = 1'118,
        .blockTime = 1,
        .operatorCostLimit = 2'618,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    assertPlansEqual(plan, oraclePreRefactorRefundGas(inputs, hooks));
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, bcos::u256(1'500));
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, bcos::u256(1'118));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
```

Add full matrix cases: type-2 1559, legacy gasPrice, L1 passthrough, null hooks, operator-only hook.

- [ ] **Step 3: Register test in CMake**

In `bcos-evm/test/cmake/OpStackTests.cmake`, after `OpStackPreDebitCharacterizationTest` block, add:

```cmake
add_executable(OpStackPostSettlementCharacterizationTest opstack/OpStackPostSettlementCharacterizationTest.cpp)
target_include_directories(OpStackPostSettlementCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackPostSettlementCharacterizationTest PRIVATE bcos-evm-op)
add_test(NAME OpStackPostSettlementCharacterization COMMAND OpStackPostSettlementCharacterizationTest)
```

- [ ] **Step 4: Run test — expect FAIL (undefined plan)**

```bash
cd build && cmake --build . --target OpStackPostSettlementCharacterizationTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./bcos-evm/test/OpStackPostSettlementCharacterizationTest
```

Expected: link error or undefined `planOpStackPostSettlement`.

- [ ] **Step 5: Implement plan**

```cpp
// bcos-evm/opstack/fee/OpStackPostSettlementPlan.cpp
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include <algorithm>

namespace bcos::evm {

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPostSettlementPlan plan;
    plan.core1559 =
        gas::planPostExecution(inputs.fee, inputs.gasUsed, inputs.gasRemaining);
    plan.l1FeeRouted = inputs.l1CostCharged;

    if (hooks.operatorCostFunc != nullptr)
    {
        auto const used = static_cast<uint64_t>(std::max<int64_t>(0, inputs.gasUsed));
        plan.operatorFeeCharged = (*hooks.operatorCostFunc)(used, inputs.blockTime);
        if (plan.operatorFeeCharged < inputs.operatorCostLimit)
        {
            plan.senderOperatorRefund = inputs.operatorCostLimit - plan.operatorFeeCharged;
        }
    }
    return plan;
}

}  // namespace bcos::evm
```

- [ ] **Step 6: Run characterization — expect PASS**

```bash
./bcos-evm/test/OpStackPostSettlementCharacterizationTest
```

- [ ] **Step 7: Commit**

```bash
git add bcos-evm/opstack/fee/OpStackPostSettlementPlan.h \
        bcos-evm/opstack/fee/OpStackPostSettlementPlan.cpp \
        bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp \
        bcos-evm/test/cmake/OpStackTests.cmake
git commit -m "feat(opstack): add OpStackPostSettlementPlan with characterization tests"
```

---

### Task 2: View → inputs mapper

**Files:**
- Create: `bcos-evm/opstack/fee/OpStackPostSettlementInputs.h`
- Modify: `bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp` (optional mapper smoke)

**Interfaces:**
- Consumes: `OpStackSettlementView`, `OpStackSettlementResult` (from `OpStackSettlement.h`)
- Produces: `toOpStackPostSettlementInputs(view, settled) noexcept`

- [ ] **Step 1: Create mapper header**

```cpp
// bcos-evm/opstack/fee/OpStackPostSettlementInputs.h
#pragma once

#include "bcos-evm/eth/pipeline/FeeInputsProjection.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementView.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"

namespace bcos::evm {

inline OpStackPostSettlementInputs toOpStackPostSettlementInputs(
    OpStackSettlementView const& view, OpStackSettlementResult const& settled) noexcept
{
    auto const& ctx = view.pipelineContext();
    auto const& sidecar = view.feeSidecar();
    return OpStackPostSettlementInputs{
        .fee = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
            gas::FeeCapsView{ctx.gasPrice, view.gasTipCap(), view.gasFeeCap(),
                view.web3TypedTxKind(), view.hasGasFeeCap()},
            ctx.originalGasLimit),
        .gasUsed = settled.gasUsed,
        .gasRemaining = settled.gasRemaining,
        .blockTime = static_cast<uint64_t>(view.blockInfo().timestamp),
        .l1CostCharged = sidecar.l1CostCharged,
        .operatorCostLimit = sidecar.operatorCostLimit,
    };
}

}  // namespace bcos::evm
```

- [ ] **Step 2: Add characterization case — effectiveGasPrice vs sidecar**

After `buyGas` would have run, assert for a fixture with pre-filled sidecar:

```cpp
BOOST_AUTO_TEST_CASE(post_effective_gas_price_matches_sidecar_snapshot)
{
    auto const fee = makeType2FeeInputs();  // reuse helper from PreDebit test pattern
    OpStackPostSettlementInputs inputs{
        .fee = fee,
        .gasUsed = 120'000,
        .gasRemaining = 380'000,
        .l1CostCharged = 42'000,
        .operatorCostLimit = 1'000'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, {});
  // sidecar.effectiveGasPrice after buyGas equals plan.core1559.effectiveGasPrice
    BOOST_CHECK_EQUAL(
        plan.core1559.effectiveGasPrice,
        gas::planPreExecution(fee).effectiveGasPrice);
}
```

- [ ] **Step 3: Run characterization — expect PASS**

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/opstack/fee/OpStackPostSettlementInputs.h \
        bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp
git commit -m "feat(opstack): add OpStackPostSettlementInputs mapper"
```

---

### Task 3: Wire `refundGas` adapter

**Files:**
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.h`
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.cpp`
- Modify: `bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake` (add `OpStackPostSettlementPlan.cpp` to `OpStackIntrinsicGasSyncTest` source list line ~31)

**Interfaces:**
- Consumes: `planOpStackPostSettlement`, `toOpStackPostSettlementInputs`
- Produces: `task::Task<OpStackPostSettlementPlan> refundGas(view, settled)`

- [ ] **Step 1: Update header — new signature, remove Isthmus method**

```cpp
// OpStackTxFeeLedger.h
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"

task::Task<OpStackPostSettlementPlan> refundGas(
    OpStackSettlementView& view, OpStackSettlementResult const& settled);
// DELETE: refundIsthmusOperatorCost
```

- [ ] **Step 2: Rewrite `refundGas` implementation**

```cpp
task::Task<OpStackPostSettlementPlan> OpStackTxFeeLedger::refundGas(
    OpStackSettlementView& view, OpStackSettlementResult const& settled)
{
    if (view.isDeposit())
    {
        co_return OpStackPostSettlementPlan{};
    }
    if (view.isCall() && view.skipTransactionChecks() && view.noBaseFee() &&
        view.gasFeeCap() == 0 && view.gasTipCap() == 0)
    {
        co_return OpStackPostSettlementPlan{};
    }

    auto& ctx = view.pipelineContext();
    auto& state = ctx.state;

    OpStackFeeHooks hooks{};
    if (m_operatorCostFunc)
    {
        hooks.operatorCostFunc = &m_operatorCostFunc;
    }

    auto const plan = planOpStackPostSettlement(toOpStackPostSettlementInputs(view, settled), hooks);

    addBalance(state, ctx.message.sender,
        plan.core1559.unusedRefund + plan.senderOperatorRefund);
    addBalance(state, view.blockInfo().coinbase, plan.core1559.coinbaseTip);
    addBalance(state, m_baseFeeRecipient, plan.core1559.baseFeeAmount);
    addBalance(state, m_l1FeeRecipient, plan.l1FeeRouted);
    if (hooks.operatorCostFunc != nullptr)
    {
        addBalance(state, m_operatorFeeRecipient, plan.operatorFeeCharged);
    }

    co_return plan;
}
```

Delete entire `refundIsthmusOperatorCost` function.

- [ ] **Step 3: Update `OpStackTxFeeLedgerCtxTest.cpp`**

Replace:

```cpp
task::syncWait(executor.refundGas(view, settled));
```

With (discard return or assign to verify non-empty):

```cpp
auto const feePlan = task::syncWait(executor.refundGas(view, settled));
(void)feePlan;
```

Balance assertions unchanged — integration oracle.

- [ ] **Step 4: Add `OpStackPostSettlementPlan.cpp` to intrinsic sync test source list**

In `OpStackTests.cmake` `OpStackIntrinsicGasSyncTest` sources, after `OpStackPreDebitPlan.cpp`:

```cmake
    ../opstack/fee/OpStackPostSettlementPlan.cpp
```

- [ ] **Step 5: Run integration tests**

```bash
cd build && cmake --build . --target OpStackTxFeeLedgerCtxTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./bcos-evm/test/OpStackTxFeeLedgerCtxTest
```

Expected: PASS (balances unchanged).

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/opstack/OpStackTxFeeLedger.h bcos-evm/opstack/OpStackTxFeeLedger.cpp \
        bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
git commit -m "feat(opstack): delegate refundGas to OpStackPostSettlementPlan"
```

---

### Task 4: Wire `OpStackNormalFeeSettlement` + receipt projection

**Files:**
- Modify: `bcos-evm/opstack/OpStackNormalFeeSettlement.cpp`

**Interfaces:**
- Consumes: `OpStackPostSettlementPlan` from `refundGas`
- Produces: `NormalSettleOutcome`, updated `projectNormalReceiptMeta(..., feePlan)`

- [ ] **Step 1: Add `NormalSettleOutcome` and update `settleNormal`**

```cpp
struct NormalSettleOutcome {
    OpStackSettlementResult settled;
    OpStackPostSettlementPlan feePlan;
};

task::Task<NormalSettleOutcome> settleNormal(
    OpStackSettlementView view, TxPipelineExitKind exitKind,
    OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool)
{
    auto& ctx = view.pipelineContext();
    auto settled = finalizeNormal(ctx, view.feeSidecar(), exitKind);
    auto feePlan = co_await ledger.refundGas(view, settled);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(settled.gasRemaining,
            static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return NormalSettleOutcome{.settled = settled, .feePlan = feePlan};
}
```

- [ ] **Step 2: Thin `projectNormalReceiptMeta`**

```cpp
void projectNormalReceiptMeta(OpStackMessageResult& output, OpStackSettlementView& view,
    OpStackFeeParams const& feeParams, OpStackSettlementResult const& settled,
    OpStackPostSettlementPlan const& feePlan)
{
    auto const& input = view.input;
    output.receiptMeta.l1Fee = feePlan.l1FeeRouted;
    if (isOpStackIsthmus(input.forkSchedule, view.blockInfo().timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        output.receiptMeta.operatorFee = feePlan.operatorFeeCharged;
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
}
```

Remove `settled.gasUsed` hook call and `sidecar.l1CostCharged` read.

- [ ] **Step 3: Update `completeAfterPipeline`**

```cpp
auto outcome = co_await settleNormal(view, ctx.exitKind, ledger, gasPool);
output.gasUsed = outcome.settled.gasUsed;
projectNormalReceiptMeta(output, view, feeParams, outcome.settled, outcome.feePlan);
output.stateDiff = ctx.state.build_diff();
```

- [ ] **Step 4: Run ADR-025 + settlement tests**

```bash
cd build && cmake --build . --target OpStackNormalFeeSettlementTest OpStackSettlementTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./bcos-evm/test/OpStackNormalFeeSettlementTest
./bcos-evm/test/OpStackSettlementTest
```

Expected: PASS (phantom-fee matrix green).

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/opstack/OpStackNormalFeeSettlement.cpp
git commit -m "feat(opstack): consume post-settlement plan in receipt projection"
```

---

### Task 5: Remove `RefundIsthmusTest` + CI filter

**Files:**
- Delete: `bcos-evm/test/opstack/RefundIsthmusTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake` (remove RefundIsthmus block lines ~301–319)
- Modify: `.github/workflows/capability-gate.yml`

- [ ] **Step 1: Delete test file and CMake target**

Remove from `OpStackTests.cmake`:

```cmake
set(REFUND_ISTHMUS_TEST_BINARY_NAME RefundIsthmusTest)
...
add_test(NAME RefundIsthmus ...)
```

Delete `bcos-evm/test/opstack/RefundIsthmusTest.cpp`.

- [ ] **Step 2: Update capability gate**

In `.github/workflows/capability-gate.yml`, replace `RefundIsthmus` with `OpStackPostSettlementCharacterization` in the `ctest -R` pattern.

- [ ] **Step 3: Run full OpStack gate**

```bash
cd build && ctest -R 'OpStack|L1Block|Deposit|Blob|7702|OpStackPostSettlementCharacterization|L1Attributes' --output-on-failure
```

Expected: all matched tests PASS.

- [ ] **Step 4: Commit**

```bash
git add -A bcos-evm/test/opstack/RefundIsthmusTest.cpp bcos-evm/test/cmake/OpStackTests.cmake .github/workflows/capability-gate.yml
git commit -m "test(opstack): migrate RefundIsthmus to post-settlement characterization"
```

---

### Task 6: ADR compliance + docs

**Files:**
- Modify: `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md` (check Appendix C §C.10 boxes)
- Modify: `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` §8 (already has pointer — verify)

- [ ] **Step 1: Mark Appendix C compliance checklist items `[x]`**

- [ ] **Step 2: Run cross characterization (no regression)**

```bash
cd build && ./bcos-evm/test/FeeSettlementCharacterizationTest
```

- [ ] **Step 3: Final commit**

```bash
git add bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md
git commit -m "docs(adr): mark OpStack post-settlement Appendix C complete"
```

---

## Spec Self-Review

| Appendix C requirement | Task |
| --- | --- |
| C-D1 pure amount oracle | Task 1, 4 |
| C-D2 inputs + mapper | Task 2 |
| C-D3 fork outside plan | Task 4 (`isOpStackIsthmus` in receipt only) |
| C-D4 delete `refundIsthmusOperatorCost` | Task 3, 5 |
| C-D5 single PR | All tasks sequential in one branch |
| C-D6 `refundGas` returns plan | Task 3, 4 |
| C-D7 skip → empty plan | Task 3 |
| C-D8 L1 passthrough | Task 1, 2 |
| C.6.1 `NormalSettleOutcome` | Task 4 |
| C.6.3 ADR-025 negative | Task 4 gate |
| C.9 test matrix | Task 1, 5 |
| Delete `RefundIsthmusTest` | Task 5 |

No placeholders remain. Type `refundGas` return consistent across Tasks 3–4.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-26-opstack-post-settlement.md`. Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?

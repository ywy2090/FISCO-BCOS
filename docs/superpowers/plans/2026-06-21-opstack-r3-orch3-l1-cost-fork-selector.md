# R3-ORCH-3 L1 Cost Fork 选择器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 引入 `OpStackForkSchedule` + per-block cache 工厂，替换 `OpStackExecuteViaHost` 硬编码 `l1CostFjord`，闭合 R3-ORCH-3；Isthmus+ 行为零回归。

**Architecture:** `OpStackForkSchedule.h` 门控 fork 时间；`OpStackFee` 内共享 cache helper 实现 `select*`（单测）与 `wire*WithState`（生产，cache miss 重读 `loadOpStackFeeParams`）；同 PR 删除 `m_isIsthmus` 全路径。

**Tech Stack:** C++17, Boost.Test, bcos-evm-op, evmc, CMake/CTest

**Spec:** `docs/superpowers/specs/2026-06-21-opstack-r3-orch3-l1-cost-fork-selector-design.md`

**Review note (2026-06-21):** 计划经 multi-agent grill 修订 — Task 3 拆 3A/3B；FIX-04 工厂测试用专用 params；补 §9 缺失用例。

## Global Constraints

- Phase 1 合法 schedule：**仅** `makeIsthmusPlusForkSchedule()` → `{fjordTime=0, isthmusTime=0}`（`std::optional<uint64_t>`）
- pre-Fjord L1 → closure invoke 抛 `std::invalid_argument("OpStack: pre-Fjord L1 cost unsupported")`；operator pre-Isthmus → **恒零，不 throw**
- **禁止捕获栈上 `OpStackFeeParams&`**；`select*` 必须 value-capture snapshot。`wire*` **允许**捕获 `StateView const&`（生命周期 ⊆ 单次 `opStackExecuteViaHost`）
- `RevisionConfig` 与 `OpStackForkSchedule` **严格正交**；禁止互相推导
- Closure **禁止**跨 tx 复用
- 不实现 Bedrock/Ecotone；**不含** ORCH-3-INT-1
- 命令前缀：`rtk`

## Recommended Task Order

```
Task 1 → Task 2 → Task 3A → Task 5 → Task 4 → Task 3B → Task 6 → Task 7
```

**Invariant:** 删除 `OpStackTxExecutor::m_isIsthmus` 字段（Task 3B）必须在 Task 4（测试迁移）+ Task 5（TE）完成之后。

## File Map

| File | Action |
|------|--------|
| `bcos-evm/opstack/OpStackForkSchedule.h` | Create |
| `bcos-evm/opstack/OpStackFee.h/cpp` | Modify |
| `bcos-evm/opstack/OpStackExecuteViaHost.h/cpp` | Modify |
| `bcos-evm/opstack/OpStackTxExecutor.h/cpp` | Modify |
| `transaction-executor/.../OpStackTransactionExecutorImpl.h` | Modify |
| `bcos-evm/test/opstack/OpStackForkScheduleTest.cpp` | Create |
| `bcos-evm/test/opstack/OpStackFeeTest.cpp` | Modify |
| `bcos-evm/test/opstack/RefundIsthmusTest.cpp` | Modify |
| `bcos-evm/test/opstack/OpStackSettlementTest.cpp` | Modify |
| `bcos-evm/test/opstack/BlobGasBalanceTest.cpp` | Modify |
| `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp` | Modify |
| `bcos-evm/test/CMakeLists.txt` | Modify |
| `bcos-evm/docs/adr/014-opstack-fork-schedule-l1-cost.md` | Create |
| `bcos-evm/capability-matrix.md` | Modify |

---

### Task 1: OpStackForkSchedule + unit tests

**Requires:** —  
**Produces:** `makeIsthmusPlusForkSchedule`, `isOpStackFjord`, `isOpStackIsthmus`

- [ ] **Step 1:** 写 `OpStackForkScheduleTest.cpp`（preset、genesis 0、nullopt inactive、future inactive）— 见 spec §9
- [ ] **Step 2:** 注册 CMake `OpStackForkScheduleTest`
- [ ] **Step 3:** 实现 `OpStackForkSchedule.h`（含 internal `isOpStackForkActive`）
- [ ] **Step 4:** `rtk test ./build/bcos-evm/test/OpStackForkScheduleTest` → PASS
- [ ] **Step 5: Commit**

```bash
git add bcos-evm/opstack/OpStackForkSchedule.h bcos-evm/test/opstack/OpStackForkScheduleTest.cpp bcos-evm/test/CMakeLists.txt
git commit -m "feat(opstack): add OpStackForkSchedule with unit tests"
```

---

### Task 2: Shared cache factories in OpStackFee

**Requires:** Task 1  
**Produces:** `selectL1CostFunc`, `selectOperatorCostFunc`, `wireL1CostFuncWithState`, `wireOperatorCostFuncWithState`, typedefs `L1CostFunc` / `OperatorCostFunc`

- [ ] **Step 1:** 追加失败测试到 `OpStackFeeTest.cpp`：

```cpp
#include "bcos-evm/opstack/OpStackForkSchedule.h"

// FIX-04 params — 勿用 makeTestParams()；与现有 FIX04_FjordL1CostSolidityParity 相同
OpStackFeeParams makeFix04Params()
{
    return OpStackFeeParams{
        .l1BaseFee = u256(2'000'000),
        .l1BlobBaseFee = u256(1),
        .l1BaseFeeScalar = 20,
        .l1BlobBaseFeeScalar = 1,
    };
}

BOOST_AUTO_TEST_CASE(FIX04_via_selectL1CostFunc_matchesOpGeth105484)
{
    RollupCostData data{.fastLzSize = 235};
    auto const l1 = selectL1CostFunc(makeIsthmusPlusForkSchedule(), makeFix04Params());
    BOOST_CHECK_NO_THROW(selectL1CostFunc(OpStackForkSchedule{.fjordTime=100,.isthmusTime=0}, makeFix04Params()));
    BOOST_CHECK_EQUAL(l1(data, 1), u256(105'484));
    BOOST_CHECK_EQUAL(fjordCalldataGasUsed(data), u256(2463));
}

BOOST_AUTO_TEST_CASE(selectL1_nulloptFjord_invokeThrows)
{
    OpStackForkSchedule s{.fjordTime = std::nullopt, .isthmusTime = 0};
    auto const l1 = selectL1CostFunc(s, makeTestParams());
    RollupCostData data{.fastLzSize = 31};
    BOOST_CHECK_THROW(l1(data, 1), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(cached_l1_reselects_on_block_time_change)
{
    OpStackForkSchedule s{.fjordTime = 0, .isthmusTime = 0};
    auto const l1 = selectL1CostFunc(s, makeTestParams());
    RollupCostData data{.fastLzSize = 31};
    BOOST_CHECK_NO_THROW(l1(data, 1));
    OpStackForkSchedule preFjord{.fjordTime = 100, .isthmusTime = 0};
    auto const l1b = selectL1CostFunc(preFjord, makeTestParams());
    BOOST_CHECK_NO_THROW(l1b(data, 1));
    BOOST_CHECK_THROW(l1b(data, 50), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(selectOperator_IsthmusPlus_gas1618_matchesFixture) { /* 同原计划 */ }
BOOST_AUTO_TEST_CASE(selectOperator_preIsthmus_returnsZero) { /* 同原计划 */ }
BOOST_AUTO_TEST_CASE(selectL1_preFjord_invokeThrows) { /* 同原计划 */ }
BOOST_AUTO_TEST_CASE(selectL1_emptyRollup_returnsZero)
{
    auto const l1 = selectL1CostFunc(makeIsthmusPlusForkSchedule(), makeTestParams());
    BOOST_CHECK_EQUAL(l1(RollupCostData{}, 1), u256(0));
}

BOOST_AUTO_TEST_CASE(wireL1_matches_select_on_isthmus_plus) { /* 同原计划 */ }

BOOST_AUTO_TEST_CASE(wireOperator_matches_select_on_isthmus_plus)
{
    MockStateView state;
    state.setSlot(L1_BASE_FEE_SLOT, state::toEvmC(u256(1000) * u256(1'000'000)));
    state.setSlot(L1_BLOB_BASE_FEE_SLOT, state::toEvmC(u256(10) * u256(1'000'000)));
    state.setSlot(L1_FEE_SCALARS_SLOT, packFeeScalars(2, 3));
    state.setSlot(OPERATOR_FEE_PARAMS_SLOT,
        packOperatorFeeParams(1'439'103'868, 1'256'417'826'609'331'460ULL));
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const params = loadOpStackFeeParams(state);
    auto const fromSelect = selectOperatorCostFunc(schedule, params);
    auto const fromWire = wireOperatorCostFuncWithState(schedule, state);
    BOOST_CHECK_EQUAL(fromSelect(1618, 1), fromWire(1618, 1));
}
```

- [ ] **Step 2:** 扩展 `OpStackFee.h` — `#include <functional>` + 4 工厂声明 + typedefs
- [ ] **Step 3:** 实现 `OpStackFee.cpp` — 共享 `makeCachedL1CostFunc` / `makeCachedOperatorCostFunc`；内层 struct 命名 **`L1CostCache` / `OperatorCostCache`**（勿用 `struct State`）；`#include <memory>`, `<limits>`
- [ ] **Step 4:** `rtk test ./build/bcos-evm/test/OpStackFeeTest` → PASS
- [ ] **Step 5: Commit**

```bash
git add bcos-evm/opstack/OpStackFee.h bcos-evm/opstack/OpStackFee.cpp bcos-evm/test/opstack/OpStackFeeTest.cpp
git commit -m "feat(opstack): add L1/operator cost fork selector factories"
```

---

### Task 3A: Host wiring（保留 `m_isIsthmus` 字段）

**Requires:** Task 2  
**Do NOT delete `m_isIsthmus` yet** — 测试/TE 仍引用

- [ ] **Step 1:** `OpStackExecuteViaHost.h` — `#include OpStackForkSchedule.h`；`OpStackForkSchedule forkSchedule = makeIsthmusPlusForkSchedule();`

- [ ] **Step 2:** `OpStackExecuteViaHost.cpp` 替换 wiring：

```cpp
auto const feeParams = loadOpStackFeeParams(state);  // receipt scalar/constant ONLY
input.opTxExecutor.m_l1CostFunc =
    wireL1CostFuncWithState(input.forkSchedule, state);
input.opTxExecutor.m_operatorCostFunc =
    wireOperatorCostFuncWithState(input.forkSchedule, state);
```

删除 inline `l1CostFjord` / `operatorCostIsthmus` lambda。**暂保留** `isIsthmusOrchestrationProfile → m_isIsthmus`（Task 3B 再删）。

- [ ] **Step 3:** Receipt gate（~297）改为：

```cpp
if (isOpStackIsthmus(input.forkSchedule, txData.m_blockInfo.timestamp) &&
    input.opTxExecutor.m_operatorCostFunc)
```

- [ ] **Step 4:** `OpStackTxExecutor.cpp` — 删除 `buyGas` / `refundIsthmusOperatorCost` / `refundGas:147` 的 `m_isIsthmus &&`（operator 由 closure 语义表达）

- [ ] **Step 5:** `rtk test ./build/bcos-evm/test/OpStackExecuteViaHostSmokeTest` → PASS

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/opstack/OpStackExecuteViaHost.h bcos-evm/opstack/OpStackExecuteViaHost.cpp bcos-evm/opstack/OpStackTxExecutor.cpp
git commit -m "feat(opstack): wire fork-aware L1/operator cost funcs in ExecuteViaHost"
```

---

### Task 5: TE forkSchedule injection

**Requires:** Task 3A Step 1（`forkSchedule` 字段存在）

- [ ] **Step 1:** `OpStackTransactionExecutorImpl.h` — 添加 `input.forkSchedule = bcos::evm::makeIsthmusPlusForkSchedule();`
- [ ] **Step 2:** **删除** `input.opTxExecutor.m_isIsthmus = true;`（222–223）
- [ ] **Step 3:** `rtk test ... FIX05_signed_rlp_rollup_execute_e2e` → PASS
- [ ] **Step 4: Commit**

---

### Task 4: Test migration + §9 E2E cases

**Requires:** Task 3A Step 4（TxExecutor guards 已删）

- [ ] **Step 1: RefundIsthmusTest** — **仅删除** `executor.m_isIsthmus = true;`；**保留** lambda `m_operatorCostFunc`（勿用 `makeTestParams()`，该符号不可见）

- [ ] **Step 2: OpStackSettlementTest** — 删除所有 `m_isIsthmus = true`

- [ ] **Step 3: BlobGasBalanceTest** — 删除冗余 `m_isIsthmus` 若存在

- [ ] **Step 4: OpStackExecuteViaHostSmokeTest — 必做（非 optional）**

  - `pre_fjord_schedule_throws_on_user_tx` — user tx + `forkSchedule.fjordTime=100` + rollupCostData → `BOOST_CHECK_THROW`
  - `deposit_pre_fjord_schedule_no_throw` — deposit tx + 同上 schedule → 不 throw
  - `host_pre_isthmus_operator_recipient_zero` — `isthmusTime=100`, blockTime=50 → operator recipient 0
  - `orthogonality_non_isthmus_revision_with_isthmus_fork_schedule` — 非 Isthmus `revisionConfig` + Isthmus+ `forkSchedule` → operator fee > 0

- [ ] **Step 5:** `rtk test RefundIsthmusTest OpStackSettlementTest OpStackExecuteViaHostSmokeTest`

- [ ] **Step 6: Commit**

---

### Task 3B: Remove `m_isIsthmus` field

**Requires:** Task 4 + Task 5

- [ ] **Step 1:** 删除 `OpStackTxExecutor.h` 中 `bool m_isIsthmus`
- [ ] **Step 2:** 删除 `OpStackExecuteViaHost.cpp:108-111` `isIsthmusOrchestrationProfile` → `m_isIsthmus` 块
- [ ] **Step 3:** 全量编译 `bcos-evm/test` + TE — 无 `m_isIsthmus` 引用
- [ ] **Step 4:** `rg 'm_isIsthmus' bcos-evm transaction-executor` → **0 命中**
- [ ] **Step 5: Commit**

```bash
git commit -m "refactor(opstack): remove m_isIsthmus; fork schedule drives operator fee"
```

---

### Task 6: ADR-014 + capability-matrix

- [ ] ADR-014（spec §8.1 consumption table + deviations）
- [ ] capability-matrix §8.2 四行拆分
- [ ] R3-ORCH-3 CLOSED
- [ ] **Commit**

---

### Task 7: Full regression

```bash
ctest --test-dir build/bcos-evm/test -R 'OpStackForkSchedule|OpStackFee|OpStackExecuteViaHost|RefundIsthmus|OpStackSettlement|RollupCost|OpStackTxInputBuilder' --output-on-failure
ctest --test-dir build/transaction-executor/tests -R OpStackTransactionExecutorFixture --output-on-failure
ctest --test-dir build/bcos-evm/test -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' --output-on-failure
rg 'm_isIsthmus' bcos-evm transaction-executor --glob '*.{cpp,h,hpp}'  # expect 0
```

---

## Spec Coverage Self-Check（修订）

| Spec § | Task | 备注 |
|--------|------|------|
| §4.2 wire* | 3A | |
| §5.1 schedule | 1 | |
| §5.2 cache + wireOperator | 2 | wireOperator equiv test |
| §5.3 m_isIsthmus removal | 3B, 4, 5 | |
| §5.4 TE | 5 | |
| §6 throw / deposit | 4 Step 4 | **required** host cases |
| §6 正交性 | 4 Step 4 | |
| §8 ADR/matrix | 6 | |
| §9 cache 1→50 / nullopt | 2 | |
| ORCH-3-INT-1 | — | Phase 2 |

---

## Verification Commands

```bash
cmake --build build --target OpStackForkScheduleTest OpStackFeeTest OpStackExecuteViaHostSmokeTest bcos-evm-op -j
rtk test ./build/bcos-evm/test/OpStackForkScheduleTest
rtk test ./build/bcos-evm/test/OpStackFeeTest
```

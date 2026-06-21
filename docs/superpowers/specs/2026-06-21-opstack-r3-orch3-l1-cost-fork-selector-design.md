# OPStack R3-ORCH-3：L1 Cost Fork 选择器与扩展性设计

**日期：** 2026-06-21  
**状态：** Approved — grilling（Q1–Q5）+ multi-agent review 修订  
**审计来源：** R3-ORCH-3 @ Wave 3 Isthmus re-audit  
**op-geth 锚点：** `core/types/rollup_cost.go:151-251` (`NewL1CostFunc` / `NewOperatorCostFunc`)  
**FB 锚点：** `OpStackExecuteViaHost.cpp:116-122`（硬编码 `l1CostFjord`）；`OpStackTxExecutor.cpp:50-65`（`blockTime` 已传入但被忽略）；receipt operator 写回 `OpStackExecuteViaHost.cpp:297-307`  
**相关 ADR：** ADR-004、ADR-005、ADR-012；本 spec 新增 **ADR-014**  
**前置：** R3-ORCH-1/2（OPF1 header fees）；Fjord 公式与 signed RLP 字节源（FIX-04/05）

---

## 1. 问题陈述

| 维度 | op-geth | FB 现状 | 风险 |
|------|---------|---------|------|
| L1 cost 选择 | `NewL1CostFunc` 按 `blockTime` + 链配置在 Bedrock / Ecotone / Fjord 间切换 | 编排层 lambda 固定调用 `l1CostFjord`，忽略 `blockTime` | R3-ORCH-3 开放；未来 fork 需改 orchestrator |
| 首 Ecotone 块 | Bedrock 回退（consensus-critical） | 无 | 产品不跑历史块 → out of scope |
| Operator fee | `IsOptimismIsthmus(blockTime)` 门控 | `m_isIsthmus`：TE **硬编码** `true`（`OpStackTransactionExecutorImpl.h:222-223`）+ 编排层 `isIsthmusOrchestrationProfile` 启发式 | 与 L1 cost 选择机制不一致；**非** RevisionConfig 字段直读 |

**与 ORCH-1/2 的边界：**

| 项 | 管什么 |
|----|--------|
| ORCH-1/2 | L2 执行层 gas price：`baseFee` / `blobBaseFee` 从 block header **OPF1** 读取 |
| **ORCH-3** | L1 rollup 数据费 / operator fee：**按 `OpStackForkSchedule` 选公式** |

**显式不涉及 ORCH-3：** `resolveOpStackBaseFee` / `resolveOpStackBlobBaseFee`（ORCH-1/2 API）。

L1Block slot 1/3/7/scalars 服务 L1 cost / operator 公式输入（`loadOpStackFeeParams`），**不得**注入 execution `blobBaseFee`（ORCH-2 已解耦）。

---

## 2. 目标与非目标

### 目标

1. 引入 **OpStackForkSchedule**（与 `RevisionConfig` 分离），对标 op-geth `ChainConfig.Optimism.*Time`。
2. 提供 **per-block cache 工厂**（Q1=C）；编排层不再 inline 绑定 `l1CostFjord`。
3. **Isthmus+ 产品基线**行为不变：Fjord L1 + Isthmus operator fee。
4. **pre-Fjord L1** → throw（FB deviation）；**pre-Isthmus operator** → 恒零（对齐 op-geth，Q3=B）。
5. 未来 fork 接入契约（§7）。
6. 闭合 R3-ORCH-3（intentional scope + extensible scaffold）。

### 非目标

- Bedrock / Ecotone L1 公式及首 Ecotone Bedrock 回退
- OP 主网 genesis 历史重放
- Op Stack fork 时间戳写入 `RevisionConfig` bool（ADR-004）
- Jovian+ 具体公式（本 spec 只留扩展点）
- L1Block slot 语义变更、receipt `L1GasUsed`
- Phase 1 **block 级** closure 复用（见 §5.2 Phase 1 契约）
- **ORCH-3-INT-1**：`LedgerConfig` / genesis fork 注入（§10.2）

---

## 3. 方案

**推荐方案 A：** `OpStackForkSchedule` + 共享 cache-miss helper + `wire*WithState` 生产路径。

工厂签名接收 **`(schedule, params)`**；**`blockTime` 在返回 closure 每次 invoke 时传入**（对标 op-geth outer closure），非 factory 构造参数。

### 3.1 Grilling 已闭合决策

| # | 主题 | 决策 |
|---|------|------|
| Q1 | fork 校验时机 | **C**：per-block cache；`forBlock` + cache miss 重选 impl |
| Q2 | StateView | **C**：单测 `select*`；生产 `wire*WithState` 捕获 `StateView const&` |
| Q3 | pre-fork 错误 | **B**：operator pre-Isthmus 恒零；L1 pre-Fjord throw（deviation） |
| Q4 | forkTime 类型 | **A**：`std::optional<uint64_t>`（`nullopt` / `0` / `T>0`） |
| Q5 | vs RevisionConfig | **A**：严格正交，TE 并列注入，禁止互相推导 |

---

## 4. 架构

### 4.1 两层 fork 模型

```
RevisionConfig          → EVM 语义 / kernel / evmone / executeMessage gates
OpStackForkSchedule     → L1 cost 公式、operator fee（本 spec 范围）
```

deposit / preCheck 等其它编排 fork 门控**不在**本 spec 扩展 schedule。

### 4.2 数据流

**Deposit 路径：** 不经过 factory / `buyGas` / L1 closure（与现状一致）。

**非 deposit 路径（生产）：**

```
TE: opStackExecuteViaHostTx()
  ├─ revisionConfig = makeIsthmusRevisionConfig()     // 删除 m_isIsthmus = true
  └─ forkSchedule   = makeIsthmusPlusForkSchedule()

opStackExecuteViaHost:
  ├─ State state(*stateView)                          // 单次 tx 栈上；closure 生命周期 ⊆ 本调用
  ├─ wireL1CostFuncWithState(schedule, state)        // Phase 1 miss: loadOpStackFeeParams(state)
  ├─ wireOperatorCostFuncWithState(schedule, state)  // Phase 1 miss: 读 slot 8 或 loadOpStackFeeParams
  ├─ buyGas → executeMessage → refundGas
  └─ receiptMeta.l1Fee / operatorFee                 // 改由 isOpStackIsthmus(schedule, blockTime) 门控
```

**单元测试路径：** `selectL1CostFunc(schedule, params)` / `selectOperatorCostFunc(schedule, params)` — 使用 **snapshot `params`**，不 mock StateView。

**Phase 1 cache 契约：** TE **每 tx** 新建 closure（`opStackExecuteViaHost` 每调用一次）；cache 优化 **同 tx 内** 多次 invoke（`buyGas` + `refundIsthmusOperatorCost` + receipt）。**禁止**跨 tx 复用 closure（避免 stale `params` / UAF）。Block 级复用列为 Integration opt-in。

**共识不变量（对齐 op-geth deposit 顺序）：** `loadOpStackFeeParams` / cache miss 读 state 发生在 **该 tx 执行时** 的 state 快照上；L1 attributes 系统 deposit 须在同块内 **先于** 依赖更新后 fee 的用户 tx 执行（现有 Stage B 测试不变）。

### 4.3 Isthmus+ preset

```cpp
inline OpStackForkSchedule makeIsthmusPlusForkSchedule()
{
    return OpStackForkSchedule{
        .fjordTime = 0,      // optional: 0 = 创世激活（op-geth FjordTime=&zero）
        .isthmusTime = 0,
    };
}
```

门控（internal `isOpStackForkActive`）：`forkTime.has_value() && *forkTime <= blockTime`（对标 `isTimestampForked`）。

---

## 5. 组件与 API

### 5.1 `OpStackForkSchedule.h`

```cpp
struct OpStackForkSchedule {
    std::optional<uint64_t> fjordTime;
    std::optional<uint64_t> isthmusTime;
    // 未来: std::optional<uint64_t> jovianTime;
};

OpStackForkSchedule makeIsthmusPlusForkSchedule();
bool isOpStackFjord(OpStackForkSchedule const&, uint64_t blockTime);
bool isOpStackIsthmus(OpStackForkSchedule const&, uint64_t blockTime);
// isOpStackForkActive — namespace detail / 内联于上述 helper
```

Phase 1 合法 schedule：**仅** `makeIsthmusPlusForkSchedule()`（Integration 前禁止自定义 partial schedule）。

### 5.2 `OpStackFee.h` — 工厂与共享 helper

```cpp
using L1CostFunc = std::function<u256(RollupCostData const&, uint64_t blockTime)>;
using OperatorCostFunc = std::function<u256(uint64_t gas, uint64_t blockTime)>;

L1CostFunc selectL1CostFunc(OpStackForkSchedule const&, OpStackFeeParams const& params);
OperatorCostFunc selectOperatorCostFunc(OpStackForkSchedule const&, OpStackFeeParams const& params);

L1CostFunc wireL1CostFuncWithState(
    OpStackForkSchedule const&, state::StateView const& state);
OperatorCostFunc wireOperatorCostFuncWithState(
    OpStackForkSchedule const&, state::StateView const& state);
```

**Normative：共享 cache 实现（禁止 duplicate logic）**

- Internal `makeCachedL1CostFunc` / `makeCachedOperatorCostFunc` 接受：
  - `OpStackForkSchedule`（**value capture**）
  - **params 来源 lambda**：`(uint64_t blockTime) -> OpStackFeeParams`
    - `select*`：`[=] { return paramsSnapshot; }`（`paramsSnapshot` **按值**捕获）
    - `wire*`：`[&state](uint64_t) { return loadOpStackFeeParams(state); }`（**cache miss 重读 state**，对齐 op-geth miss 读 L1Block）
- Outer closure **按值**持有 schedule + inner state；**禁止**捕获调用方栈上 `OpStackFeeParams&`。
- 捕获 `StateView const&`（或 pointer），**不**捕获 transactional `State` wrapper；生命周期 ⊆ 单次 `opStackExecuteViaHost`。

**Per-block cache 伪代码（L1）：**

```cpp
return [schedule, resolveParams, stateRef](RollupCostData const& data, uint64_t blockTime) -> u256 {
    if (data.isEmpty()) {
        return 0;  // 见 §6 与 op-geth nil 语义对照
    }
    if (forBlock != blockTime) {
        forBlock = blockTime;
        auto params = resolveParams(blockTime);
        if (!isOpStackFjord(schedule, blockTime)) {
            cached = [](RollupCostData const&, OpStackFeeParams const&) {
                throw std::invalid_argument("OpStack: pre-Fjord L1 cost unsupported");
            };
        } else {
            cached = [](RollupCostData const& d, OpStackFeeParams const& p) {
                return l1CostFjord(d, p);
            };
        }
        cachedParams = std::move(params);
    }
    return cached(data, cachedParams);
};
```

**Operator cache miss（Q3=B + op-geth slot 8）：**

| 条件 | 行为 |
|------|------|
| `!isOpStackIsthmus` | 恒零 impl |
| Isthmus+ 且 operator slot 为空（全零 bytes32） | 恒零 impl（对齐 `rollup_cost.go:228-232`） |
| Isthmus+ 且 slot 有值 | `operatorCostIsthmus(gas, params)` |
| 未来 Jovian+ | miss 分支追加 `isOpStackJovian` → `operatorCostJovianFix(...)` |

**`wire*` vs `select*`（Phase 1 Isthmus+）：** 行为等价（Fjord+ 均不依赖 state 读）；§9 要求 **equivalence test**。

### 5.3 `OpStackExecuteViaHost` 编排改动

**Input：**

```cpp
OpStackForkSchedule forkSchedule = makeIsthmusPlusForkSchedule();
```

**接线（删除 wire 前 `loadOpStackFeeParams` + inline lambda）：**

```cpp
input.opTxExecutor.m_l1CostFunc =
    wireL1CostFuncWithState(input.forkSchedule, state);
input.opTxExecutor.m_operatorCostFunc =
    wireOperatorCostFuncWithState(input.forkSchedule, state);
```

**`m_isIsthmus` 同 PR 全量移除：**

| 位置 | 动作 |
|------|------|
| `OpStackExecuteViaHost.cpp:108-110` | 删除 `isIsthmusOrchestrationProfile` → `m_isIsthmus` |
| `OpStackTxExecutor.cpp` `buyGas` / `refundIsthmusOperatorCost` / `refundGas` | 删除 `m_isIsthmus &&` |
| `OpStackExecuteViaHost.cpp:297-307` receipt operator 字段 | 改为 `isOpStackIsthmus(input.forkSchedule, blockTime) && m_operatorCostFunc` |
| `OpStackTxExecutor.h` | **删除** `m_isIsthmus` 字段（非 deprecated 半态） |
| `OpStackTransactionExecutorImpl.h:222-223` | **删除** `m_isIsthmus = true` |
| `RefundIsthmusTest` / `OpStackSettlementTest` / `BlobGasBalanceTest` | 改为 factory 注入或依赖默认 `forkSchedule` |
| `capability-matrix.md:66-67` | operator fee 行去掉 `m_isIsthmus` |

`isIsthmusOrchestrationProfile`：**保留**供 TE executor 路由 / matrix「Isthmus executor integration」行；**不再**驱动 fee。

### 5.4 TE 接线（Q5=A）

```cpp
input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
input.forkSchedule = bcos::evm::makeIsthmusPlusForkSchedule();
```

禁止从 `revisionConfig` 推导 `forkSchedule`。

---

## 6. 错误处理与异常语义

| 场景 | 行为 |
|------|------|
| pre-Fjord L1 invoke | `std::invalid_argument` **穿透** `buyGas` → `opStackExecuteViaHost` → TE；**不** apply stateDiff；gas pool hook 未 consume 时不需 return（buyGas 在 consume 前 throw 时） |
| pre-Fjord + **deposit tx** | 不走 `buyGas` / L1 closure → **不 throw** |
| Isthmus+ 正常块 | Fjord L1 + Isthmus operator |
| 空 / 缺失 `RollupCostData` | `buyGas` 不调用 L1 func（现状）；若 invoke 且 `isEmpty()` → **0**（FB）；op-geth 外层 **nil** — ADR-014 标注 **profile-equivalent deviation** |
| pre-Isthmus operator | 恒零，不 throw |
| Integration 前 schedule | TE 恒 `makeIsthmusPlusForkSchedule()` |

**不存在的路径：** Bedrock/Ecotone fallback；RevisionConfig 替代 schedule。

---

## 7. 未来 fork 接入契约

同一 PR 须含：

1. `OpStackForkSchedule` 新 `optional` 字段 + `isOpStack*()`
2. 新公式函数（`OpStackFee.cpp`，非 orchestrator inline）
3. **共享 cache-miss helper** 新分支（`select*` 与 `wire*` 共用）
4. 若需 state：`wire*WithState` miss 路径（§5.2）
5. capability-matrix 行 + op-geth parity 测试
6. ADR-014 consumption table 更新

---

## 8. 文档与审计闭合

### 8.1 ADR-014（blocking deliverable）

**OpStack Fork Schedule vs Historical L1 Cost**

| 字段 / 条款 | Category | Consumer / 内容 |
|-------------|----------|-----------------|
| `fjordTime` | consumed | `makeCachedL1CostFunc` miss → Fjord / throw |
| `isthmusTime` | consumed | `makeCachedOperatorCostFunc` miss → Isthmus / zero |
| L1 pre-Fjord throw | **deviation** | op-geth → Bedrock；FB 无实现；须 positive test |
| Operator pre-Isthmus zero | explicit | 对齐 op-geth |
| Empty rollup → 0 vs nil | deviation | profile-equivalent；§6 |
| Cache miss 读 state | consumed (wire path) | `loadOpStackFeeParams(state)` on miss |
| vs RevisionConfig | policy | 严格正交（Q5=A） |

### 8.2 capability-matrix（ADR-003 分行，禁止合并）

| Capability | Layer | OPStack | Test ref |
|------------|-------|---------|----------|
| Rollup L1 cost tx bytes | tx input | explicit (`buildRollupCostData` signed RLP) | `OpStackTxInputBuilderTest`, FIX-05 |
| Rollup L1 cost fork selection | orchestration | explicit (`wireL1CostFuncWithState` / `selectL1CostFunc`) | `OpStackForkScheduleTest`, factory FIX-04 |
| L1 pre-Fjord unsupported | orchestration | **deviation** (throw) | pre-Fjord throw + buyGas/host E2E |
| OPStack operator fee fork gate | orchestration | explicit (`wireOperatorCostFuncWithState` / `selectOperatorCostFunc`) | 扩 `OpStackFeeTest`, `RefundIsthmusTest` |

**修订现有行：**

- 删除或替换原「Rollup L1 cost tx bytes (Fjord)」单行合并表述
- 第 66 行 operator fee：去掉 `m_isIsthmus profile gate` → `OpStackForkSchedule` + factory

### 8.3 R3-ORCH-3 闭合判定

| 条件 | 状态 |
|------|------|
| Fjord 公式 | ✅ FIX-04/05 |
| fork scaffold + ADR-014 | 本 spec 实现后 ✅ |
| 历史 fork | intentional unsupported ✅ |
| 未来 fork §7 | ✅ |

---

## 9. 测试计划

| 范围 | 用例 | 断言 |
|------|------|------|
| `OpStackForkScheduleTest` | preset + blockTime=1 | Fjord/Isthmus active |
| 同上 | `forkTime=0, blockTime=0` | active |
| 同上 | `fjordTime=nullopt` | L1 invoke throw |
| 同上 | `fjordTime=100, blockTime=50` | L1 invoke throw；factory 构造不 throw |
| 同上 | 同 closure `blockTime` 1→50 | miss 重选；50 throw |
| `OpStackFeeTest` | FIX-04 经 `selectL1CostFunc` | fee=105484；calldataGasUsed=2463 |
| 同上 | gas=1618 经 `selectOperatorCostFunc` | 与 `IsthmusOperator_gas1618` 一致 |
| 同上 | `wireL1CostFuncWithState` ≡ `selectL1CostFunc`（Isthmus+） | 同 data/blockTime 同 fee |
| 同上 | pre-Isthmus schedule | operator = 0 |
| `OpStackTxExecutor` / Host | pre-Fjord + rollupCostData | `buyGas` throw 穿透 |
| `OpStackExecuteViaHost` | deposit + pre-Fjord schedule | 无 L1 throw |
| `OpStackExecuteViaHost` | pre-Isthmus schedule | operator recipient 0 |
| 正交性 | 非 Isthmus `revisionConfig` + Isthmus+ `forkSchedule` | operator fee > 0 |
| 更新 | `RefundIsthmusTest`, `OpStackSettlementTest`, `BlobGasBalanceTest` | 无 `m_isIsthmus`；literal 不变 |
| TE | `FIX05_signed_rlp_rollup_execute_e2e` | **必 PASS**（执行路径经 wire factory） |
| Smoke | `OpStackExecuteViaHostSmokeTest` | L1 fee 与 factory 字面一致 |
| 回归 | capability-gate opstack ctest regex | 全绿（非固定「26/26」） |

**不新增：** Bedrock/Ecotone 公式向量。

---

## 10. 实现范围

### 10.1 Phase 1 估算

| 任务 | 文件 | 规模 |
|------|------|------|
| `OpStackForkSchedule.h` | 新增 | ~45 LOC |
| shared cache + select/wire 四入口 | `OpStackFee.h/cpp` | ~130–160 LOC |
| 编排 + receipt + 删 `m_isIsthmus` | `OpStackExecuteViaHost.*`, `OpStackTxExecutor.*` | ~60–80 LOC |
| TE | `OpStackTransactionExecutorImpl.h` | ~5 LOC |
| 测试 + `test/CMakeLists.txt` | 新/扩 test targets | ~120–180 LOC |
| ADR-014 + matrix | docs | 文档 |

**合计：** ~280–350 LOC（含测试）。

**CMake：** `OpStackForkSchedule.h` 仅 header；逻辑进现有 `OpStackFee.cpp`；新增 `OpStackForkScheduleTest` 按 `OpStackFeeTest` 模板注册。

### 10.2 ORCH-3-INT-1（Phase 2，不在 Phase 1）

| 项 | 现状 |
|----|------|
| `LedgerConfig.tars` | **无** `fjordTime` / `isthmusTime` |
| Genesis | 无 optimism fork 字段 |
| 加载 | 需 genesis JSON + loader + TE `loadForkSchedule(ledgerConfig)` |

**追踪 ID：** ORCH-3-INT-1 — 单独 spec/plan；Phase 1 **禁止**隐含「一行 loadForkSchedule」。

**默认策略：** Integration 缺字段时 ≡ `makeIsthmusPlusForkSchedule()`。

### 10.3 建议任务顺序

1. `OpStackForkSchedule.h` + 单测  
2. `OpStackFee` shared cache + select/wire + 扩 `OpStackFeeTest`  
3. `OpStackExecuteViaHost` + 删 `m_isIsthmus` + receipt  
4. TE + 迁移 Refund/Settlement/BlobGas 测试  
5. ADR-014 + matrix + capability-gate 全量回归  

---

## 11. 批准记录

- [x] 方案 A + per-block cache（Q1=C）
- [x] wire*WithState + shared helper（Q2=C，review 修订）
- [x] Q3=B + ADR-014 deviation 表
- [x] optional forkTime（Q4=A）
- [x] RevisionConfig 正交（Q5=A）
- [x] multi-agent review P0/P1 已并入 spec
- [x] Phase 1 同 PR 删除 `m_isIsthmus` 全路径

**下一步：** `writing-plans` → `docs/superpowers/plans/2026-06-21-opstack-r3-orch3-l1-cost-fork-selector.md`

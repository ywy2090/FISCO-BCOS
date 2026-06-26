# OpStack 费用投影层 — 设计规格

**日期：** 2026-06-26  
**状态：** 已批准（Grilling）  
**ADR：** [ADR-021 Appendix A](../../bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md#appendix-a--fee-projection-deepening-2026-06-26)  
**相关 ADR：** [ADR-019](../../bcos-evm/docs/adr/019-orchestration-pipeline.md)、[ADR-021](../../bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md)、[ADR-025](../../bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md)  
**迁移策略：** **三 PR 渐进**（PR1 投影 · PR2 deep module · PR3 清理）  
**Grilling 决策：** 边界=A · 侧车=A2 · View=B3 · 收拢=C2 · Deposit=D1 · Session=E2 · 测试迁移=F3

**前置：**

- [2026-06-25-opstack-settlement-ctx-single-source-design.md](./2026-06-25-opstack-settlement-ctx-single-source-design.md)（ADR-021 PR1+PR2 Done）
- [ADR-025](../../bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md)（entry reject abort Done）

---

## 1. 背景与动机

### 1.1 问题

Normal L2 路径的费用状态分散在三处：

| 来源 | 承载 |
| --- | --- |
| `TxPipelineContext` | `originalGasLimit`、`gasPrice`、`evmcResult`、`exitKind`、`state` |
| `OpStackFeeContext` | 15+ 字段，多数为 `input`/`ctx` 镜像 |
| `OpStackExecutionRequest` | `gasTipCap`、`rollupCostData`、fork hooks |

`populateFeeContext` 手工复制 request → `feeCtx`；`buyGas` / `refundGas` / `finalizeNormal` 同时读 `ctx` 和 `feeCtx`。ADR-025 abort 契约依赖 lifecycle **call site 组合**，而非单一 module interface。

### 1.2 与 ADR-021 / ADR-025 的关系

| 既有决策 | 本 spec |
| --- | --- |
| ADR-021 ctx 单源（gas/message/state） | **保持**；不往 `TxPipelineContext` 渗 OP 字段 |
| ADR-021 `finalizeNormal` sync 分层 | **保持**（F3）；作为 internal seam |
| ADR-025 entry reject abort | **内聚**到 `OpStackNormalFeeSettlement::completeAfterPipeline` |
| Deposit `settleDeposit` | **不变**（D1） |

---

## 2. 目标与非目标

### 2.1 目标

1. `OpStackSettlementView` — `ctx` + `input` + `sidecar&` 只读投影（B3）。
2. `OpStackFeeSidecar` — 5 个 lifecycle 可变字段（A2）。
3. 删除 `populateFeeContext`。
4. `OpStackNormalFeeSettlement` deep module — `buyGas` + `completeAfterPipeline`（C2）。
5. `OpStackOrchestrationProfile::Session` → `{ input, view }`（E2）。
6. ADR-025 决策树单测面落在 `completeAfterPipeline`。

### 2.2 非目标

- Deposit 路径对称深化（D1）。
- 费用数学迁入 `eth/` kernel（ADR-019 Q7）。
- TE `OpStackTransactionExecutorImpl` 大改（seam 在 `bcos-evm` lifecycle）。
- `TxFeeSettlement` 三链统一（Eth TE vs 参考路径 — 另项）。

---

## 3. 模块设计

### 3.1 `OpStackFeeSidecar`

```cpp
struct OpStackFeeSidecar {
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
    uint64_t floorDataGas{0};
};
```

| 字段 | 写入 | 读取 |
| --- | --- | --- |
| `floorDataGas` | precheck | `finalizeNormal` |
| `effectiveGasPrice`, `baseFee` | `buyGas` | `refundGas`, lifecycle |
| `l1CostCharged`, `operatorCostLimit` | `buyGas` | `refundGas`, `projectNormalReceiptMeta` |

### 3.2 `OpStackSettlementView`

```cpp
struct OpStackSettlementView {
    TxPipelineContext& ctx;
    OpStackExecutionRequest const& input;
    OpStackFeeSidecar& sidecar;

    // 只读 accessor（从 input / ctx 投影，不镜像存储）
    bool isDepositTx() const;
    bool isCall() const;
    bcos::u256 gasTipCap() const;
    bcos::u256 gasFeeCap() const;
    state::BlockInfo const& blockInfo() const;
    std::optional<RollupCostData> const& rollupCostData() const;
    // ...

    bcos::u256 effectiveGasPrice() const;  // sidecar 非零则用 sidecar，否则从 input 推算

    OpStackFeeSidecar& mutableSidecar() { return sidecar; }
};
```

构造点：`runOpStackTxLifecycle` 在 `TxPipelineContext` 初始化后、precheck 前。

### 3.3 `OpStackNormalFeeSettlement`（deep module）

```cpp
struct OpStackNormalFeeSettlement {
    OpStackTxFeeLedger& ledger;  // adapter seam（L1/operator hooks）

    task::Task<bool> buyGas(OpStackSettlementView view);
    task::Task<void> completeAfterPipeline(
        OpStackSettlementView view,
        GasPoolHooks const& gasPool,
        OpStackFeeParams const& feeParams,
        OpStackExecutionResult& output);
};
```

**`completeAfterPipeline` 内部（ADR-025 单点）：**

```
if isNormalPreExecutionReject(ctx.exitKind):
    abort: revert + gasPool full release + gasUsed=0 + empty stateDiff
else:
    ctx.state.commit()
    settled = finalizeNormal(ctx, sidecar, exitKind)   // sync internal
    refundGas via ledger(view, settled)
    gasPool.returnGas(...)
    projectNormalReceiptMeta(output, view, feeParams, settled)
    output.stateDiff = ctx.state.build_diff()
```

### 3.4 `OpStackTxFeeLedger`（adapter，保留）

- 仍持有 `m_l1CostFunc`、`m_operatorCostFunc`、recipient 地址。
- 方法签名改为 `buyGas(OpStackSettlementView)` / `refundGas(OpStackSettlementView const&, OpStackSettlementResult const&)`。
- `OpStackTxFeeLedgerCtxTest` 继续测 routing。

### 3.5 `OpStackOrchestrationProfile::Session`（E2）

```cpp
struct Session {
    OpStackExecutionRequest const& input;
    OpStackSettlementView view;
};
```

`OpStackPrecheckPolicy`：`floorDataGasOut` → `view.mutableSidecar().floorDataGas`；其余读 `view.*()` accessor。

### 3.6 Deposit 路径（不变）

```text
precheck(view) → acquireGasPool → mint → checkpoint → pipeline → settleDeposit
```

不使用 `OpStackFeeSidecar`（除 precheck 可能写 `floorDataGas`，deposit finalize 忽略）。

---

## 4. Lifecycle 收敛后（normal）

```text
view = { ctx, input, sidecar }
session = { input, view }
bindings = OpStackOrchestrationProfile::bind(session)

precheck → earlyExit?
gasPool.acquire → checkpoint
settlement.buyGas(view) → fail? abortNormalAfterBuyGas
runTxPipeline
settlement.completeAfterPipeline(view, gasPool, feeParams, output)
```

删除：`populateFeeContext`、`completeNormalTxAfterPipeline`（public）、`settleNormal`（public）。

---

## 5. 测试策略（F3）

| 层级 | 测试 | 职责 |
| --- | --- | --- |
| Sync internal | `finalizeNormal` 单测（保留/迁入 detail） | gas math only |
| Adapter | `OpStackTxFeeLedgerCtxTest` | refund routing、buyGas 读 view.ctx |
| Deep module | `OpStackNormalFeeSettlementTest`（新） | ADR-025 决策树 |
| E2E | `OpStackTxLifecycleCharacterizationTest` | 扩 ADR-025 矩阵 |
| Async wiring | `OpStackSettleAsyncTest` | 改打 settlement module，不直接 `settleNormal` |

---

## 6. PR 计划

### PR1 — 投影 + sidecar（零行为变更）

| 动作 | 文件 |
| --- | --- |
| 新增 | `opstack/OpStackSettlementView.h`（+ `.cpp` 若需） |
| 新增 | `opstack/OpStackFeeSidecar.h` |
| 修改 | `OpStackTxLifecycle.cpp` — 删 `populateFeeContext`；构造 view |
| 修改 | `OpStackPrecheckPolicy.*` — Session/view |
| 修改 | `OpStackOrchestrationProfile.h` — Session E2 |
| 修改 | `OpStackTxFeeLedger.*` — 签名 `view`；`OpStackFeeContext` 暂保留 typedef 到 sidecar+compat |
| 修改 | 测试 helpers — `feeCtx` → `sidecar` + `view` |

**门禁：** 全 OpStack ctest 绿；无 behavior change。

### PR2 — deep module

| 动作 | 文件 |
| --- | --- |
| 新增 | `opstack/OpStackNormalFeeSettlement.h/.cpp` |
| 修改 | `OpStackTxLifecycle.cpp` — 调 settlement module |
| 修改 | `OpStackSettlement.*` — async 路径迁入 module；`finalizeNormal` 留 internal |
| 新增/扩 | `OpStackNormalFeeSettlementTest`、characterization |

**门禁：** ADR-025 矩阵全绿。

### PR3 — 清理

| 动作 | 文件 |
| --- | --- |
| 删除 | `OpStackFeeContext` struct |
| 删除 | public `settleNormal`、`completeNormalTxAfterPipeline` |
| 更新 | ADR-021 附录、architecture-overview §OpStack fee |
| 更新 | `opstack/README.md` |

**门禁：** OpStack ctest + capability gate。

---

## 7. Compliance checklist

- [x] `eth/` 无新 OP 字段渗入 `TxPipelineContext`
- [x] `populateFeeContext` 已删除
- [x] lifecycle normal 路径仅 2 个 settlement 调用（`buyGas` + `completeAfterPipeline`）
- [x] ADR-025 characterization 全绿
- [x] `OpStackTxFeeLedgerCtxTest` 仍覆盖 adapter routing
- [x] Deposit 路径无回归
- [x] `OpStackFeeContext` 已删除（PR3）

---

## 8. 未来延伸（不在本 spec）

- `OpStackDepositSettlement` 对称 deep module（若 deposit 摩擦上升）
- `ExecutionSession` 注入 RAII（port 迁移类）
- `TxFeeSettlement` Eth TE vs 参考路径统一

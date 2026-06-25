# OpStackSettlement — ctx 单一真相源（消 txData 影子帧）

**Status:** Implemented (PR1)  
**Date:** 2026-06-25  
**Implementation:** `OpStackSettlement::{finalizeNormal}`, `OpStackFeeContext`, `buyGas(ctx, feeCtx)`  
**Related:** ADR-019, ADR-005, `architecture-review` 候选 7, `2026-06-24-orchestration-profile-design.md`  
**Companion ADR:** `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

---

## 1. Problem

ADR-019 将 OpStack intrinsic gas 同步修到 `TxPipelineContext::message`（步骤 ④ 扣减、步骤 ⑦ `executeMessage` 使用同一引用）。但 `opStackExecute` 仍维护并行的 **`OpStackTxExecutionData txData`**（「影子帧」）：

| 字段 | 用途 | 与 `ctx` 关系 |
| --- | --- | --- |
| `m_message` | `buyGas` / `refundGas` 读 sender、value | 入口拷贝，**不随 pipeline 更新** |
| `m_gasLimit` | `applySettlement`、`buyGas` | 入口拷贝，**冻结** |
| `m_gasUsed` 等 | receipt、gas pool 归还 | settlement 写回 |
| `m_state` | 费用账本改余额 | 指向 `ctx.state`（冗余句柄） |

**症状：**

1. **locality 分裂** — 同一条 normal 路径，floor 预检读 `orchestrationCtx.message`，settlement 读 `txData.m_gasLimit`，`buyGas` 读 `txData.m_message`。
2. **ADR-019 Q14 半完成** — `ctx.state` 已是唯一 State owner；gas 尚无单一真相源。
3. **维护风险** — 下一类 gas/fee drift 与历史 OpStack dual-track message bug 同族（ADR-019 Context）。
4. **shallow module** — `OpStackTxExecutionData` interface（20+ 字段）≈ 实现；settlement 逻辑散在 `OpStackExecutionBridge.cpp`、`OpStackPipelineHookBinder.cpp`、`OpStackTxFeeLedger.cpp`。

**Deletion test（今日）：** 删 `applySettlement` 或 `txData.m_gasLimit`，复杂度散到 bridge + HookBinder + 测试；不 concentrate。

---

## 2. Goals / Non-Goals

### 2.1 Goals（PR1）

- Normal L2 交易：`buyGas`、gas 结算、`refundGas`、gas pool 归还 **只读 `TxPipelineContext`** 作为 gas/message 真相源。
- 引入 **`OpStackSettlement`** deep module：`finalizeNormal(ctx, feeCtx, exitKind)` 单点 enforcement。
- 收窄 `OpStackTxExecutionData` → **`OpStackFeeContext`**：PR1 删除 `m_message`、`m_gasLimit`、`m_state`。
- Settlement **移出** `TxPipelineHooks::txFinalizeGasSettlement`（wrapper 外圈一次调用，守 ADR-019 Q7/Q19）。
- 双层测试：module 单测（无 evmone）+ TE E2E。

### 2.2 Non-Goals（PR1）

- Deposit 交易迁入 `finalizeDeposit`（PR2）。
- `m_gasUsed` / `m_gasRemaining` 收成纯返回值（PR2）。
- FISCO / Eth TE 收敛。
- 修改 `runTxPipeline` 步骤顺序或 ADR-019 内核契约。

---

## 3. Grilling 决策摘要

| # | 决策 |
| --- | --- |
| 1 | Gas 结算真相源 = **`ctx`**（`originalGasLimit` + `evmcResult`） |
| 2 | `buyGas` 读 **`ctx.message`** + **`ctx.originalGasLimit`** |
| 3 | PR1 删 `m_message` / `m_gasLimit` / `m_state`；其余 fee 字段暂留（B2） |
| 4 | Early-exit 由 **`OpStackSettlement::finalizeNormal`** 表驱动（C2） |
| 5 | Deposit 用 **`finalizeDeposit`** 独立入口（D2），**PR2** 再迁 |
| 6 | PR1 **仅 normal** 路径（E1） |
| 7 | Settlement **只在 bridge** 调；hook 去掉 `txFinalizeGasSettlement`（F1） |
| 8 | 测试 G3：module + E2E |
| 9 | 文档：**ADR-021**（H1） |

---

## 4. Architecture

### 4.1 Normal 路径数据流（PR1 目标）

```text
opStackExecute (normal)
  │
  ├─ TxPipelineContext ctx{ stateView, input.message, revision, ... }
  ├─ OpStackFeeContext feeCtx{ rollup, blob, caps, flags, ... }  // 无 message/gasLimit/state
  │
  ├─ buyGas(ctx, feeCtx, ledger)           // sender/value from ctx.message; gas from ctx.originalGasLimit
  ├─ gasPoolSubGas(ctx.originalGasLimit)
  ├─ runTxPipeline(ctx, hooks)             // hooks: floor + intrinsic ONLY
  ├─ finalizeNormal(ctx, feeCtx, exitKind, ledger, gasPoolHooks)
  │     ├─ compute gasUsed from ctx + exitKind
  │     ├─ refundGas(ctx, feeCtx, ledger)
  │     └─ gasPoolReturn
  ├─ output.stateDiff = ctx.state.build_diff()
  └─ map OpStackExecutionResult (gasUsed, receiptMeta from feeCtx)
```

### 4.2 分层

```text
OpStackExecutionBridge.cpp     OpStackSettlement (新)           eth/orchestration
──────────────────────────     ──────────────────────           ─────────────────
ctx 构造                       finalizeNormal                   runTxPipeline
buyGas 前/后编排               exitKind 表 + gas 数学            debitIntrinsicGas
deposit 分支 (PR1 不动)        refund 编排                      executeMessage
map output                     gasPool 归还
```

**Seam 纪律：** `OpStackSettlement` 在 `bcos-evm/opstack/`；不得 include `bcos/`。不修改 `eth/orchestration/` 内核。

### 4.3 Early-exit 规则（`finalizeNormal`）

| `TxPipelineExitKind` | `gasUsed` | Refund 行为 |
| --- | --- | --- |
| `IntrinsicRejected` | 0 | 全额退还 `buyGas` 预扣（与现 bridge 行为一致） |
| `GasAffordRejected` | 0 | 同上 |
| `Completed` | `postExecuteGasSettlement(originalGasLimit, gas_left, refund, floor)` | 正常 `refundGas` |
| `RulesRejected` | 按现 bridge 语义 | 见 characterization 测试锁定 |
| `ExceptionHandled` | 按现 bridge 语义 | 见 characterization 测试锁定 |

**Invariant：** `gasUsed` 计算只使用 `ctx.originalGasLimit`、`ctx.evmcResult.gas_left`、`ctx.evmcResult.gas_refund`（EIP-1559 refund 门控读 `ctx.revisionConfig.eip1559`）、`feeCtx.m_floorDataGas`（floor 预检产出）。

---

## 5. Public Interface

### 5.1 新文件

```text
bcos-evm/opstack/
  OpStackSettlement.h
  OpStackSettlement.cpp
bcos-evm/test/opstack/
  OpStackSettlementTest.cpp
bcos-evm/docs/adr/
  021-opstack-settlement-ctx-single-source.md
```

### 5.2 `OpStackFeeContext`（PR1 收窄后的 per-tx 费用状态）

重命名自 `OpStackTxExecutionData`（或 typedef 过渡期保留旧名）。

**PR1 删除：**

- `evmc_message m_message`
- `int64_t m_gasLimit`
- `state::State* m_state`

**PR1 保留：**

- `m_effectiveGasPrice`, `m_baseFee`, `m_l1CostCharged`, `m_operatorCostLimit`
- `m_gasTipCap`, `m_gasFeeCap`, `m_hasGasFeeCap`, `m_gasPrice`（legacy cap 回退）
- `m_blockInfo`（coinbase、timestamp、baseFee — refund 路由）
- `m_rollupCostData`, blob 字段
- `m_call`, `m_isDepositTx`, skip 标志
- `m_floorDataGas`（floor 预检写入，settlement 读取）
- `m_gasUsed`, `m_gasRemaining`, `m_maxUsedGas`（PR2 迁出为返回值）
- `m_evmcResult`（buyGas 失败路径）

### 5.3 `OpStackSettlement`

```cpp
namespace bcos::evm {

struct GasPoolHooks {
    std::function<bool(uint64_t)> subGas;
    std::function<void(uint64_t remaining, uint64_t used)> returnGas;
};

struct OpStackSettlementResult {
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

// PR1
OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx,
    OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind,
    OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool);

// PR2
OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx,
    OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind,
    GasPoolHooks const& gasPool);

}  // namespace bcos::evm
```

### 5.4 `OpStackTxFeeLedger` 签名变更（PR1）

```cpp
task::Task<bool> buyGas(
    TxPipelineContext const& ctx,
    OpStackFeeContext& feeCtx);

task::Task<void> refundGas(
    TxPipelineContext const& ctx,
    OpStackFeeContext& feeCtx);
```

- `buyGas`：`ctx.message.sender/value`、`ctx.originalGasLimit`、`ctx.state`
- `refundGas`：同上 + `feeCtx` 中 buyGas 产出字段

### 5.5 `OpStackPipelineHookBinder`（PR1）

- **保留：** `txCheckGasAffordable`（floor precheck，读 `orchestrationCtx.message`）
- **保留：** `intrinsicPolicy`（`OpStackEntry`）
- **删除：** `txFinalizeGasSettlement`、`applySettlement`（逻辑迁入 `OpStackSettlement`）

### 5.6 `OpStackExecutionBridge.cpp`（PR1 normal 分支）

替换 post-pipeline inline if/else 为：

```cpp
auto settled = finalizeNormal(ctx, feeCtx, ctx.exitKind, input.opTxExecutor, gasPoolHooks);
output.gasUsed = settled.gasUsed;
// receiptMeta from feeCtx unchanged
```

---

## 6. Error Handling

- `buyGas` 失败：行为不变；`feeCtx.m_evmcResult` 填充；不进入 pipeline。
- `finalizeNormal`：**同步**；不抛异常改状态；异常路径仍由 `OpStackOrchestrationErrorPolicy` + bridge 处理。
- Gas pool：`finalizeNormal` 内统一 `returnGas`；删除 bridge 内 scattered `GasPoolReturnGuard` 逻辑时需保持 RAII 等价（或 guard 仅 arm/ disarm，归还由 finalize 负责）。

---

## 7. Testing

### 7.1 Module — `OpStackSettlementTest.cpp`（无 evmone）

构造最小 `TxPipelineContext` + 假 `evmc_result` + `OpStackFeeContext`：

| 用例 | 断言 |
| --- | --- |
| `IntrinsicRejected` | `gasUsed == 0`；sender 余额恢复至 buyGas 前 |
| `GasAffordRejected` | 同上 |
| `Completed` + 已知 gas_left/refund | `gasUsed` 与 `postExecuteGasSettlement` oracle 一致 |
| `Completed` + floorDataGas | floor 参与结算 |

### 7.2 E2E — 保留 + 补强

- **保留：** `OpStackIntrinsicGasSyncTest`（不回退）
- **补强：** `TestOpStackTransactionExecutorFixture` 至少 1 条 normal 路径：receipt `gasUsed` + sender balance

### 7.3 Characterization（PR1 前）

对现 `opStackExecute` normal 路径在关键 exitKind 上跑 characterization，锁定行为后再 refactor。

---

## 8. PR 切分

### PR1 — Normal path + ctx single source（本 spec）

- 新 `OpStackSettlement` + `finalizeNormal`
- 收窄 `OpStackFeeContext`（删三字段）
- 改 `buyGas` / `refundGas` 签名
- 删 HookBinder settlement
- 测试 G3
- ADR-021 Accepted

### PR2 — Deposit + 返回值收窄

- `finalizeDeposit`
- `m_gasUsed` 等迁到 `OpStackSettlementResult`
- `OpStackFeeContext` 缩至 ~6 持久字段
- Deposit TE / smoke 测试

---

## 9. Risks

| 风险 | 缓解 |
| --- | --- |
| Early-exit 行为微妙差异 | characterization 测试先于 refactor |
| `GasPoolReturnGuard` RAII 语义变化 | finalize 内显式 returnGas；单测覆盖 OOG block pool |
| Deposit 仍用旧 `txData` 字段名 | PR1 不删 deposit 路径字段；仅 normal 路径删三字段 |
| `refundGas` 读 `feeCtx.m_gasRemaining` | PR1 仍由 finalize 写入 feeCtx；PR2 改返回值 |

---

## 10. Open Items（PR1 前关闭）

- [ ] Characterization 快照：normal 路径 `RulesRejected` / `ExceptionHandled` 的 gasUsed 现值
- [ ] 确认 `GasPoolReturnGuard` 与 `finalizeNormal` 的责任划分（finalize 内统一 vs guard 委托）

---

## 11. Compliance Checklist（PR 评审）

- [ ] Normal 路径无 `txData.m_message` / `m_gasLimit` 读取
- [ ] `applySettlement` 无 call site
- [ ] `finalizeNormal` 为 normal 路径唯一 settlement call site
- [ ] ADR-019 Q7/Q19：fee 仍在 wrapper 外圈
- [ ] `eth/` 无 opstack include 增加
- [ ] `OpStackSettlementTest` + TE E2E 绿

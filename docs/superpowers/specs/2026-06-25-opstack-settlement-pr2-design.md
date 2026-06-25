# OpStackSettlement PR2 — Deposit + settle* Facade + feeCtx 收窄

**Status:** Proposed  
**Date:** 2026-06-25  
**Depends on:** PR1 (`2026-06-25-opstack-settlement-ctx-single-source-design.md`, ADR-021 PR1 Implemented)  
**Related:** ADR-019, ADR-021, `bcos-evm/docs/audits/_work/task4-deposit.md`  
**Companion ADR:** `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` (§3 Phase 2 — update on merge)

---

## 1. Problem

PR1 将 normal L2 路径的 gas/message 真相源收敛到 `TxPipelineContext`，并引入同步 `finalizeNormal` 做 gas 数学。但 settlement **locality 仍未闭合**：

| 泄漏点 | 现状 | 影响 |
| --- | --- | --- |
| `refundGas` | 仍在 `OpStackExecutionBridge.cpp` 调用，不在 settlement module | 费用环分散；`finalizeNormal` 的 `ledger`/`gasPool` 参数未使用 |
| Deposit post-pipeline | ~50 行内联于 bridge：commit/revert、nonce、`gasUsed`、gas pool | 三轨 deposit 语义难单测；与 op-geth 对齐知识散落 |
| Gas 输出双写 | `finalizeNormal` 同时写 `feeCtx.m_gasUsed` 与 `OpStackSettlementResult` | interface 冗余；`refundGas` 依赖 feeCtx 而非返回值 |
| Gas pool | `GasPoolReturnGuard` RAII 在 bridge；`buyGas` 失败时 guard 未 armed，pool 可能不归还 | 责任分裂；与设计 spec §6 意图不符 |

**Deletion test：** 删 `OpStackSettlement`，deposit 结算 + normal refund + gas pool 逻辑重新散落 bridge；复杂度不 concentrate。

---

## 2. Goals / Non-Goals

### 2.1 Goals

- 引入异步 **`settleNormal`** / **`settleDeposit`** facade，作为 bridge 上 settlement 的**唯一**异步入口。
- 引入同步 **`finalizeDeposit`**，收拢 deposit post-pipeline：三轨 gas 数学、journal commit/revert、sender nonce bump。
- **`refundGas` 迁入 `settleNormal`**；`settleDeposit` 不调 `refundGas`（deposit 无 L2 fee 路由）。
- **Gas pool 归还**由 `settle*` 显式 `returnGas`；删除 bridge `GasPoolReturnGuard`；`buyGas` 失败显式 `returnGas(gasLimit, 0)`。
- **E1：** 从 `OpStackFeeContext` 删除 `m_gasUsed` / `m_gasRemaining` / `m_maxUsedGas`；输出仅存于 `OpStackSettlementResult`。
- **H1：** 精简 `finalizeNormal(ctx, feeCtx, exitKind)` — 去掉未用的 `ledger` / `gasPool` 参数。
- **G1：** 先 `OpStackDepositSettlementTest`（module，无 evmone）锁三轨语义，再迁代码；现有 `Deposit*` 测试作集成 oracle。

### 2.2 Non-Goals

- 修改 `runTxPipeline` 步骤或 `eth/orchestration/` 内核（ADR-005）。
- Deposit **pre-pipeline** 逻辑迁入 settlement：mint、`depositNonce` 快照、checkpoint 仍留 bridge。
- FISCO / Eth TE 收敛。
- `gasPoolSubGas` 迁入 settlement（留在 bridge；`buyGas` 失败 pool 归还在 bridge 一行显式处理）。
- Typed `OrchestrationProfile` 或其他编排 refactor。

---

## 3. Grilling 决策摘要

| # | 决策 | 选择 |
| --- | --- | --- |
| 1 | Interface 分层 | **B** — 同步 `finalize*` + 异步 `settle*` facade |
| 2 | `finalizeDeposit` 边界 | **D2** — post-pipeline 全集；bridge 留 mint / `depositNonce` / checkpoint |
| 3 | Gas 字段归属 | **E1** — 只写 `OpStackSettlementResult` |
| 4 | Gas pool | **F1** — `settle*` 显式 `returnGas`；删 Guard |
| 5 | 实施顺序 | **G1** — module 测试先行 |
| 6 | `finalizeNormal` 签名 | **H1** — 去掉 `ledger` / `gasPool` |

---

## 4. Architecture

### 4.1 Module 分层

```text
OpStackSettlement (deep module, bcos-evm/opstack/)
├── finalizeNormal(ctx, feeCtx, exitKind)              sync — normal gas 数学
├── finalizeDeposit(ctx, exitKind, evmStatus)            sync — deposit gas + journal + nonce
├── settleNormal(ctx, feeCtx, exitKind, ledger, pool)  async — finalize + refund + pool
└── settleDeposit(ctx, exitKind, evmStatus, pool)      async — finalize + pool
```

**Seam 纪律：** 模块位于 `opstack/`；不 include `bcos/`；不修改 `eth/orchestration/`。

### 4.2 Normal 路径（PR2 目标）

```text
opStackExecute (normal)
  │
  ├─ TxPipelineContext ctx
  ├─ OpStackFeeContext feeCtx          // 无 gas 输出字段
  │
  ├─ gasPool.subGas(originalGasLimit)
  ├─ buyGas(ctx, feeCtx) → 失败: returnGas(gasLimit, 0); early return
  ├─ runTxPipeline(ctx, hooks)
  ├─ settled = co_await settleNormal(ctx, feeCtx, exitKind, ledger, gasPool)
  │     ├─ finalizeNormal(ctx, feeCtx, exitKind)
  │     ├─ refundGas(ctx, feeCtx, settled)
  │     └─ gasPool.returnGas(settled.gasRemaining, settled.gasUsed)
  ├─ output.gasUsed = settled.gasUsed
  ├─ output.receiptMeta from feeCtx
  └─ output.stateDiff = ctx.state.build_diff()
```

### 4.3 Deposit 路径（PR2 目标）

```text
opStackExecute (deposit)
  │
  ├─ output.receiptMeta.depositNonce = get_nonce(sender)   // bridge, mint 前
  ├─ mint if depositTx.mint > 0                            // bridge
  ├─ ctx.state.checkpoint()                                // bridge
  ├─ runTxPipeline(ctx, hooks)
  ├─ settled = co_await settleDeposit(ctx, exitKind, evmStatus, gasPool)
  │     ├─ finalizeDeposit(ctx, exitKind, evmStatus)
  │     └─ gasPool.returnGas(settled.gasRemaining, settled.gasUsed)
  ├─ output.gasUsed = settled.gasUsed
  └─ output.stateDiff = ctx.state.build_diff()
```

---

## 5. Public Interface

### 5.1 `OpStackSettlement.h`（PR2 目标）

```cpp
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

// Sync — gas math only (unit-test surface)
OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx,
    OpStackFeeContext const& feeCtx,
    TxPipelineExitKind exitKind);

OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx,
    TxPipelineExitKind exitKind,
    evmc_status_code evmStatus);

// Async — bridge call sites
task::Task<OpStackSettlementResult> settleNormal(
    TxPipelineContext& ctx,
    OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind,
    OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool);

task::Task<OpStackSettlementResult> settleDeposit(
    TxPipelineContext& ctx,
    TxPipelineExitKind exitKind,
    evmc_status_code evmStatus,
    GasPoolHooks const& gasPool);

}  // namespace bcos::evm
```

### 5.2 `finalizeNormal` — early-exit 与 completed（不变，PR1 语义）

| `TxPipelineExitKind` | `gasUsed` | `gasRemaining` |
| --- | --- | --- |
| `IntrinsicRejected` | 0 | `originalGasLimit` |
| `GasAffordRejected` | 0 | `originalGasLimit` |
| `Completed` / `RulesRejected` / `ExceptionHandled` | `postExecuteGasSettlement(...)` | 同上函数产出 |

输入：`ctx.originalGasLimit`、`ctx.evmcResult`、`ctx.revisionConfig.eip1559`（refund 门控）、`feeCtx.m_floorDataGas`。

### 5.3 `finalizeDeposit` — 三轨表（op-geth / task4-deposit）

| 条件 | `gasUsed` | State journal | Sender nonce |
| --- | --- | --- | --- |
| `Completed` + `EVMC_SUCCESS` | `postExecuteGasSettlement(originalGasLimit, gas_left, stateRefund, 0)` | `commit()` | `nonce + 1` |
| `Completed` + 非 `EVMC_SUCCESS`（如 `EVMC_REVERT`） | actual（同上 `postExecuteGasSettlement`） | `revert()` if checkpoint | `nonce + 1` |
| `exitKind != Completed`（entry / intrinsic 失败） | `originalGasLimit` | `revert()` if checkpoint | `nonce + 1` |

**Invariants:**

- Mint 已在 bridge checkpoint **之前**应用；`finalizeDeposit` 不处理 mint。
- Deposit **永不**调用 `buyGas` / `refundGas` / L1·operator fee 路由。
- `stateRefund` 门控：`ctx.revisionConfig.eip1559 ? evmcResult.gas_refund : 0`（与 normal 一致）。
- `gasRemaining` = `max(0, originalGasLimit - gasUsed)`（用于 gas pool）。

### 5.4 `OpStackFeeContext` 收窄（E1）

**删除：**

- `int64_t m_gasUsed`
- `uint64_t m_gasRemaining`
- `uint64_t m_maxUsedGas`

**保留（buyGas / refund 账本）：**

- `m_effectiveGasPrice`, `m_baseFee`, `m_l1CostCharged`, `m_operatorCostLimit`
- `m_gasTipCap`, `m_gasFeeCap`, `m_hasGasFeeCap`, `m_gasPrice`
- `m_blockInfo`, rollup/blob 字段, skip 标志
- `m_floorDataGas`（floor 预检写入，`finalizeNormal` 读取）
- `m_evmcResult`（buyGas 失败路径）
- `m_call`, `m_isDepositTx`

`using OpStackTxExecutionData = OpStackFeeContext` typedef 可保留。

### 5.5 `OpStackTxFeeLedger` 签名变更

```cpp
task::Task<void> refundGas(
    TxPipelineContext& ctx,
    OpStackFeeContext const& feeCtx,
    OpStackSettlementResult const& settled);
```

- `refundGas` 读 `settled.gasRemaining` / `settled.gasUsed`，不再读 `feeCtx` gas 字段。
- `refundIsthmusOperatorCost` 读 `settled.gasUsed`。
- `buyGas` 签名不变。

### 5.6 `OpStackExecutionBridge.cpp` 删除项

- `GasPoolReturnGuard` struct 及 normal 路径用法
- `returnDepositPoolGas` free function（逻辑收入 `settleDeposit`）
- Deposit 分支内联 post-pipeline（commit/revert/nonce/gasUsed 赋值）
- Normal 路径 `co_await refundGas(...)` 直接调用

---

## 6. Error Handling

| 场景 | 行为 |
| --- | --- |
| `buyGas` 失败 | bridge：`returnGas(gasLimit, 0)`；不进入 pipeline / settle |
| `gasPool.subGas` 失败 | 行为不变；不 sub 则不 return |
| `settleNormal` / `settleDeposit` | 同步 finalize 不抛；`refundGas` 协程错误向上传播 |
| Pipeline 异常 | `OpStackOrchestrationErrorPolicy` 不变；`finalize*` 按 `exitKind` 表驱动 |

---

## 7. Testing

### 7.1 实施顺序（G1）

1. **新增** `OpStackDepositSettlementTest.cpp`（无 evmone）
2. **更新** `OpStackSettlementTest.cpp`（H1 签名；断言 `result` 而非 `feeCtx.m_gasUsed`）
3. 实现 `finalizeDeposit`、`settle*`、收窄 `feeCtx`、`refundGas` 签名
4. 精简 `OpStackExecutionBridge.cpp`
5. **更新** `OpStackTxFeeLedgerCtxTest.cpp`（`refundGas(ctx, feeCtx, settled)`）
6. 全量跑 `Deposit*`、`OpStackSettlement*`、`OpStackSettlementCharacterization*`

### 7.2 `OpStackDepositSettlementTest` 矩阵

| 用例 | exitKind | evmStatus | 断言 |
| --- | --- | --- | --- |
| `deposit_success_actual_gas` | `Completed` | `EVMC_SUCCESS` | `gasUsed` = postExecute oracle；nonce+1；state committed |
| `deposit_revert_actual_gas` | `Completed` | `EVMC_REVERT` | actual gas（≠ gasLimit）；reverted；nonce+1 |
| `deposit_entry_failure_gas_limit` | `IntrinsicRejected` | — | `gasUsed` = `originalGasLimit`；reverted；nonce+1 |

构造：最小 `TxPipelineContext` + 预置 checkpoint + mock `evmc_result`。

### 7.3 集成 oracle（须保持绿）

| 测试文件 | 覆盖 |
| --- | --- |
| `DepositMintTest.cpp` | mint + 成功 nonce + depositNonce |
| `DepositNoFeeRoutingTest.cpp` | 无 fee 路由；REVERT actual gas；entry gasLimit |
| `DepositCreateNonceTest.cpp` | CREATE deposit nonce |
| `L1AttributesDepositTest.cpp` | 成功 gasUsed > 0 |
| `OpStackSettlementCharacterizationTest.cpp` | normal 路径 oracle |

### 7.4 `settleNormal` / `settleDeposit` async 单测（Implemented）

- `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp` — 14 cases（8 normal + 6 deposit）
- `GasPoolSpy` 验证 `returnGas(settled.gasRemaining, settled.gasUsed)`
- `assertSettleNormalMatchesFinalizeOracle` 验证返回值与 sync 层一致
- 详见 `docs/superpowers/specs/2026-06-25-opstack-settle-async-test-design.md`

---

## 8. File Touch List

| 文件 | 变更 |
| --- | --- |
| `opstack/OpStackSettlement.h` | +`finalizeDeposit`, `settleNormal`, `settleDeposit`；精简 `finalizeNormal` |
| `opstack/OpStackSettlement.cpp` | 实现上述；deposit 三轨表 |
| `opstack/OpStackTxFeeLedger.h/.cpp` | `refundGas` 读 `settled` |
| `opstack/OpStackExecutionBridge.cpp` | 两条路径 `co_await settle*`；删 Guard / deposit inline |
| `test/opstack/OpStackDepositSettlementTest.cpp` | **新建** |
| `test/opstack/OpStackSettlementTest.cpp` | 适配 H1 + E1 |
| `test/opstack/OpStackTxFeeLedgerCtxTest.cpp` | 适配 `refundGas` 签名 |
| `test/cmake/OpStackTests.cmake` | 注册新测试 target |
| `test/opstack/OpStackSettleAsyncTest.cpp` | async 层 14 用例（Implemented） |
| `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` | PR2 → Implemented |

---

## 9. Risks

| 风险 | 缓解 |
| --- | --- |
| Deposit 成功路径当前可能未调 `postExecuteGasSettlement` | G1 module 测试 + `Deposit*` oracle 在迁代码前/后双跑 |
| `refundGas` 签名变更波及测试 | `OpStackTxFeeLedgerCtxTest` 同步更新 |
| `buyGas` 失败 pool 泄漏（既有） | PR2 显式 `returnGas(gasLimit, 0)` + 单测或 characterization |
| Early-exit normal `RulesRejected` gas 语义 | 现有 `OpStackSettlementTest` + characterization 保持 |

---

## 10. Compliance Checklist（PR 评审）

- [ ] `settleNormal` 为 normal 路径唯一 post-pipeline 异步入口（含 refund + pool）
- [ ] `settleDeposit` 为 deposit 路径唯一 post-pipeline 异步入口
- [ ] `OpStackFeeContext` 无 `m_gasUsed` / `m_gasRemaining` / `m_maxUsedGas`
- [ ] `finalizeNormal` 无 `ledger` / `gasPool` 参数
- [ ] Bridge 无 `GasPoolReturnGuard`、无 inline deposit settlement、无直接 `refundGas`
- [ ] `eth/` seam 不变（ADR-005）
- [ ] `OpStackDepositSettlementTest` + 现有 `Deposit*` + `OpStackSettlement*` 绿
- [ ] ADR-021 Phase 2 更新为 Implemented

---

## 11. Spec Self-Review（2026-06-25）

| 检查项 | 结果 |
| --- | --- |
| Placeholder / TBD | 无 |
| 与 PR1 spec / ADR-021 矛盾 | 无；PR2 为 ADR-021 §3 已规划 Phase 2 |
| 范围 | 单次 PR 可完成；touch list 聚焦 opstack + 测试 |
| 歧义 | `finalizeDeposit` 三轨表已显式；pre-pipeline 留 bridge 已声明 |
| PR1 实现差距 | 已记录：`refundGas` 仍在 bridge、`ledger`/`gasPool` 未用 — PR2 闭合 |

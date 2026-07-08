# OpStack Jovian DA Footprint (Block Phase / Scope C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐 Jovian **块级** DA footprint：`calcDAFootprint`、TE `BlockDAFootprintPool`、Engine `ExecutionPayload.blobGasUsed` 写入/校验、getPayload 打包预筛；闭合审计 N2 / D10b。

**Architecture:** 在 `RollupCost` 导出块级公式；TE 扩展 `beginBlock` 创建 DA pool 并注入 `daFootprintPoolSubHook`（mirror `BlockGasPool`）；Engine finalize 调用 `calcDAFootprint` 写 payload 并在 newPayload 重算校验。Receipt 层 Phase 1 不变。

**Tech Stack:** C++20，Boost.Test，CMake/CTest，bcos-evm-op，transaction-executor，bcos-engine。

**Spec:** `docs/superpowers/specs/2026-07-02-opstack-jovian-da-footprint-block-design.md`

**Prerequisite:** Phase 1 已实现（`estimatedDASize`、receipt meta、sidecar）— 见 `docs/superpowers/plans/2026-07-02-opstack-jovian-da-footprint-receipt.md`。

## Global Constraints

- **Block 阶段（Phase 2 / Scope C）**：不改 receipt meta 写入逻辑；不改 L1Block setter / operator fee。
- **Jovian 门控**：DA pool / hooks / 块级校验 **仅** `isOpStackJovian(schedule, blockTime)`；pre-Jovian **零行为变化**。
- **DA 上限**：`daFootprintLimit = blockGasLimit`（对齐 op-geth）。
- **激活块**：首 deposit 176B + 无 user tx → footprint = 0；含 user tx → error。
- **Deposit**：不占 pool、不计入 Σ；块首须为 deposit。
- **FISCO BlockHeader**：不新增 tars 字段；块级 footprint 经 `ExecutionPayload.blobGasUsed`（D8）。
- **op-geth 基线**：v1.101702.2 @ `/Users/octopus/octo/code/blockchain-impl/op-geth`。
- **命令前缀**：`rtk`

---

## File Structure

| 文件 | 职责 | 动作 |
|------|------|------|
| `bcos-evm/opstack/fee/RollupCost.h` | 声明 `calcTxDAFootprint` / `calcDAFootprint` | Modify |
| `bcos-evm/opstack/fee/RollupCost.cpp` | 实现块级公式 | Modify |
| `bcos-evm/test/opstack/CalcDAFootprintTest.cpp` | 块级公式单测 | Create |
| `transaction-executor/.../OpStackTxInputBuilder.h` | `BlockDAFootprintPool` | Modify |
| `bcos-evm/test/opstack/BlockDAFootprintPoolTest.cpp` | pool 单测 | Create |
| `bcos-evm/opstack/apply/ApplyOpStackMessage.h` | `daFootprintPoolSubHook` / `ReturnHook` | Modify |
| `bcos-evm/opstack/apply/ApplyOpStackMessage.cpp` | buyGas 前/后 wiring | Modify |
| `transaction-executor/.../OpStackTransactionExecutorImpl.h` | `beginBlock` + hook 注入 | Modify |
| `transaction-scheduler/.../SchedulerSerialImpl.h` | 传 Jovian ctx 到 `beginBlock` | Modify |
| `transaction-scheduler/.../SchedulerParallelImpl.h` | 同上 | Modify |
| `engine/bcos-engine/EngineServiceImpl.h` | finalize + 预筛 | Modify |
| `engine/bcos-engine/EngineServiceImpl.cpp` | newPayload 校验 | Modify |
| `bcos-evm/test/cmake/OpStackTests.cmake` | 注册新测试 | Modify |
| `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` | TE E2E DA reject | Modify |
| `engine/test/unittests/engine/EngineServiceTest.cpp` | payload blobGasUsed 断言 | Modify |
| `docs/superpowers/specs/2026-07-01-...-design.md` | §8 链接 Phase 2 spec | Modify |
| `bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md` | N2/D10b 闭合 | Modify |

---

## Task 1: `calcTxDAFootprint` + `calcDAFootprint`

**Files:**
- Modify: `bcos-evm/opstack/fee/RollupCost.h`
- Modify: `bcos-evm/opstack/fee/RollupCost.cpp`
- Create: `bcos-evm/test/opstack/CalcDAFootprintTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Produces:
  - `uint64_t calcTxDAFootprint(RollupCostData const&, uint64_t daFootprintGasScalar) noexcept`
  - `std::expected<uint64_t, std::string> calcDAFootprint(...)` — 签名见 spec §3.3

- [ ] **Step 1: 写失败测试**

Create `CalcDAFootprintTest.cpp`，覆盖：
- `calcTxDAFootprint({fastLzSize=200}, 400) == 124*400`（复用 Phase 1 向量）
- 激活块：mock txs = [deposit(176B isthmus calldata)] → `calcDAFootprint` = 0
- 激活块 + user tx → error
- 正常块：[jovian deposit 178B, user tx1, user tx2] → Σ 与手工计算一致
- 空 / 非 deposit 首 tx → error

- [ ] **Step 2: 注册测试，确认 FAIL**

- [ ] **Step 3: 实现 `calcTxDAFootprint`**

```cpp
uint64_t calcTxDAFootprint(RollupCostData const& data, uint64_t scalar) noexcept
{
    return estimatedDASize(data) * scalar;
}
```

- [ ] **Step 4: 实现 `calcDAFootprint`**

按 spec §3.3 逐步移植 op-geth `CalcDAFootprint`；deposit 判定用 `transaction.type()` / OpStack tx type helper（与 TE 一致）。

- [ ] **Step 5: 运行测试 PASS**

Run: `cd build && cmake --build . --target CalcDAFootprintTest && ./bcos-evm/test/CalcDAFootprintTest`

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/fee/RollupCost.h bcos-evm/opstack/fee/RollupCost.cpp bcos-evm/test/opstack/CalcDAFootprintTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "feat(opstack): add calcDAFootprint for Jovian block-level DA footprint"
```

---

## Task 2: `BlockDAFootprintPool`

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h`
- Create: `bcos-evm/test/opstack/BlockDAFootprintPoolTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Produces: `bcos::evm::opstack_tx::BlockDAFootprintPool`（API 见 spec §3.4）

- [ ] **Step 1: 写失败测试** — 镜像 `BlockGasPoolTest.cpp` 结构（consume/return/cumulative/耗尽）

- [ ] **Step 2: 实现 pool** — 复制 `BlockGasPool` 实现，rename 为 footprint 语义

- [ ] **Step 3: ctest PASS**

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "feat(opstack): add BlockDAFootprintPool for Jovian DA budget"
```

---

## Task 3: Apply hooks + TE `beginBlock`

**Files:**
- Modify: `bcos-evm/opstack/apply/ApplyOpStackMessage.h`
- Modify: `bcos-evm/opstack/apply/ApplyOpStackMessage.cpp`
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`
- Modify: `bcos-evm/test/opstack/helpers/OpStackLifecycleTestHelpers.h`（若有 pool mock）

**Interfaces:**
- Adds to `OpStackMessageRequest`:
  - `std::function<bool(uint64_t)> daFootprintPoolSubHook`
  - `std::function<void(uint64_t, uint64_t)> daFootprintPoolReturnHook`

- [ ] **Step 1: 扩展 `OpStackMessageRequest`**（两 hook 字段，默认 empty = no-op）

- [ ] **Step 2: 在 normal tx 路径 buyGas 前调用 subHook**

计算 `txFootprint`：
```cpp
if (isOpStackJovian(...) && !deposit && rollupCostData && daFootprintPoolSubHook) {
    auto fp = calcTxDAFootprint(*rollupCostData, static_cast<uint64_t>(feeParams.daFootprintGasScalar));
    if (!daFootprintPoolSubHook(fp)) { /* same exit as gas pool fail */ }
}
```

标量读取：复用 `loadOpStackFeeParams`（execution 路径 state 已就绪）。

- [ ] **Step 3: settle 成功路径 returnHook**

对称于 `gasPoolReturnGasHook`：传入 `(txFootprint, txFootprint)` 更新 cumulative。

- [ ] **Step 4: 扩展 TE `beginBlock`**

```cpp
void beginBlock(int64_t blockGasLimit, OpStackForkSchedule const& schedule, uint64_t blockTimestamp) noexcept
{
    m_blockGasPool = std::make_shared<BlockGasPool>(blockGasLimit);
    if (isOpStackJovian(schedule, blockTimestamp)) {
        m_blockDAFootprintPool = std::make_shared<BlockDAFootprintPool>(static_cast<uint64_t>(blockGasLimit));
    } else {
        m_blockDAFootprintPool.reset();
    }
}
```

`ExecuteContext` 注入两 hook（mirror gas pool 217-220 行模式）。

- [ ] **Step 5: 写 `DaFootprintEntryRejectTest.cpp`** — 小 gasLimit 块，两笔大 footprint tx，第二笔 fail

- [ ] **Step 6: Commit**

```bash
rtk git commit -m "feat(opstack): wire BlockDAFootprintPool hooks into apply and TE beginBlock"
```

---

## Task 4: Scheduler 传递 Jovian 上下文

**Files:**
- Modify: `transaction-scheduler/bcos-transaction-scheduler/SchedulerSerialImpl.h`
- Modify: `transaction-scheduler/bcos-transaction-scheduler/SchedulerParallelImpl.h`

- [ ] **Step 1: 在 `executeBlock` 块首**，从 `blockHeader.timestamp()` + ledger/executor 持有的 `OpStackForkSchedule` 调用扩展后的 `beginBlock`

> 若 schedule 暂仅存在于 TE 内，可在 `OpStackTransactionExecutorImpl` 硬编码默认 `makeIsthmusPlusForkSchedule()` 并从 `LedgerConfig` 扩展字段读取 Jovian 时间——实现时以最小 diff 为准，**测试路径**须能注入 `makeJovianPlusForkSchedule()`。

- [ ] **Step 2: 构建 scheduler + opstack tests**

- [ ] **Step 3: Commit**

```bash
rtk git commit -m "feat(scheduler): pass Jovian context to OpStack TE beginBlock"
```

---

## Task 5: Engine finalize + newPayload 校验

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`
- Modify: `engine/bcos-engine/EngineServiceImpl.cpp`
- Modify: `engine/test/unittests/engine/EngineServiceTest.cpp`

- [ ] **Step 1: getPayload Step 2i 之后写 `blobGasUsed`**

```cpp
if (isOpStackJovian(forkSchedule, blockHeader->timestamp())) {
    auto fp = calcDAFootprint(txPtrs, forkSchedule, blockHeader->timestamp());
    if (!fp) { /* fail getPayload with validation error */ }
    executionPayload.blobGasUsed = u256(*fp);
}
```

- [ ] **Step 2: 扩展 `validateExecutionPayload`**

Jovian：重算 `calcDAFootprint`；比较 `*executionPayload.blobGasUsed`；检查 `<= gasLimit`。

- [ ] **Step 3: Engine 单测** — mock Jovian 块 payload 一致/篡改拒绝

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "feat(engine): set and validate ExecutionPayload.blobGasUsed for Jovian DA footprint"
```

---

## Task 6: getPayload tx 预筛（P2-6）

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`（tx 收集 loop）

- [ ] **Step 1: deposit 注入后解析 `daFootprintGasScalar`**

从首笔 L1 attributes deposit calldata（`parseJovianL1Attributes` 或 176B 激活路径）。

- [ ] **Step 2: 对每个 candidate user tx**

```cpp
auto fp = calcTxDAFootprint(buildRollupCostData(signed), scalar);
if (scheduledFootprint + fp > gasLimit) continue; // skip tx
scheduledFootprint += fp; // 乐观累计（执行失败时 getPayload 仍靠 TE pool 兜底）
```

- [ ] **Step 3: 单测或集成测** — 超限 tx 不出现在 sealed payload

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "feat(engine): pre-filter txs by Jovian DA footprint during getPayload"
```

---

## Task 7: TE E2E + 交叉校验

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

- [ ] **Step 1: 新增 `jovian_da_footprint_block_limit`**

- Jovian schedule + L1 attributes deposit
- tx1 正常执行
- tx2 footprint 超过剩余 DA 预算 → reject
- 块内 `beginBlock`/`endBlock` 包裹

- [ ] **Step 2: 可选 Σ receipt vs calc 断言**

- [ ] **Step 3: Commit**

```bash
rtk git commit -m "test(te): Jovian block DA footprint pool rejects oversized tx"
```

---

## Task 8: 文档与审计闭合

**Files:**
- Modify: `docs/superpowers/specs/2026-07-01-opstack-jovian-da-footprint-design.md`（§8 链接）
- Modify: `docs/superpowers/specs/2026-07-02-opstack-jovian-da-footprint-block-design.md`（Status → 已实现）
- Modify: `bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md`

- [ ] **Step 1:** Phase 1 spec §8 首条改为「见 Phase 2 spec: 2026-07-02-...-block-design.md」

- [ ] **Step 2:** 审计 N2/D10b → **全量闭合**（块级 + receipt）

- [ ] **Step 3: Commit**

```bash
rtk git commit -m "docs(opstack): close N2/D10b with Jovian block DA footprint phase 2"
```

---

## Self-Review

**Spec 覆盖：**
- P2-1 `calcDAFootprint` → Task 1 ✅
- P2-2/P2-3 header/payload → Task 5 ✅
- P2-4 pool → Task 2 ✅
- P2-5 entry reject → Task 3 ✅
- P2-6 sealer 预筛 → Task 6 ✅
- 激活块 176B 特例 → Task 1 测试 ✅
- pre-Jovian 回归 → Global Constraints + Task 7 ✅

**Out of scope 确认：** P2-7 RPC、P2-8 min base fee 无 Task ✅

---

## Execution Handoff

**Plan saved to `docs/superpowers/plans/2026-07-02-opstack-jovian-da-footprint-block.md`.**

**Recommended order:** Task 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

**Parallelizable:** Task 5 可在 Task 3 完成后与 Task 6 并行（共享 Task 1 公式）。

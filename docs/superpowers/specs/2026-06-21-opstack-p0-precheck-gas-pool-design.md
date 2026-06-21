# OPStack P0：EIP-4844 preCheck + Block Gas Pool 修复设计

**日期：** 2026-06-21  
**状态：** Approved（2026-06-21）— 4844 方案 A；Gas pool 方案 B1  
**审计来源：** `bcos-evm/docs/audits/2026-06-21-l2-tx-rlp-to-receipt-comparison.md`（R3-4844-1/2/3、R3-POOL-1）  
**op-geth 锚点：** `state_transition.go:417-433`（blob preCheck）；`gaspool.go` + `buyGas:331` + `innerExecute:667-673`（gas pool）  
**范围：** Isthmus TE baseline；普通 L2 tx（非 deposit）；不含 P1（7702 CREATE、baseFee 来源）

---

## 1. 问题陈述

### 1.1 EIP-4844 preCheck 缺口（R3-4844-1/2/3）

当前 `OpStackPreCheck.cpp` 仅在 `!blobVersionedHashes.empty()` 时检查 `eip4844` 门控与 `blobGasFeeCap >= blobBaseFee`。

op-geth 在 `msg.BlobHashes != nil` 时额外要求：

| 规则 | op-geth | FB 现状 |
|------|---------|---------|
| blob tx 禁止 CREATE | `To == nil` → `ErrBlobTxCreate` | 未检查 |
| hash 列表非空 | `len==0` → `ErrMissingBlobHashes` | type 0x03 解码失败时 hashes 为空且跳过全部 blob 检查 |
| version byte = 0x01 | `kzg4844.IsValidVersionedHash` | 未检查 |

**风险：** 畸形 blob tx 在 op-geth 拒绝入块，FB 可能继续 buyGas/execute，产生共识分叉。

### 1.2 Block gas pool 缺口（R3-POOL-1）

| 项 | op-geth | FB 现状 |
|----|---------|---------|
| 占用时机 | `buyGas` 内 `gp.SubGas(gasLimit)` | 普通 L2 **从未**调用 `gasPoolSubGasHook` |
| 归还 | `gp.ReturnGas(gasRemaining, gasUsed)` | `BlockGasPool` **无 return** API |
| 作用域 | 块级共享 `GasPool` | 每笔 tx 在 `ExecuteContext::Data` **新建** pool，初始值=整块 gas limit |

**风险：** 块内多笔 tx 无法正确累计 gas；单笔 tx 也不占 pool。

---

## 2. 目标与非目标

### 目标

1. 普通 L2 blob 相关 preCheck 与 op-geth `417-433` + Cancun fee cap 行为对齐。
2. 普通 L2 在成功/失败路径上正确 SubGas/ReturnGas，块级 pool 在多 tx 场景下递减。
3. 新增 CTest 覆盖 4844 三项 + 两笔 tx 超 block gas limit。
4. 更新 `capability-matrix.md` 与 Wave 3 审计记录。

### 非目标（本 P0）

- Jovian/Osaka blob 上限、EIP-7825 gas cap
- `noBaseFee` eth_call preCheck 跳过（R3-ETHCALL-1）
- 7702 type-4 CREATE 强化（R3-7702-1，另开 P1）
- Receipt `BlobGasUsed` 字段写入
- Scheduler 以外路径（legacy executor）的 gas pool

---

## 3. 方案对比

### 3.1 EIP-4844 preCheck

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **A（推荐）** | 在 `opStackPreCheck` 增加 `hasBlobTxIntent()` + 三规则 | 最小 diff；与现有 `BlobGasBalanceTest` 同层 | 需在 builder 层保证 type 0x03 设 `web3TypedTxKind` |
| B | 在 `fillWeb3Fields` 解码失败时直接 reject | 早失败 | 不在 orchestration 层；与 geth preCheck 位置不一致 |
| C | 分散到 TE + preCheck | — | 双份逻辑，不推荐 |

**推荐 A：** 单一入口 `opStackPreCheck`，与 op-geth `preCheck` 对齐。

**`hasBlobTxIntent(input)` 定义（对齐 geth `BlobHashes != nil`）：**

```text
web3TypedTxKind == 0x03
OR authorizationListPresent 不适用
OR blobGasFeeCap != 0   // 可选；geth 用 typed tx 字段存在性
```

保守策略（推荐）：**`web3TypedTxKind == 0x03 || !blobVersionedHashes.empty()`**

- type 0x03 即使 decode 失败也进入 blob 规则 → 空 hashes → R3-4844-2 拒绝
- 非 0x03 但携带 hashes（异常 envelope）→ 同样检查

**CREATE 检测：** `isCreateKind(message.kind)` OR `state::isZeroAddress(message.recipient)` 且非 CALL（与 geth `To==nil` 对齐，需确认 `newEVMCMessage` 对 CREATE 的 recipient 编码）。

**Version hash：** 新增 `bcos-evm/opstack/Eip4844.h`：

```cpp
inline bool isValidVersionedHash(bcos::h256 const& h) noexcept {
    return h[0] == 0x01; // kzg4844.BlobCommitmentVersionKZG
}
```

### 3.2 Block gas pool

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **B1（推荐）** | 扩展 `BlockGasPool` + buyGas SubGas + 执行后 ReturnGas；pool 由 **块执行上下文** 注入 TE | 语义对齐 geth；多 tx 正确 | 需 TE API 小改 + scheduler/baseline 传 pool |
| B2 | 仅在 `opStackPreCheck` 调 hook（不占 buyGas） | diff 小 | buyGas 失败时已占 pool；无 ReturnGas；不对齐 geth |
| B3 | TE 内 static/thread_local 块 pool | 不改 scheduler | 并发/测试污染；不推荐 |

**推荐 B1：** 分两层交付：

- **B1a（bcos-evm）：** `OpStackExecuteViaHostInput` 增加 `gasPoolReturnGasHook(gasRemaining, gasUsed)`；`opStackExecuteViaHost` 在 buyGas 前/后调用（见 §4.2）。
- **B1b（transaction-executor）：** `BlockGasPool` 增加 `returnGas(returned, gasUsed)` + `cumulativeUsed()`；块级 `shared_ptr` 由 **BaselineScheduler 块执行** 创建并传入 `createExecuteContext`（若 scheduler API 暂不可改，则在 `OpStackTransactionExecutorImpl` 增加可选 `BlockExecutionScope` 成员，由 `executeBlock` 路径 reset）。

**Fallback（若 scheduler 改动过大）：** P0 最小可交付 = B1a + 单测/TE fixture 传入 **外部 shared pool**；生产 scheduler 跟进为 P0b 子任务（同 PR 若可行）。

---

## 4. 详细设计

### 4.1 EIP-4844 preCheck 变更

**文件：** `bcos-evm/opstack/Eip4844.h`（新）、`OpStackPreCheck.cpp`

**伪代码：**

```cpp
if (hasBlobTxIntent(input)) {
    if (!input.revisionConfig.eip4844)
        return Malformed;
    if (isCreateKind(input.message.kind) /* or CREATE to nil */)
        return Malformed;  // R3-4844-1
    if (input.blobVersionedHashes.empty())
        return Malformed;  // R3-4844-2
    for (auto const& h : input.blobVersionedHashes)
        if (!isValidVersionedHash(h))
            return Malformed;  // R3-4844-3
    if (!skipChecks && input.blobGasFeeCap < input.blockInfo.blobBaseFee)
        return InsufficientFunds;
}
// 移除旧的双层 empty 分支，避免 dead code
```

**错误码映射：**

| op-geth | FB |
|---------|-----|
| `ErrBlobTxCreate` | `Malformed` |
| `ErrMissingBlobHashes` | `Malformed` |
| invalid version | `Malformed` |
| `ErrBlobFeeCapTooLow` | `InsufficientFunds`（保持） |

### 4.2 Block gas pool 变更

**`BlockGasPool` API（`OpStackTxInputBuilder.h` 或移至 `bcos-evm/opstack/BlockGasPool.h`）：**

```cpp
bool tryConsume(uint64_t gas);           // 现有 SubGas
void returnGas(uint64_t gasRemaining, uint64_t gasUsed);  // 对齐 gaspool.go:52-68
uint64_t cumulativeUsed() const;
```

**调用链（普通 L2，非 deposit，非 call+noBaseFee 跳过）：**

```text
opStackExecuteViaHost:
  preCheck (不变 deposit 占 pool)
  buyGas:
    if (gasPoolSubGasHook && !gasPoolSubGasHook(gasLimit)) → OutOfGasLimit  // 新增：在 OpStackTxExecutor::buyGas 开头 OR opStackExecuteViaHost 紧接 preCheck 后
    ... existing balance deduct ...
  execute / settlement
  on exit (success, revert, entry fail):
    if (gasPoolReturnGasHook)
      gasPoolReturnGasHook(gasRemaining, gasUsed)
```

**与 geth 对齐点：**

- SubGas 时机 = buyGas 内（`gasLimit`），非 preCheck（deposit 仍在 preCheck，保持 OP- deposit 语义）
- ReturnGas：`returned = gasRemaining`（Isthmus 非 Amsterdam 路径用 refund-included 分支，与当前 settlement 一致）
- `gasUsed` = settlement 后 `txData.m_gasUsed`

**deposit 路径：** 保持 preCheck SubGas；执行后 ReturnGas(actual gasUsed) — 需确认 op-geth deposit Regolith+ `ReturnGas(0, gasUsed)` 分支；FB deposit 已在 preCheck 占 `gasLimit`，应在 deposit 分支末尾 return `gasLimit - gasUsed` 或等价。

### 4.3 TE 块级 pool 注入

**选项 1（首选）：** 扩展 `OpStackTransactionExecutorImpl::createExecuteContext`：

```cpp
createExecuteContext(..., std::shared_ptr<BlockGasPool> blockGasPool = nullptr);
// 若 null，测试单 tx 行为不变；生产 executeBlock 传入块级 pool
```

**选项 2：** `OpStackTransactionExecutorImpl` 持有 `mutable std::shared_ptr<BlockGasPool> m_blockGasPool`，新增：

```cpp
void beginBlock(int64_t gasLimit);
void endBlock();
```

由 BaselineScheduler 在块首/块尾调用。

**测试：** `TestOpStackTransactionExecutorFixture` 或新 `BlockGasPoolTest`：同一 `shared_ptr`，tx1 gasLimit=100k + tx2 gasLimit=block-100k+1 → 第二笔 `OutOfGasLimit`。

### 4.4 测试计划

| 用例 | 文件 | 断言 |
|------|------|------|
| blob + CREATE | `OpStackPreCheck4844Test.cpp` | Malformed |
| type 0x03 + empty hashes | 同上 | Malformed |
| hash[0]!=0x01 | 同上 | Malformed |
| 合法 hash + low blob cap | 复用 `BlobGasBalanceTest` | InsufficientFunds |
| 合法 blob 通过 preCheck | 同上 | nullopt |
| 两笔 tx 超 block gas | `BlockGasPoolTest.cpp` | 第二笔 fail |
| buyGas 失败后 pool 归还 | 单元 | remaining 恢复 |
| deposit 仍占 pool | `DepositTxPreCheckTest` | 回归 |

### 4.5 文档与矩阵

- `capability-matrix.md`：EIP-4844 blob orchestration 行更新测试 ref
- `2026-06-21-l2-tx-rlp-to-receipt-comparison.md`：R3-4844、R3-POOL-1 标 CLOSED
- `2026-06-21-opstack-isthmus-reaudit-wave3.md`：P0 闭合

---

## 5. 实现任务分解（供 writing-plans）

| ID | 任务 | 层级 | 估时 |
|----|------|------|------|
| T1 | 新增 `Eip4844.h` + preCheck 三规则 | bcos-evm | 0.5d |
| T2 | `OpStackPreCheck4844Test` 6 用例 | test | 0.5d |
| T3 | 扩展 `BlockGasPool::returnGas` + cumulative | TE/opstack | 0.5d |
| T4 | buyGas SubGas + execute 结束 ReturnGas hooks | bcos-evm | 0.5d |
| T5 | TE 块级 pool 注入（scheduler 或 beginBlock API） | TE + scheduler | 1d |
| T6 | 多 tx block gas CTest + TE fixture | test | 0.5d |
| T7 | 矩阵 + 审计 doc 更新 | docs | 0.25d |

**建议 PR 切分：**

- **PR-1：** T1+T2（4844 only，可独立合并）
- **PR-2：** T3–T7（gas pool，依赖 scheduler 触点确认）

---

## 6. 风险与回滚

| 风险 | 缓解 |
|------|------|
| CREATE 判定与 `newEVMCMessage` 不一致 | T1 前读 `TransactionExecutorImpl.cpp` CREATE 编码；单测 CREATE+blob |
| scheduler 无块上下文 | Fallback：document + TE fixture 证明 pool 逻辑；follow-up PR 接 scheduler |
| deposit ReturnGas 回归 | 保留 `DepositTxPreCheckTest` + 新增 deposit 两阶段 pool 断言 |
| 4844 误杀合法 tx | 使用 op-geth 向量：version 0x01 + 非空 hashes |

---

## 7. 批准检查项

- [x] 4844：`hasBlobTxIntent` 采用 `0x03 || !hashes.empty()` — **已批准（方案 A）**
- [x] Gas pool：B1（buyGas SubGas + ReturnGas + 块级 shared pool）— **已批准**
- [x] Scheduler：`SchedulerSerialImpl` 块首/块尾调用 `beginBlock`/`endBlock`（与 gas pool PR 同批交付）

---

**Spec self-review：** 无 TBD；范围限于 P0；4844 与 pool 可拆分 PR；CREATE 判定需在 T1 实现前核实 `newEVMCMessage`。

**下一步（批准后）：**  invoke `writing-plans` 生成逐步 implementation plan。

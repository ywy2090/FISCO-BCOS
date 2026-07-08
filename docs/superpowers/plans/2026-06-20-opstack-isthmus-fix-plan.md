# OPStack Isthmus 审计 — Remediation 执行计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans。每步用 checkbox 跟踪；完成一项后在 work-list 标 `[x]`。

**Goal:** 闭合 OP Isthmus 审计全部 P0（OP-01–09），主判定升至 ≥ ⚠️ 有条件通过。

**Architecture:** 按 Phase 顺序修复 orchestration wiring；kernel-verify 项只补 OP 路径测试/接线，不重做 ETH 已闭合内核。每项捆绑 fix + 验收测试。

**关联文档：**

- 设计 spec：`docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-remediation-design.md`
- 工作列表：`bcos-evm/docs/audits/2026-06-20-opstack-isthmus-work-list.md`
- 审计报告：`bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`
- ETH 复审计：`bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit-reaudit.md`

**Tech Stack:** C++、`opStackExecuteViaHost` 路径、Boost.Test、`TestOpStackTransactionExecutorFixture`

**命令前缀：** 使用 `rtk`（仓库 CLAUDE.md）

---

## Global Constraints

- **生产路径：** `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` → `OpStackTxExecutor`
- **Profile：** Isthmus = `makeIsthmusRevisionConfig()`；operator fee 需 `m_isIsthmus=true`
- **op-geth 对照：** v1.101702.2 — `core/types/rollup_cost.go`、`core/state_transition.go`
- **禁止** 重复实现 ETH 复审计已闭合的 6780/2537 内核（除非 OP-08 验证失败）
- **Done：** 代码 + 验收测试 PASS + matrix（若适用）

---

## Phase 1 — OP-01: Isthmus operator fee 生产接线

**Work-list ID:** OP-01  
**审计引用:** W1, S3  
**轨道:** orchestration

### 问题

`OpStackTxExecutor::m_isIsthmus` 默认 `false`。生产路径 `opStackExecuteViaHostTx()` 从未设为 `true`，导致 `buyGas` / `refundIsthmusOperatorCost` 跳过 operator fee；测试手动设 `m_isIsthmus=true` 造成假绿。

### 修改

**文件:** `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`

在 `opStackExecuteViaHostTx()` 构建 `input` 后、调用 `opStackExecuteViaHost` 前：

```cpp
input.opTxExecutor.m_isIsthmus = true;  // Isthmus executor always activates operator fee
```

### 验收测试

- [ ] `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` — operator fee E2E（`OP_OPERATOR_FEE_RECIPIENT` 余额变化）
- [ ] `bcos-evm/test/opstack/RefundIsthmusTest.cpp` — 仍 PASS

### Done

- [x] OP-01 work-list `[x]`

---

## Phase 2 — Tx 费用模型（OP-02, OP-06, OP-07）

### OP-02: Rollup L1 cost 用 signed RLP tx 字节

**审计引用:** R1

#### 问题

`buildRollupCostData()` 对 Web3 tx 使用 `extraTransactionBytes`（= `encodeForSign()` 无签名）或 `tx.input()`。op-geth Fjord L1 cost 使用 **signed typed tx RLP**（`MarshalBinary` / `encode(tx)`）。

**当前代码:**

```109:117:transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h
inline std::optional<RollupCostData> buildRollupCostData(protocol::Transaction const& tx)
{
    auto const extra = tx.extraTransactionBytes();
    if (!extra.empty())
    {
        return newRollupCostData(extra);
    }
    return newRollupCostData(tx.input());
}
```

#### 修改方向

1. 对 `Web3Transaction`：从 `protocol::Transaction` 字段 + `signature` 重建 signed RLP（参考 `bcos-rpc/.../Web3Transaction.cpp` 的 `encode()`），再 `newRollupCostData(signedBytes)`。
2. 或：若 `extraTransactionHash` 对应 signed RLP 的 keccak，从 tars 层暴露 signed bytes getter。
3. Deposit tx：保持 deposit 专用字节源（非 Web3 signed RLP）。

**文件:** `OpStackTxInputBuilder.h`（可能需 `Web3Transaction` 编码 helper）

#### 验收测试

- [ ] 新增/扩展：signed EIP-1559 tx 的 rollup bytes 与 op-geth `NewRollupCostData` 一致
- [ ] `bcos-evm/test/opstack/OpStackFeeTest.cpp` — PASS
- [ ] `TestOpStackTransactionExecutorFixture` — L1 fee receipt 字段 E2E

#### Done

- [ ] OP-02 work-list `[x]`

---

### OP-06: Blob buyGas + type-0x03 字段传播

**审计引用:** B1

#### 问题

1. `OpStackTxExecutor::buyGas` 未扣 blob gas（`blobBaseFee × blobGas`）。
2. `fillWeb3Fields` 未从 transaction 传播 `blobVersionedHashes` / `maxFeePerBlobGas` 到 `OpStackExecuteViaHostInput`。
3. Executor 路径未为 EIP-4844 填充 blob 字段。

#### 修改

**文件:**

- `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h` — `fillWeb3Fields` 解析 0x03 字段
- `bcos-evm/opstack/OpStackTxExecutor.cpp` — `buyGas` 增加 blob 扣款（对照 op-geth `buyGas`）
- `OpStackTransactionExecutorImpl.h` — 确保 blob 字段进入 input

#### 验收测试

- [ ] `bcos-evm/test/opstack/BlobGasBalanceTest.cpp` — precheck 仍 PASS
- [ ] 新增：buyGas 扣 blob 余额 E2E（`TestOpStackTransactionExecutorFixture` 或新用例）

#### Done

- [ ] OP-06 work-list `[x]`

---

### OP-07: 7702 intrinsic 25000×n

**审计引用:** A1

#### 问题

`OpStackExecuteViaHost.cpp` 使用 `TX_AUTH_TUPLE_GAS = 12'500`；op-geth Prague+ 为 **25000×n** 预扣（含 existence refund 语义差异）。

**当前代码:**

```23:35:bcos-evm/opstack/OpStackExecuteViaHost.cpp
constexpr uint64_t TX_AUTH_TUPLE_GAS = 12'500;
// ...
return base + authTuples * TX_AUTH_TUPLE_GAS;
```

#### 修改

- 对齐 op-geth `SetCodeIntrinsicGas` / `IntrinsicGas` 7702 分支
- 常量改为 `25'000` 或复用 `bcos-evm/eth` 已有 intrinsic 计算（若 Isthmus profile 已含）

**文件:** `OpStackExecuteViaHost.cpp`（或统一到 `gas::computeTxIntrinsicGas`）

#### 验收测试

- [ ] `bcos-evm/test/opstack/Eip7702PreCheckTest.cpp`
- [ ] 7702 intrinsic gas 断言 vs op-geth

#### Done

- [ ] OP-07 work-list `[x]`

---

## Phase 3 — Deposit 共识语义（OP-03, OP-04, OP-05）

### OP-03: Deposit 成功 sender nonce +1

**审计引用:** task4 成功路径

#### 问题

成功路径（`EVMC_SUCCESS`）commit 后未 `nonce+1`；op-geth deposit 成功亦 bump nonce。

**当前:** 仅失败路径 `state.set_nonce(nonce + 1)`（约 174–175 行）。

#### 修改

**文件:** `bcos-evm/opstack/OpStackExecuteViaHost.cpp` — 成功分支 commit 前/后 bump sender nonce。

#### 验收测试

- [ ] `bcos-evm/test/opstack/DepositMintTest.cpp` — 成功 deposit nonce+1
- [ ] 新增断言若现有用例未覆盖

#### Done

- [ ] OP-03 work-list `[x]`

---

### OP-04: Deposit EVM REVERT → actual gasUsed

**审计引用:** D1

#### 问题

失败分支固定 `txData.m_gasUsed = gasLimit`（约 176–177 行）。op-geth 使用 **实际消耗 gas**（`gasLimit - gas_left` 经 settlement）。

**当前测试（假覆盖）:**

```127:128:bcos-evm/test/opstack/DepositNoFeeRoutingTest.cpp
BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
BOOST_CHECK_EQUAL(output.gasUsed, 50'000);
```

REVERT 合约仅 `PUSH1 0 PUSH1 0 REVERT`，实际 gas ≪ gasLimit。

#### 修改

1. 失败分支用 `postExecuteGasSettlement` 或 `gasLimit - gas_left` 计算 actual gas。
2. 修正 `DepositNoFeeRoutingTest` 期望为 actual gas（非 gasLimit）。

#### Done

- [ ] OP-04 work-list `[x]`

---

### OP-05: Deposit entry 失败 nonce+1 + gasLimit

**审计引用:** D2

#### 问题

`executeEntryChecks` 失败（intrinsic OOG 等）时：`state.revert()` 后直接返回，**无 nonce bump**，gas 语义可能不符 op-geth deposit entry failure。

**当前代码:** 约 133–138 行。

#### 修改

- Entry 失败：仍 bump nonce（deposit 语义）
- `gasUsed = gasLimit`（对照 op-geth `ApplyTransaction` deposit 分支）

#### 验收测试

- [ ] 新增 `DepositEntryFailureTest` 或扩展 `DepositNoFeeRoutingTest` — intrinsic OOG entry 失败

#### Done

- [ ] OP-05 work-list `[x]`

---

## Phase 4 — Kernel verify（OP-08, OP-09）

### OP-08: OP 路径验证 6780 + 2537

**审计引用:** task8 #10, #21  
**轨道:** kernel-verify

#### 范围

ETH 复审计 @ `f989f073f` 已闭合 6780 SELFDESTRUCT、2537 MSM gas。**本项仅验证** Isthmus OP 路径无回归。

#### 步骤

- [ ] 以 `makeIsthmusRevisionConfig()` 跑 `Eip2537KernelTest` / 6780 smoke（`Bcos6780SelfdestructTest` 或 OP fixture）
- [ ] 若 FAIL → 查 OP wiring（revision config、precompile registry），**不重写** eth 内核除非接线错误
- [ ] 记录结论于 work-list 备注

#### Done

- [ ] OP-08 work-list `[x]`（验证通过或修复接线后通过）

---

### OP-09: OP 路径 2929 tx-entry wiring

**审计引用:** task8 #1–3

#### 问题

`applyDefaultTxProps()` 存在于 `OpStackTxInputBuilder.h` 但 **生产 executor 未调用**；2929 warm tx-entry 可能未生效。

#### 修改

**文件:** `OpStackTransactionExecutorImpl.h` — 在 `fillWeb3Fields` 后调用 `opstack_tx::applyDefaultTxProps(input)`。

可选：`applyDefaultTxProps` 扩展 warm precompile / access list 传播（对照 ETH `TxFeaturePrepare`）。

#### 验收测试

- [ ] `bcos-evm/test/opstack/OpStackTxPropsTest.cpp`（若存在）或新建
- [ ] Executor E2E warm destination 断言

#### Done

- [ ] OP-09 work-list `[x]`

---

## Phase 5 — Matrix + Quality（OP-10–15）

### OP-10: capability-matrix 增补 4 行

- [ ] 更新 `bcos-evm/capability-matrix.md`（见 work-list Matrix 预览表）
- [ ] 运行 `check-capability-matrix.sh` / CI gate

### OP-11–15: Quality（P1，可分期）

| ID | 任务 |
|----|------|
| OP-11 | Fjord min-bound / parity 向量于 `OpStackFeeTest` |
| OP-12 | `L1AttributesDepositTest` 精确 fee literal |
| OP-13 | Receipt `operatorFeeScalar/Constant`（若对齐 specs） |
| OP-14 | L1Block 元数据 slot 完整仿真 |
| OP-15 | 移除 8 个测试中冗余 `m_isIsthmus=true`（依赖 OP-01） |

---

## 验证命令

```bash
# 构建测试目标（路径依 CMake 配置调整）
rtk cmake --build build --target test-opstack-transaction-executor -j$(sysctl -n hw.ncpu)
rtk cmake --build build --target bcos-evm-test -j$(sysctl -n hw.ncpu)

# OPStack 单元测试
rtk test ./build/bcos-evm/test/opstack/*Test --report_level=detailed

# Executor fixture
rtk test ./build/transaction-executor/tests/test-opstack-transaction-executor --report_level=detailed

# Matrix gate
rtk ./bcos-evm/scripts/check-capability-matrix.sh
```

---

## 复审计检查清单

P0 全部 `[x]` 后：

- [ ] 重跑 Isthmus 审计 Part 4 双轨统计
- [ ] 可裁决行无 🔴
- [ ] 更新 `2026-06-20-opstack-isthmus-audit.md` 状态与修复引用
- [ ] `DepositNoFeeRoutingTest` 无假覆盖断言

---

**Last updated:** 2026-06-20

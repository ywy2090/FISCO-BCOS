# OPStack R3-DEP-1：CREATE Deposit Nonce 设计

**日期：** 2026-06-21  
**状态：** Approved — 方案 A（test-first）；测试范围 A（仅 `DepositCreateNonceTest`）  
**审计来源：** `R3-DEP-1` @ `2026-06-21-opstack-isthmus-reaudit-wave3.md`  
**op-geth 锚点：** `state_transition.go:598-602`（CALL bump）；`evm.go:526-530` + `654-655`（CREATE bump + 地址）  
**FB 锚点：** `OpStackExecuteViaHost.cpp:153-207`；`EthHost.cpp:277-280`（CREATE 地址，不 bump sender）

---

## 1. 问题陈述

Wave 3 审计发现：**CALL deposit** 的 sender nonce 已有 `DepositMintTest`（3→4）覆盖；**CREATE deposit**（`OpStackDepositTx.to == nullopt`）成功路径缺少用例。

| 路径 | op-geth | FB 现状 |
|------|---------|---------|
| CALL deposit 成功 | Call 前 `SetNonce(from, n+1)` | 执行后编排层 `+1` | ✅ 已测 |
| CREATE deposit 成功 | `evm.Create` 内 `SetNonce(caller, n+1)`；地址用 bump 前 nonce | 内核不 bump sender；编排层成功分支无条件 `+1` | ⚠️ 未测 |

**风险：** 若内核与编排层均 bump → nonce +2（共识分叉）；若均不 bump → +0。代码阅读倾向编排层单次 +1 已正确，**需 CREATE deposit 测试证实**。

---

## 2. 目标与非目标

### 目标

1. 新增 `DepositCreateNonceTest`：CREATE deposit 成功后 sender nonce **恰好 +1**。
2. 断言 `receiptMeta.depositNonce` = 执行前 nonce（与 `DepositMintTest` 一致）。
3. 测试通过且无逻辑缺陷 → 闭合 R3-DEP-1（可能零 prod 改动）。
4. 测试失败 → 按 §5 最小条件修复。
5. 更新 Wave 3 审计与 task4 附录。

### 非目标

- TE `TestOpStackTransactionExecutorFixture` RLP CREATE deposit（另开可选 follow-up）
- CREATE2 deposit、Regolith 前语义、普通 L2 CREATE
- CREATE deposit REVERT / entry 失败 nonce（已有 CALL 路径覆盖；CREATE 失败另项）
- `depositNonce` 协议/RPC 字段扩展

---

## 3. 方案（已批准）

**方案 A — Test-first 验证（推荐并已选）**

1. 写失败/验证测试
2. 运行；若 3→4 PASS → 仅文档闭合
3. 若 3→5 或 3→3 → 按 §5 修复

不 preemptive 按 `message.kind` 改 bump（避免漏 +1）。

---

## 4. 测试设计

### 4.1 文件

- **Create:** `bcos-evm/test/opstack/DepositCreateNonceTest.cpp`
- **Modify:** `bcos-evm/test/CMakeLists.txt`（`DepositMintTest` 模式注册 `DepositCreateNonce`）

### 4.2 Fixture

```cpp
// sender nonce = 3
input.message.kind = EVMC_CREATE;
input.message.sender = sender;
input.message.recipient = {};           // zero — legacy CREATE
input.message.code_address = {};
input.message.gas = 100'000;
input.web3TypedTxKind = DEPOSIT_TX_TYPE;
input.depositTx = OpStackDepositTx{
    .from = sender,
    .to = std::nullopt,                 // CREATE deposit
    .gas = 100'000,
    .data = minimalInitCode,            // e.g. PUSH1 0 PUSH1 0 RETURN
};
input.revisionConfig.revision = EVMC_CANCUN; // Isthmus profile 可换 makeIsthmusRevisionConfig()
```

### 4.3 断言

| # | 断言 |
|---|------|
| 1 | `output.evmcResult.status_code == EVMC_SUCCESS` |
| 2 | `nonceFromDiff(sender) == initialNonce + 1`（如 3→4） |
| 3 | `*output.receiptMeta.depositNonce == initialNonce`（执行前快照） |
| 4 | （可选）`create_address` 非零 / 新账户在 stateDiff 中 |

### 4.4 回归

`DepositMintTest`、`DepositNoFeeRoutingTest`、`DepositTxPreCheckTest` 全 PASS。

---

## 5. 条件修复（仅测试失败时）

| 观测 | 修复 |
|------|------|
| nonce 3→5 | CREATE 成功路径跳过编排层 `set_nonce`；若内核未 bump，则在 `EthHost` CREATE 入口补 **一次** geth 式 bump（禁止双处同时 bump） |
| nonce 3→3 | 保留编排层 +1；若内核应 bump 则在内核补 |
| 3→4 但 create 地址错 | 对齐 bump 与 `predictLegacyCreateAddress` 顺序（geth：地址用 n，再 set n+1） |

**禁止：** 未跑测试前删除 CREATE 成功路径的编排层 bump。

---

## 6. 文档更新

- `2026-06-21-opstack-isthmus-reaudit-wave3.md`：R3-DEP-1 → CLOSED
- `bcos-evm/docs/audits/_work/task4-deposit.md`：Wave 3 CREATE 行更新
- `capability-matrix.md`：deposit 行追加 `DepositCreateNonceTest`（若闭合）

---

## 7. 批准记录

- [x] 方案 A test-first
- [x] 测试范围：仅 `DepositCreateNonceTest`（不含 TE fixture）
- [x] 非目标范围确认

**下一步：** `writing-plans` → 实现计划 → 实现

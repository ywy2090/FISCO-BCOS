# OPStack TE Baseline（Isthmus）规范合规审计报告

**日期：** 2026-06-20（初审计）；**复审计：** 2026-06-21 @ `54e17a62c`  
**分支/commit：** `worktree-feat-evm-refactor` / `54e17a62c`（初审计 `f989f073f`；remediation OP-01～15 已合入）  
**op-geth：** v1.101702.2 @ `e8800cffe`  
**geth：** v1.17.3  
**Besu：** tag 26.6.0  
**optimism/specs：** `689a96f6d3aad7cf7b26525e2d7e0b5d581ae057`  
**范围：** OpStack TE baseline 端到端（`transaction-executor` + `bcos-evm`），Isthmus profile  
**设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`  
**执行计划：** `docs/superpowers/plans/2026-06-20-opstack-isthmus-audit.md`（Subagent-Driven 2026-06-20；Task 0–11 复跑 2026-06-21）

**详细矩阵 / 断言表：** `bcos-evm/docs/audits/_work/task*.md`、`test-inventory-opstack.md`

---

## Part 0 — 执行摘要

### 审计时间线

| 阶段 | Commit | 主判定 |
|------|--------|--------|
| 初审计 | `f989f073f` | ❌ 不通过（≥8 🔴） |
| Remediation | OP-01～15 | P0 全闭合 |
| **复审计** | **`54e17a62c`** | **⚠️ 有条件通过** |

### 可裁决行统计（深审 + inherited smoke，不含纯 ⚪）— 复审计 @ `54e17a62c`

| 指标 | 初审计 | 复审计 |
|------|--------|--------|
| 审计行数（可裁决） | ~30 | ~33（含 S1–S3 + warm_access 行） |
| ✅ 一致 | ~18 | **~26** |
| 🟡 警告 | ~10 | **~7** |
| 🔴 阻断 | **≥8** | **0** |
| 📋 设计选择 | 若干 | 若干（matrix deviation 已文档化） |
| **主判定** | **❌ 不通过** | **⚠️ 有条件通过** |

### 待决行统计

| 类型 | 行数 |
|------|------|
| blocked: ETH audit pending | **0** |
| matrix-patch-pending（增补 S1–S3） | **0**（已合入 `capability-matrix.md` OP-10） |

### 初审计 Top 5 阻断项 — 复审计闭合状态

| # | 初审计 🔴 | 复审计 | 证据 |
|---|-----------|--------|------|
| 1 | `m_isIsthmus` 生产未接线 | **✅ 闭合** OP-01 | `OpStackTransactionExecutorImpl.h:210-211`；`TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` |
| 2 | Rollup L1 cost 字节源错误 | **✅ 闭合** OP-02 | `OpStackTxInputBuilder.h:119-124`；`OpStackTxInputBuilderTest::buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` |
| 3 | Deposit gas/nonce 语义 | **✅ 闭合** OP-03～05 | `OpStackExecuteViaHost.cpp:135-189`；`DepositNoFeeRoutingTest` |
| 4 | Blob buyGas + 0x03 传播 | **✅ 闭合** OP-06 | `OpStackTxExecutor.cpp:67-75`；`OpStackTxInputBuilder.h:218-226`；`BlobGasBalanceTest` |
| 5 | 7702 intrinsic 12500×n | **✅ 闭合** OP-07 | `calcAuthTupleIntrinsicGas` = 25000×n；`OpStack7702ExecuteViaHostPropagationTest` |

**继承 ETH 内核 🔴 → ✅：** EIP-2537 MSM 折扣表（`BlsGas.h`）；EIP-6780 `selfdestruct`（`EthHost.cpp:190-197`）；`OpStack67802537KernelSmokeTest`

### 测试断言审计（Part 3 摘要）— 复审计

| 指标 | 初审计 | 复审计 |
|------|--------|--------|
| 测试文件 | 29 | **30**（+ `OpStack67802537KernelSmokeTest`） |
| 用例 | 65 | **86** |
| ✅ / 🟡 / 🔴 断言 | 49 / 15 / **1** | **74 / 12 / 0** |

### 复审计放行条件（⚠️ → ✅）

1. HEAD 重建并跑通 opstack + `TestOpStackTransactionExecutorFixture` 全量 CTest  
2. 协议层 `TransactionReceipt` 暴露 `operatorFeeScalar` / `operatorFeeConstant`（编排层 meta 已有）  
3. （可选）4844 full executor E2E；Fjord Solidity parity 精确向量 105484

---

## Part 1 — 合规矩阵

完整 Part 1 行见各 Task 笔记（可直接粘贴）：

| Task | 域 | 文件 |
|------|-----|------|
| 1 | Isthmus profile + executor wiring (S3) | `_work/task1-executor-wiring.md` |
| 2 | Fjord L1 cost + rollup | `_work/task2-fjord-l1-cost.md` |
| 3 | Operator fee (S1) | `_work/task3-operator-fee.md` |
| 4 | Deposit tx | `_work/task4-deposit.md` |
| 5 | L1Block + L1 attributes (S2) | `_work/task5-l1block-attributes.md` |
| 6 | 7623 floor + receipt | `_work/task6-floor-gas-receipt.md` |
| 7 | 7702 precheck + 4844 blob | `_work/task7-7702-4844.md` |
| 8 | Inherited smoke | `_work/task8-inherited-smoke.md` |
| 9 | unsupported / profile-only | `_work/task9-unsupported.md` |

### 增补能力（已合入 matrix OP-10）— 摘要 @ `54e17a62c`

| 能力 | 状态 |
|------|------|
| S1 Operator fee (Isthmus) | ✅ explicit（wiring + 公式 + buy/refund/route） |
| S2 L1 attributes system deposit | ✅ explicit（OP-14 IL1Block + OP-12 literal fee E2E） |
| S3 Executor Isthmus wiring | ✅ explicit（`m_isIsthmus` + `applyDefaultTxProps` + `warm_access`） |

---

## Part 2 — 偏离项详情（🟡 精选，复审计无 🔴）

详见各 `task*.md` Part 2。初审计 🔴 均已闭合；**仍开放 🟡：**

| ID | 现象 | 严重度 | Task |
|----|------|--------|------|
| D3-2 | `OpStackReceiptMeta` 有 scalar/constant；`makeReceipt` 仅 `setOperatorFee` | 🟡 协议层 | 6 |
| D2-1 | 无 `TestFjordL1CostSolidityParity` 精确向量（105484/2463） | 🟡 测试 | 2 |
| D2-2 | signed RLP → L1 fee 无 TE E2E 精确链 | 🟡 测试 | 2 |
| D7-1 | 4844 preCheck 缺 hash 版本 / blob CREATE；无 full executor E2E | 🟡 测试 | 7 |
| D5-4 | L1 attributes deposit 未断言 `depositNonce`/nonce | 🟡 测试 | 5 |
| D1-1 | TE fixture revert/hard-fail 未断言 operator fee | 🟡 测试 | 1/3 |
| D8-1 | inherited #14/#18/#20 profile flag 稀疏（evmone-delegated） | 🟡 文档 | 8 |

**Out of scope（⚪，非阻断）：** GPO `0x4200…000F`、`setFeature`、`proxyAdmin*`、Bedrock/Jovian setter、`setIsthmus()` 升级迁移、`L1GasUsed` deprecated receipt 字段

---

## Part 3 — 测试断言审计

完整 86 行表见 `_work/task10-assertions.md` 与 `_work/test-inventory-opstack.md`。

**复审计要点：**
- `DepositNoFeeRoutingTest` — REVERT `gasUsed=21'000` ✅（初审计 🔴 已修正）
- 手动 `m_isIsthmus=true` — 仅 3 处（`RefundIsthmusTest`、`OpStackSettlementTest`×2），直连 `OpStackTxExecutor` 单元测，**非** wiring 缺口
- `TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` — TE operator fee E2E ✅
- `L1AttributesDepositTest` — L1/operator literal + recipient balance ✅

---

## Part 4 — 后续动作

### P0 代码修复（🔴）— 全部闭合 @ `54e17a62c`

| OP ID | 项 | 状态 |
|-------|-----|------|
| OP-01 | `m_isIsthmus` 生产接线 | ✅ |
| OP-02 | signed RLP rollup bytes | ✅ |
| OP-03～05 | deposit nonce/gas 语义 | ✅ |
| OP-06 | blob buyGas + 0x03 传播 | ✅ |
| OP-07 | 7702 intrinsic 25000×n | ✅ |
| OP-08 | 2537/6780 内核 | ✅ |
| OP-09 | 2929 warm_access + applyDefaultTxProps | ✅ |

### P1 补测 / 协议 parity（🟡）

- 协议 `TransactionReceipt` 暴露 `operatorFeeScalar` / `operatorFeeConstant`
- `TestFjordL1CostSolidityParity` 精确向量；signed RLP → L1 fee TE E2E
- type-0x03 blob full executor E2E；4844 preCheck 形状校验补全
- L1 attributes deposit `depositNonce`/nonce 断言；TE revert operator fee 断言
- HEAD 重建 + 全量 opstack CTest CI gate

### Matrix — 已合入（OP-10）

见 `bcos-evm/capability-matrix.md` 行：OPStack operator fee、L1 attributes deposit、Isthmus executor integration、Rollup L1 cost tx bytes (Fjord)。

### ETH 交叉引用

- inherited 行已引用 `2026-06-20-eth-reference-cancun-plus-audit.md`；无 `blocked: ETH audit pending`
- 2537/6780 OP 行随 ETH P0 闭合 → OP smoke ✅

---

**审计状态：** 初版 + 复审计完成（Subagent-Driven Task 0–11 @ `54e17a62c`）。Part 1 全表见 `_work/task*.md`。

# OPStack TE Baseline（Isthmus）规范合规审计报告

**日期：** 2026-06-20  
**分支/commit：** `worktree-feat-evm-refactor` / `f989f073f`  
**op-geth：** v1.101702.2 @ `e8800cffe`  
**geth：** v1.17.3  
**Besu：** tag 26.6.0  
**optimism/specs：** `689a96f6d3aad7cf7b26525e2d7e0b5d581ae057`  
**范围：** OpStack TE baseline 端到端（`transaction-executor` + `bcos-evm`），Isthmus profile  
**设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`  
**执行计划：** `docs/superpowers/plans/2026-06-20-opstack-isthmus-audit.md`（Subagent-Driven 2026-06-20）

**详细矩阵 / 断言表：** `bcos-evm/docs/audits/_work/task*.md`、`test-inventory-opstack.md`

---

## Part 0 — 执行摘要

### 可裁决行统计（深审 + inherited smoke，不含纯 ⚪）

| 指标 | 值 |
|------|-----|
| 审计行数（可裁决） | ~30 |
| ✅ 一致 | ~18 |
| 🟡 警告 | ~10 |
| 🔴 阻断 | **≥8** |
| 📋 设计选择 | 若干（matrix deviation 已文档化） |
| **主判定** | **❌ 不通过** |

### 待决行统计

| 类型 | 行数 |
|------|------|
| blocked: ETH audit pending | **0**（ETH 初审计报告已存在，已交叉引用） |
| matrix-patch-pending（增补 S1–S3） | **3** |

### Top 5 阻断项

1. **🔴 `m_isIsthmus` 生产未接线** — `OpStackTransactionExecutorImpl` 未设 `input.opTxExecutor.m_isIsthmus = true` → Isthmus operator fee 端到端失效（`task1-executor-wiring.md`）
2. **🔴 Rollup L1 cost 字节源错误** — `buildRollupCostData` 用 `encodeForSign` 而非 signed `MarshalBinary` RLP tx（`task2-fjord-l1-cost.md`）
3. **🔴 Deposit 失败/入口 gas 记账** — EVM REVERT 用 `gasLimit` 非 actual gas；entry 失败无 nonce bump（`task4-deposit.md`）
4. **🔴 EIP-4844 blob 费未扣** — `buyGas` 无 blob gas 扣款；executor 未传播 type-0x03 blob 字段（`task7-7702-4844.md`）
5. **🔴 7702 intrinsic gas** — 新 authority 账户少扣 12500/元组（12500 vs op-geth 25000 预扣）（`task7-7702-4844.md`）

**继承 ETH 内核 🔴（OP smoke）：** EIP-2537 MSM 线性 gas、EIP-6780 `selfdestruct` stub（`task8-inherited-smoke.md`）

### 测试断言审计（Part 3 摘要）

| 指标 | 值 |
|------|-----|
| 测试文件 | 29 |
| 用例 | 65 |
| ✅ / 🟡 / 🔴 断言 | 49 / 15 / 1（`task10-assertions.md`） |

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

### 增补能力（待 matrix 合入）— 摘要

| 能力 | 状态 |
|------|------|
| S1 Operator fee (Isthmus) | 🔴（wiring）+ ✅（公式实现） |
| S2 L1 attributes system deposit | 🟡（fee 联动 ✅，继承 deposit 🔴，L1Block 仿真不完整） |
| S3 Executor Isthmus wiring | 🔴 |

---

## Part 2 — 偏离项详情（🟡/🔴 精选）

详见各 `task*.md` Part 2。核心 🔴：

| ID | 现象 | 规范/金标准 | FB 指针 | 严重度 |
|----|------|-------------|---------|--------|
| W1 | `m_isIsthmus` 未设 | Isthmus operator fee MUST | `OpStackTransactionExecutorImpl.h:210` | 🔴 |
| R1 | L1 cost 用 encodeForSign | optimism Fjord signed RLP tx | `OpStackTxInputBuilder.h:109-117` | 🔴 |
| D1 | Deposit REVERT gasUsed=gasLimit | op-geth Regolith+ actual gas | `OpStackExecuteViaHost.cpp:176-177` | 🔴 |
| D2 | Deposit entry 失败无 nonce+1 | op-geth deposit failure | `OpStackExecuteViaHost.cpp:133-138` | 🔴 |
| B1 | 无 blob gas buyGas 扣款 | op-geth `:319-329` | `OpStackTxExecutor.cpp buyGas` | 🔴 |
| A1 | 7702 intrinsic 12500×n | op-geth 25000×n 预扣 | `OpStackExecuteViaHost.cpp` intrinsic | 🔴 |

---

## Part 3 — 测试断言审计

完整 65 行表见 `_work/task10-assertions.md` 与 `_work/test-inventory-opstack.md`。

**假覆盖 / 编码偏离：**
- `DepositNoFeeRoutingTest` — `gasUsed == gasLimit` 🔴
- 8 文件手动 `m_isIsthmus=true` — 不覆盖生产 wiring 🟡
- `TestOpStackTransactionExecutorFixture` — 无 operator fee E2E 🟡

---

## Part 4 — 后续动作

### P0 代码修复（🔴）

1. `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx` — `input.opTxExecutor.m_isIsthmus = true`
2. `OpStackTxInputBuilder::buildRollupCostData` — 使用 signed RLP tx 字节（对齐 `MarshalBinary`）
3. `OpStackExecuteViaHost` deposit 分支 — Regolith+ 失败/成功 nonce 与 actual `gasUsed`
4. `OpStackTxExecutor::buyGas` — blob gas 扣款 + executor blob 字段传播
5. 7702 intrinsic — `25000 × authTuples` + existence refund 对齐 op-geth
6. 共享内核：2537 MSM gas、6780 `selfdestruct`（继承 ETH 审计 🔴）

### P1 补测 / 改断言（🟡）

- 修复 `DepositNoFeeRoutingTest` gas 期望（actual gas）
- `TestOpStackTransactionExecutorFixture` operator fee + `m_isIsthmus` E2E
- Fjord min-bound / parity 向量；L1 attributes 精确 fee literal
- Isthmus `warm_access` / `applyDefaultTxProps` 生产路径

### Matrix patch（强制）

```markdown
| OPStack operator fee (Isthmus) | orchestration | unsupported | explicit | `RefundIsthmusTest`, executor E2E |
| L1 attributes system deposit | orchestration | unsupported | explicit | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest` |
| Isthmus executor integration | executor-integration | unsupported | explicit | `TestOpStackTransactionExecutorFixture` |
| Rollup L1 cost tx bytes (Fjord) | executor-integration | unsupported | explicit | `OpStackFeeTest` + signed-tx E2E |
```

### ETH 交叉引用

- inherited 行已引用 `2026-06-20-eth-reference-cancun-plus-audit.md`；无 `blocked: ETH audit pending` 行
- 2537/6780 OP 🔴 继承 ETH 内核结论 — 修内核后 OP 行自动关闭

---

**审计状态：** 初版完成（Subagent-Driven Task 0–11）。合入 Part 1 全表时可从 `_work/task*.md` 粘贴。

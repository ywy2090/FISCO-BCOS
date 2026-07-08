# OPStack Isthmus 审计Remediation — 工作列表设计规格

**日期：** 2026-06-20  
**状态：** 已评审  
**类型：** 审计后续 remediation 组织（非新功能 spec）

---

## 1. 背景

`bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md` 主判定 **❌ 不通过**（≥8×🔴）。需将 Part 4 后续动作整理为可跟踪、可执行的工作列表。

**范围（brainstorming 决策 B）：**

- OPStack Isthmus 审计全部 🔴/关键 🟡
- 继承 ETH 内核项：以 **OP 路径验证** 为主（ETH CANCUN+ 复审计 @ `f989f073f` 已关闭 7×🔴，不重复实现 6780/2537 等）

**组织方式（决策 C + A）：**

- 双文件：`work-list.md`（人类）+ `fix-plan.md`（执行细则）
- 每条工作项捆绑：**代码修复 + 验收测试 + matrix 行（若适用）** 才算 Done

---

## 2. 交付物

| 文件 | 路径 | 用途 |
|------|------|------|
| 本 design spec | `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-remediation-design.md` | 工作列表结构与 Done 定义 |
| 工作跟踪表 | `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-work-list.md` | ID、轨道、优先级、状态 checkbox |
| 执行计划 | `docs/superpowers/plans/2026-06-20-opstack-isthmus-fix-plan.md` | 逐步修复、命令、文件指针 |

**关联文档（只读引用）：**

- 审计报告：`bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`
- 审计设计：`docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`
- ETH 复审计：`bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit-reaudit.md`
- 矩阵规范：`bcos-evm/capability-matrix.md`
- 继承 tracker：`bcos-evm/docs/inheritance-work-tracker.md`（并行轨道，不替代）

---

## 3. 工作列表 schema

### 3.1 列定义（work-list）

| 列 | 说明 |
|----|------|
| `ID` | `OP-01` … `OP-15` |
| `轨道` | `orchestration` / `kernel-verify` / `docs-matrix` / `quality` |
| `优先级` | P0 / P1 |
| `标题` | 一句话 |
| `审计引用` | 报告 ID（W1/R1/D1…）或 task 笔记 |
| `主要文件` | 修改预期路径 |
| `验收测试` | 必须 PASS 的测试名 |
| `捆绑` | fix / test / matrix 勾选 |
| `状态` | `[ ]` open · `[~]` partial · `[x]` done |

### 3.2 Done 定义

一项 **Done** 当且仅当：

1. 代码/fix 已合入（或验证项已执行并记录结论）
2. 「验收测试」列全部 PASS
3. 若含 matrix：同行 PR 更新 `capability-matrix.md` + Test ref
4. work-list 该行标 `[x]`；fix-plan 对应节标完成

---

## 4. 轨道与阶段

### 4.1 轨道

| 轨道 | 含义 |
|------|------|
| **orchestration** | `transaction-executor` + `bcos-evm/opstack` OP 特有逻辑 |
| **kernel-verify** | 共享 `bcos-evm/eth` 已在 ETH 复审计修复；OP 路径 smoke/补测 |
| **docs-matrix** | capability-matrix 增补与 CI gate |
| **quality** | 非阻断增强（边界向量、literal、清理） |

### 4.2 执行阶段（fix-plan）

```
Phase 1: OP-01（operator fee wiring 解锁全链）
Phase 2: OP-02, OP-06, OP-07（tx 费用模型：L1 rollup / blob / 7702 intrinsic）
Phase 3: OP-03, OP-04, OP-05（deposit 共识语义）
Phase 4: OP-08, OP-09（kernel-verify，可能仅补测/接线）
Phase 5: OP-10 + OP-11–15（matrix + quality）
```

---

## 5. 工作项清单（normative）

### P0 — orchestration

| ID | 标题 | 审计 | 验收测试 |
|----|------|------|----------|
| OP-01 | `m_isIsthmus=true` 生产接线 | W1, S3 | `TestOpStackTransactionExecutorFixture` operator fee E2E |
| OP-02 | Rollup L1 cost 用 signed RLP tx | R1 | signed-tx E2E + `OpStackFeeTest` |
| OP-03 | Deposit 成功 sender nonce +1 | D-success | `DepositMintTest` 或新用例 |
| OP-04 | Deposit EVM REVERT → actual gasUsed | D1 | `DepositNoFeeRoutingTest` 修正期望 |
| OP-05 | Deposit entry 失败 nonce+1 + gasLimit | D2 | 新 entry-failure 用例 |
| OP-06 | Blob buyGas + executor 0x03 传播 | B1 | `BlobGasBalanceTest` + executor fixture |
| OP-07 | 7702 intrinsic 25000×n + refund | A1 | `Eip7702PreCheckTest` / gas 断言 |

### P0 — kernel-verify

| ID | 标题 | 审计 | 验收测试 |
|----|------|------|----------|
| OP-08 | OP 路径验证 6780 + 2537 | task8 #10,#21 | Isthmus profile smoke；`Eip2537KernelTest`；无 🔴 |
| OP-09 | OP 路径 2929 tx-entry wiring | task8 #1-3 | `applyDefaultTxProps` 生产路径 + `OpStackTxPropsTest` |

### P1 — docs-matrix

| ID | 标题 | 验收 |
|----|------|------|
| OP-10 | matrix 增补 4 行 + Test ref | `check-capability-matrix.sh` / CI gate |

### P1 — quality

| ID | 标题 |
|----|------|
| OP-11 | Fjord min-bound / parity 向量 |
| OP-12 | L1 attributes 精确 fee literal |
| OP-13 | Receipt operatorFeeScalar/Constant（若对齐 specs） |
| OP-14 | L1Block 元数据 slot 完整仿真（非阻断） |
| OP-15 | 移除测试中冗余 `m_isIsthmus=true`（依赖 OP-01） |

---

## 6. 与 ETH 复审计的关系

| ETH 初审计 🔴 | ETH 复审计 @ f989f073f | 本 remediation |
|---------------|------------------------|----------------|
| 6780 SELFDESTRUCT | ✅ 已修复 | OP-08 验证 OP 路径 |
| 2537 MSM gas | ✅ 已修复 | OP-08 验证 OP 路径 |
| 7702 revision | ✅ 已修复 | OP-07/09 覆盖 OP orchestration |
| 7212/7823 | ✅ 已修复 | Isthmus profile ⚪，不列 P0 实现项 |

**禁止** 在本列表中重复实现已闭合的 ETH P0，除非 OP-08 验证失败。

---

## 7. 成功标准（Remediation 完成）

- [ ] work-list 全部 P0（OP-01–09）标 `[x]`
- [ ] OP 审计报告复跑后：可裁决行 **无 🔴**，主判定 ≥ ⚠️ 有条件通过
- [ ] OP-10 matrix patch 已合入
- [ ] fix-plan Phase 1–4 完成；P1 quality 可分期

---

## 8. 后续

用户评审本 spec 与 `work-list.md` / `fix-plan.md` 后，使用 `writing-plans` 细化 fix-plan（若需）或直接按 fix-plan 执行 remediation。

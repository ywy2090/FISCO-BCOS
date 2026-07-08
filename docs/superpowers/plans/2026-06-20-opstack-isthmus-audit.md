# OPStack TE Baseline（Isthmus）规范合规审计 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对 OPStack TE baseline 端到端（`transaction-executor` + `bcos-evm`，Isthmus profile）做一次性全量合规审计，产出完整审计包（Part 0–4）至 `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`。

**Architecture:** 以 capability matrix OPStack 列 + grill-me 增补清单生成审计行；深审行走 spec → FB → op-geth 流水线；inherited 行交叉引用 ETH 审计报告（未完成则 `blocked: ETH audit pending`）；按域分 Task 可并行，最后双轨汇总合并判定。不修改生产代码（审计发现可另开修复 PR），只写审计报告与 matrix patch 建议。

**Tech Stack:** C++ 源码走读、本地 op-geth v1.101702.2、clone 的 `ethereum-optimism/specs`（pin SHA）、geth v1.17.3、Besu 26.6.0、Boost.Test 断言审计、`fisco-evm-review` / `fisco-evm-test-coverage` skill

## Global Constraints

- **路径：** `OpStackTxInputBuilder` → `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` → `executeMessage` → `EthHost` + `OpHostExtension`
- **代码根：** `bcos-evm/opstack/**`、`bcos-evm/eth/**`（只读/inherited）、`transaction-executor/bcos-transaction-executor/**`
- **Profile：** Isthmus — `makeIsthmusRevisionConfig()`（PRAGUE + `eip7623` + `eip7702` + `eip4844`）；L1 cost Fjord；operator fee Isthmus
- **排除：** `bcos-evm/bcos/**`、ETH reference 深审、`bcos-executor/**`、evmone opcode 内部、Bedrock→Fjord 历史 activation 切换
- **参考客户端：** op-geth `/Users/octopus/octo/code/blockchain-impl/op-geth` v1.101702.2；geth v1.17.3；Besu tag 26.6.0
- **OP 规范：** 审计启动时 `git clone https://github.com/ethereum-optimism/specs`，pin commit SHA 写入 Part 0
- **金标准：** OP 特有 — optimism/specs MUST > op-geth > FB 测试；共享 EIP — EIP/execution-specs > geth ≈ Besu > op-geth wiring > FB 测试
- **禁止**跳过本地 op-geth 直接 WebFetch 实现代码（除非本地不可读）
- **Fjord L1 cost、operator fee** 须逐项对照，不可抽样
- **Inherited 行：** ETH 审计未完成 → 🟡 `blocked: ETH audit pending`，不计入可裁决合并判定
- **合并判定：** 双轨统计（可裁决 vs 待决），见设计 spec §5.1
- **发现 🔴 仍完成全部能力审计**
- **设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）

---

## File Map

### 生产代码（审计主战场）

| 文件 | 审计职责 |
|------|----------|
| `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h` | 端到端：profile、rollup、receipt、`opStackExecuteViaHost` 调用 |
| `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h` | Web3 字段、gas cap、blob、deposit、7702 字段传播 |
| `bcos-evm/eth/RevisionConfig.h` | `makeIsthmusRevisionConfig()` |
| `bcos-evm/opstack/OpStackExecuteViaHost.cpp` | orchestration 主入口、deposit 分支、fee 挂钩 |
| `bcos-evm/opstack/OpStackExecuteViaHost.h` | `OpStackExecuteViaHostInput` 契约 |
| `bcos-evm/opstack/OpStackTxExecutor.cpp` | `buyGas`、`refundGas`、`refundIsthmusOperatorCost`、`m_isIsthmus` |
| `bcos-evm/opstack/OpStackTxExecutor.h` | operator fee recipient 常量 |
| `bcos-evm/opstack/OpStackPreCheck.cpp` | blob、7702、deposit precheck |
| `bcos-evm/opstack/OpStackDepositTx.h` | deposit 结构 |
| `bcos-evm/opstack/fee/RollupCost.cpp` | `flzCompressLen`、`newRollupCostData` |
| `bcos-evm/opstack/fee/RollupCost.h` | `RollupCostData` |
| `bcos-evm/opstack/fee/OpStackFee.cpp` | `l1CostFjord`、`operatorCostIsthmus`、`loadOpStackFeeParams` |
| `bcos-evm/opstack/fee/OpStackFloorGas.cpp` | OP 7623 floor gas deviation |
| `bcos-evm/opstack/fee/OpStackGasSettlement.h` | `postExecuteGasSettlement` |
| `bcos-evm/opstack/OpStackReceiptMeta.h` | l1Fee / operatorFee / depositNonce |
| `bcos-evm/opstack/l1/L1BlockPredeploy.cpp` | L1Block predeploy 执行 |
| `bcos-evm/opstack/l1/L1BlockStorage.cpp` | `parseIsthmusL1Attributes`、slot 读写 |
| `bcos-evm/opstack/l1/L1BlockStorage.h` | `IsthmusL1Attributes` 布局 |
| `bcos-evm/opstack/OpHostExtension.h` | L1Block 链预编译路由 |
| `bcos-evm/opstack/OpStackConstants.h` | predeploy 地址、Fjord 常量、slot 编号 |
| `bcos-evm/capability-matrix.md` | OPStack 列清单 |
| `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md` | **交付物** |
| `bcos-evm/docs/audits/_work/` | 各 Task 中间笔记 |

### op-geth 对照入口（symbol 搜索，不用行号）

| 域 | op-geth 文件 / symbol |
|----|----------------------|
| Deposit tx | `core/types/deposit_tx.go` |
| Deposit 失败 | `core/state_transition.go` — `IsDepositTx`、revert + nonce bump |
| L1 cost Fjord | `core/types/rollup_cost.go` — `L1CostFjord`、`flzCompressLen` |
| Operator fee | `core/types/rollup_cost.go` — `OperatorCost`、Isthmus 分支 |
| L1Block | `params/protocol_params.go`、L1 attributes 相关 |
| 7623 OP settlement | `core/state_transition.go` — Prague floor + OP gas 路径 |
| 7702 OP precheck | `core/state_transition.go`、deposit/SET_CODE 路径 |
| Blob tx | `core/types/transaction.go` blob 字段 |

### 测试（断言审计范围）

| 文件 | 关联域 |
|------|--------|
| `bcos-evm/test/opstack/RollupCostTest.cpp` | Fjord compress、rollup data |
| `bcos-evm/test/opstack/OpStackFeeTest.cpp` | L1 cost 公式 |
| `bcos-evm/test/opstack/RefundIsthmusTest.cpp` | operator fee refund |
| `bcos-evm/test/opstack/DepositTxPreCheckTest.cpp` | deposit precheck |
| `bcos-evm/test/opstack/DepositMintTest.cpp` | mint |
| `bcos-evm/test/opstack/DepositNoFeeRoutingTest.cpp` | deposit 无 fee |
| `bcos-evm/test/opstack/L1AttributesDepositTest.cpp` | L1 attributes 成功 + fee 联动 |
| `bcos-evm/test/opstack/L1AttributesDepositFailureTest.cpp` | L1 attributes 失败回滚 |
| `bcos-evm/test/opstack/L1BlockPredeployTest.cpp` | predeploy 路由 |
| `bcos-evm/test/opstack/L1BlockGetterTest.cpp` | getter |
| `bcos-evm/test/opstack/OpStackFloorGasTest.cpp` | floor gas |
| `bcos-evm/test/opstack/OpStackSettlementTest.cpp` | settlement + receipt meta |
| `bcos-evm/test/opstack/BlobGasBalanceTest.cpp` | 4844 blob orchestration |
| `bcos-evm/test/opstack/Eip7702PreCheckTest.cpp` | 7702 precheck |
| `bcos-evm/test/opstack/OpStack7702ExecuteViaHostPropagationTest.cpp` | 7702 OP 路径 |
| `bcos-evm/test/opstack/OpStackTxInputBuilderTest.cpp` | tx input |
| `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp` | 端到端 smoke |
| `bcos-evm/test/opstack/OpStackTxPropsTest.cpp` | 2929 tx props |
| `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin` | L1 attributes calldata |
| `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` | executor 集成 |

其余 `bcos-evm/test/opstack/*.cpp`（`CanTransferTest`、`CalcRefundTest`、`EvmoneRefundSpikeTest` 等）纳入 Task 10 目录扫描，按用例名映射到域。

---

## 并行策略

```
Task 0 (bootstrap + clone specs) ──┬── Task 1 (profile + executor wiring)
                                   ├── Task 2 (rollup + L1 fee Fjord)
                                   ├── Task 3 (operator fee 增补)
                                   ├── Task 4 (deposit + 失败路径)
                                   ├── Task 5 (L1Block + L1 attributes)
                                   ├── Task 6 (7623 deviation + receipt)
                                   ├── Task 7 (7702 precheck + 4844 blob)
                                   ├── Task 8 (inherited smoke + ETH 交叉引用)
                                   ├── Task 9 (unsupported / feature-gated)
                                   └── Task 10 (断言审计，可与 1–9 并行）
                                              │
                                              ▼
                                         Task 11 (双轨汇总)
```

---

### Task 0: 环境校验、clone specs 与报告骨架

**Files:**
- Create: `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`
- Create: `bcos-evm/docs/audits/_work/inventory.md`
- Clone: `blockchain-impl/ethereum-optimism-specs`（若不存在）
- Read: `bcos-evm/capability-matrix.md`, `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`

**Interfaces — Produces:**
- 报告 Part 0–4 空标题与表头
- `inventory.md`：matrix OPStack 列 + 增补清单（约 35 行）
- Part 0 中记录的 optimism/specs commit SHA

- [ ] **Step 1: 校验参考仓库版本**

```bash
cd /Users/octopus/octo/code/blockchain-impl/op-geth && rtk git describe --tags | head -1
cd /Users/octopus/octo/code/blockchain-impl/go-ethereum && rtk git describe --tags | head -1
cd /Users/octopus/octo/code/blockchain-impl/besu && rtk git describe --tags | head -1
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor && rtk git rev-parse --abbrev-ref HEAD && rtk git rev-parse --short HEAD
```

Expected: op-geth 含 `v1.101702.2`；geth `v1.17.3`；Besu `26.6.0`

- [ ] **Step 2: Clone 并 pin optimism/specs**

```bash
if [ ! -d /Users/octopus/octo/code/blockchain-impl/ethereum-optimism-specs ]; then
  git clone https://github.com/ethereum-optimism/specs /Users/octopus/octo/code/blockchain-impl/ethereum-optimism-specs
fi
cd /Users/octopus/octo/code/blockchain-impl/ethereum-optimism-specs && rtk git rev-parse HEAD && rtk git log -1 --oneline
```

记录 SHA；在 specs 内定位 Isthmus 相关文档：

```bash
rtk grep -rln -i "isthmus\|operator.fee\|fjord\|deposit" /Users/octopus/octo/code/blockchain-impl/ethereum-optimism-specs --glob '*.md' | head -20
```

将路径列表写入 `_work/specs-isthmus-index.md`。

- [ ] **Step 3: 生成审计清单 `_work/inventory.md`**

从 matrix OPStack 列提取每一行 + 增补三行：

```markdown
| # | Capability | Layer | OPStack status | Depth | Source |
|---|------------|-------|----------------|-------|--------|
| S1 | OPStack operator fee (Isthmus) | orchestration | explicit(增补) | 深审 | supplement |
| S2 | L1 attributes system deposit | orchestration | explicit(增补) | 深审 | supplement |
| S3 | Isthmus executor integration wiring | executor-integration | explicit(增补) | 深审 | supplement |
| 1 | EIP-2929 runtime warm | kernel | inherited | smoke | matrix |
| ... | OPStack deposit tx | orchestration | explicit | 深审 | matrix |
```

跳过 OPStack 列 `unsupported` 且非审计边界项（BCOS auth 等）→ 标 ⚪ 但仍列入 inventory。

- [ ] **Step 4: 检查 ETH 审计报告是否可用于交叉引用**

```bash
test -f bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md && rtk grep -c "Part 1" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md || echo "ETH_AUDIT_MISSING"
```

若 `ETH_AUDIT_MISSING`：在 `_work/inventory.md` 顶部注明「inherited 行默认 blocked: ETH audit pending」。

- [ ] **Step 5: 创建报告骨架**

```bash
mkdir -p bcos-evm/docs/audits/_work
```

写入 `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`：

```markdown
# OPStack TE Baseline（Isthmus）规范合规审计报告

**日期：** 2026-06-20
**分支/commit：** <填>
**op-geth：** v1.101702.2 @ <sha>
**geth / Besu：** v1.17.3 / 26.6.0
**optimism/specs：** <pin SHA>
**范围：** OpStack TE baseline 端到端，Isthmus profile

## Part 0 — 执行摘要
### 可裁决行统计
### 待决行统计
### 主合并判定

## Part 1 — 合规矩阵
| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |

## Part 2 — 偏离项详情

## Part 3 — 测试断言审计
| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |

## Part 4 — 后续动作
```

- [ ] **Step 6: 编译运行 OP 相关测试基线**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target OpStackExecuteViaHostSmokeTest RollupCostTest OpStackFeeTest RefundIsthmusTest L1AttributesDepositTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
ctest -R "OpStack|RollupCost|L1Attributes|RefundIsthmus" --output-on-failure
```

Expected: PASS；若 FAIL 记入 Part 2 作为起点 🔴，继续审计。

- [ ] **Step 7: Commit 审计脚手架**

```bash
rtk git add bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md bcos-evm/docs/audits/_work/
rtk git commit -m "docs(audit): scaffold OPStack Isthmus compliance audit report"
```

---

### Task 1: Isthmus Profile + Executor 集成 wiring（增补 S3）

**Files:**
- Read: `bcos-evm/eth/RevisionConfig.h`, `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`
- Read: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`（`m_isIsthmus` 传递）
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`, `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`
- Work: `bcos-evm/docs/audits/_work/task1-executor-wiring.md`

**Interfaces — Consumes:** Task 0 `inventory.md`  
**Interfaces — Produces:** Part 1 行 `makeIsthmusRevisionConfig`、增补 S3 executor wiring

- [ ] **Step 1: 对照 `makeIsthmusRevisionConfig` 与 matrix**

读 `RevisionConfig.h` 第 62–72 行，记录字段：

| 字段 | Isthmus 值 | matrix OPStack 列期望 |
|------|------------|----------------------|
| `revision` | `EVMC_PRAGUE` | inherited |
| `eip7623` | true | explicit orchestration |
| `eip7702` | true | inherited |
| `eip4844` | true | inherited profile |
| `prague_post_execution` | false | unsupported |

运行：

```bash
cd build && ./bcos-evm/test/RevisionConfigProfileTest --log_level=test_suite 2>/dev/null | tail -5
```

- [ ] **Step 2: 审计 `m_isIsthmus` 生产路径（关键 🔴 风险点）**

```bash
rtk grep -n "m_isIsthmus" bcos-evm/opstack/ transaction-executor/bcos-transaction-executor/
```

检查清单：
- `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx` 是否设置 `input.opTxExecutor.m_isIsthmus = true`
- `opStackExecuteViaHost` 是否依赖调用方已设置
- 测试是否手动 `m_isIsthmus = true` 而生产未设

对照 op-geth：Isthmus 激活时 operator fee 必须计入 `buyGas` balance check。

若生产未设 → Part 2 **🔴**「operator fee 未接线」。

- [ ] **Step 3: Executor 输入构建 checklist（§3.6）**

读 `OpStackTransactionExecutorImpl.h` `opStackExecuteViaHostTx()`：
- `fillGasCaps` / `fillWeb3Fields`
- `buildRollupCostData`
- `resolveOpStackBaseFee` / `resolveOpStackBlobBaseFee`
- receipt `setL1Fee` / `setOperatorFee` / `setDepositNonce`

对照 `OpStackTxInputBuilder.h` 字段映射与 op-geth tx 解码。

- [ ] **Step 4: 运行 executor fixture**

```bash
cd build && ctest -R OpStackTransactionExecutorFixture --output-on-failure
```

- [ ] **Step 5: 写入 Part 1（profile + S3）+ Part 2；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task1 Isthmus profile and executor wiring"
```

---

### Task 2: Rollup Cost + L1 Data Fee（Fjord）

**Files:**
- Read: `bcos-evm/opstack/fee/RollupCost.cpp`, `bcos-evm/opstack/fee/OpStackFee.cpp`, `bcos-evm/opstack/OpStackConstants.h`
- op-geth: `core/types/rollup_cost.go`
- specs: Fjord / rollup cost 章节（`_work/specs-isthmus-index.md`）
- Test: `RollupCostTest.cpp`, `OpStackFeeTest.cpp`
- Work: `_work/task2-fjord-l1-cost.md`

- [ ] **Step 1: 读 specs + op-geth Fjord MUST**

记录公式：`L1_COST_INTERCEPT`、`L1_COST_FASTLZ_COEF`、`FJORD_DIVISOR`、`MIN_TX_SIZE_SCALED` 与 op-geth 常量名。

- [ ] **Step 2: 逐项对照 FB 常量**

```bash
rtk grep -n "L1_COST\|FJORD\|MIN_TX" bcos-evm/opstack/OpStackConstants.h
rtk grep -n "L1Cost\|Fjord\|flzCompress" /Users/octopus/octo/code/blockchain-impl/op-geth/core/types/rollup_cost.go | head -30
```

任一常量不等 → 🔴。

- [ ] **Step 3: `flzCompressLen` / `newRollupCostData` 算法对照**

读 `RollupCost.cpp` 全文；对照 op-geth `flzCompressLen`、`NewRollupCostData` 实现逻辑（非抽样）。

运行 `RollupCostTest`；读断言中的 literal 输入/输出。

- [ ] **Step 4: `l1CostFjord` 端到端**

读 `OpStackFee.cpp` `l1CostFjord`；对照 op-geth `L1CostFjord` 函数。

读 `OpStackFeeTest.cpp` 每个 `BOOST_CHECK` 期望值，手算或对照 op-geth 同输入。

- [ ] **Step 5: 写入 Part 1（rollup + 4844 前置 L1 fee）+ Part 2/3；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task2 Fjord L1 data fee findings"
```

---

### Task 3: Operator Fee（Isthmus）（增补 S1）

**Files:**
- Read: `bcos-evm/opstack/fee/OpStackFee.cpp`, `OpStackTxExecutor.cpp`
- op-geth: `rollup_cost.go` operator cost / Isthmus
- specs: operator fee 章节
- Test: `RefundIsthmusTest.cpp`, `OpStackSettlementTest.cpp`
- Work: `_work/task3-operator-fee.md`

- [ ] **Step 1: specs + op-geth operator fee MUST**

记录：fee 计算公式、`OPERATOR_FEE_PARAMS_SLOT`、扣款时机（buyGas）、refund 规则。

- [ ] **Step 2: FB `operatorCostIsthmus` 对照**

```bash
rtk grep -n "operatorCost\|OperatorFee\|m_operatorCost" bcos-evm/opstack/
rtk grep -n "OperatorCost\|operatorFee" /Users/octopus/octo/code/blockchain-impl/op-geth/core/types/rollup_cost.go
```

- [ ] **Step 3: `buyGas` / `refundIsthmusOperatorCost` 路径**

读 `OpStackTxExecutor.cpp` 第 24–140 行；确认：
- `m_isIsthmus && m_operatorCostFunc` 门控
- balance check 含 `m_operatorCostLimit`
- refund 与 op-geth Regolith/Isthmus 一致

- [ ] **Step 4: 断言审计 `RefundIsthmusTest`**

读用例体；对照 op-geth 同 gas/refund 场景的期望值。

- [ ] **Step 5: 写入 Part 1 增补 S1 + Part 4 matrix patch 草案行；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task3 Isthmus operator fee findings"
```

---

### Task 4: Deposit Tx（含失败路径）

**Files:**
- Read: `bcos-evm/opstack/OpStackDepositTx.h`, `OpStackExecuteViaHost.cpp`（deposit 分支）, `OpStackPreCheck.cpp`
- op-geth: `core/types/deposit_tx.go`, `state_transition.go`（失败处理）
- specs: deposit tx 章节
- Test: `DepositTxPreCheckTest.cpp`, `DepositMintTest.cpp`, `DepositNoFeeRoutingTest.cpp`
- Work: `_work/task4-deposit.md`

- [ ] **Step 1: specs + op-geth deposit MUST**

记录：mint、gasPrice=0、无 L1/operator fee、Regolith 后 gas 记账、`skipNonceChecks` 语义。

- [ ] **Step 2: FB deposit 成功路径**

读 `opStackExecuteViaHost` deposit 分支（约 122–181 行）：mint → checkpoint → execute → settlement。

对照 `DepositMintTest`、`DepositNoFeeRoutingTest` 断言。

- [ ] **Step 3: deposit 失败路径（grill-me 决策 A）**

读失败分支：`state.revert()` 后 `set_nonce(nonce+1)`、`gasUsed = gasLimit`。

对照 op-geth `state_transition.go` 约 485–510 行注释与逻辑。

若无专项失败测试 → Part 3 🟡「deposit 失败 nonce/gas 缺测」。

- [ ] **Step 4: precheck**

读 `OpStackPreCheck` deposit 分支；对照 op-geth deposit 验证。

- [ ] **Step 5: 写入 Part 1 `OPStack deposit tx` + Part 2/3；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task4 deposit tx including failure path"
```

---

### Task 5: L1Block Predeploy + L1 Attributes Deposit（增补 S2）

**Files:**
- Read: `L1BlockPredeploy.cpp`, `L1BlockStorage.cpp`, `OpHostExtension.h`, `OpStackConstants.h`
- op-geth: `params/protocol_params.go`, L1Block 相关
- Test: `L1BlockPredeployTest.cpp`, `L1BlockGetterTest.cpp`, `L1AttributesDepositTest.cpp`, `L1AttributesDepositFailureTest.cpp`
- Fixture: `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin`
- Work: `_work/task5-l1block-attributes.md`

- [ ] **Step 1: predeploy 地址与 slot 对照**

```bash
rtk grep -n "0x4200\|L1_BLOCK\|BASE_FEE_SLOT\|OPERATOR_FEE" bcos-evm/opstack/OpStackConstants.h
rtk grep -n "0x4200" /Users/octopus/octo/code/blockchain-impl/op-geth/params/protocol_params.go | head -15
```

- [ ] **Step 2: `parseIsthmusL1Attributes` 176 字节布局**

读 `L1BlockStorage.cpp` `parseIsthmusL1Attributes`；对照 specs Isthmus L1 attributes 字段顺序与长度。

用 `isthmus_l1_attributes.bin` 十六进制对照各字段 offset。

- [ ] **Step 3: L1 attributes 成功路径 + fee 联动**

读 `L1AttributesDepositTest.cpp`：attributes 更新后下一笔 tx 的 L1 cost 变化断言。

对照 op-geth / specs：base fee scalar、blob base fee 更新语义。

- [ ] **Step 4: 失败路径 state 不回滚 slot**

读 `L1AttributesDepositFailureTest.cpp`；确认失败 deposit 不 commit L1Block slot 变更。

- [ ] **Step 5: 写入 Part 1（chain precompile deviation + 增补 S2）+ Part 2/3；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task5 L1Block and L1 attributes deposit"
```

---

### Task 6: EIP-7623 Floor Gas Deviation + Settlement + Receipt Meta

**Files:**
- Read: `OpStackFloorGas.cpp`, `OpStackGasSettlement.h`, `OpStackExecuteViaHost.cpp`（settlement 调用）
- Read: `OpStackReceiptMeta.h`, `OpStackTransactionExecutorImpl.h`（receipt 回填）
- op-geth: OP 路径 `FloorDataGas`、`gasUsed`、receipt 扩展字段
- Test: `OpStackFloorGasTest.cpp`, `OpStackSettlementTest.cpp`
- Work: `_work/task6-floor-gas-receipt.md`

- [ ] **Step 1: OP floor gas deviation vs Ethereum reference**

读 `OpStackFloorGas.cpp`、`postExecuteGasSettlement`；对照 op-geth OP 栈 settlement（非纯 `finalizeEthereumGasUsed`）。

matrix 标 `deviation` → 确认有正向测试 `OpStackFloorGasTest`。

- [ ] **Step 2: entry floor check**

读 `OpStackExecuteViaHost.cpp` `executeEntryFloorDataGasCheck`；对照 geth `FloorDataGas` 与 OP 特有门控。

- [ ] **Step 3: receipt metadata**

读 `OpStackReceiptMeta` 字段；对照 `makeReceipt()` 中 `setL1Fee`、`setOperatorFee`、`setDepositNonce`。

对照 op-geth receipt / specs 扩展字段名与编码。

- [ ] **Step 4: 断言审计 settlement 测试**

读 `OpStackSettlementTest.cpp` 每个 `BOOST_CHECK` 的 gasUsed、l1Fee、operatorFee 期望值。

- [ ] **Step 5: 写入 Part 1 三行（7623 entry、7623 settlement deviation、receipt meta）；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task6 floor gas deviation and receipt meta"
```

---

### Task 7: EIP-7702 Precheck + EIP-4844 Blob Orchestration

**Files:**
- Read: `OpStackPreCheck.cpp`, `OpStackExecuteViaHost.cpp`（auth intrinsic `TX_AUTH_TUPLE_GAS`）
- Test: `Eip7702PreCheckTest.cpp`, `BlobGasBalanceTest.cpp`, `OpStack7702ExecuteViaHostPropagationTest.cpp`
- op-geth: SET_CODE tx 验证、blob gas 字段
- Work: `_work/task7-7702-4844.md`

- [ ] **Step 1: 7702 precheck + intrinsic gas**

读 `OpStackPreCheck` 7702 分支；读 `computeIntrinsicGasDebit` 中 `TX_AUTH_TUPLE_GAS = 12500`。

对照 op-geth Prague SET_CODE intrinsic gas。

- [ ] **Step 2: 7702 OP 路径传播 smoke**

读 `OpStack7702ExecuteViaHostPropagationTest.cpp`；确认走 `opStackExecuteViaHost` 而非纯 kernel 单测。

若 ETH 审计已完成：交叉引用 kernel 行；若未完成：标 blocked。

- [ ] **Step 3: 4844 blob orchestration**

读 `OpStackPreCheck` blob 字段校验；读 `BlobGasBalanceTest.cpp`。

对照 op-geth blob gas fee cap、balance 检查（Isthmus 下 blob base fee 来自 L1Block）。

- [ ] **Step 4: 写入 Part 1 两行（7702 precheck explicit、4844 blob explicit）；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task7 EIP-7702 precheck and blob orchestration"
```

---

### Task 8: Inherited 行 Smoke + ETH 交叉引用

**Files:**
- Read: ETH 审计报告 Part 1（若存在）
- Read: `bcos-evm/test/opstack/OpStackTxPropsTest.cpp`, shared kernel 测试
- Work: `_work/task8-inherited-smoke.md`

**Inherited 行清单（smoke）：** 2929×3、7702 kernel/input/revision、2537、6780 kernel、builtin precompiles、4844 profile、1153/5656/6780 profile、chain precompile（除 L1Block 已在 Task 5）

- [ ] **Step 1: 加载 ETH 审计交叉引用表**

若 `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md` 存在：

```bash
rtk grep "| EIP-2929\|7702\|2537\|6780\|builtin precompile" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md | head -20
```

为每行 inherited 记录 ETH 状态（✅/🟡/🔴）。若报告不存在：所有 inherited 标 `blocked: ETH audit pending`。

- [ ] **Step 2: OP 路径 wiring 逐行验证**

| 能力 | OP wiring 检查 |
|------|----------------|
| 2929 tx-entry | `OpStackTxPropsTest`、TE `applyDefaultTxProps` |
| 7702 tx propagation | `OpStackTxInputBuilderTest` |
| 7702 kernel | `Eip7702ApplyAuthorizationTest`（opstack） |
| 2537 | 共享内核 + Isthmus profile `EVMC_PRAGUE` |
| 6780 | 共享 `EthHost::selfdestruct` |

```bash
rtk grep -n "applyDefaultTxProps\|OpStackTxProps" transaction-executor/ bcos-evm/
```

- [ ] **Step 3: 运行 OP 路径相关共享测试**

```bash
cd build && ctest -R "OpStack7702|OpStackTxInput|OpStackTxProps|Eip7702Apply" --output-on-failure
```

- [ ] **Step 4: 写入 Part 1 inherited 分组；ETH 🔴 → 继承 🔴；ETH ✅ + OP 缺测 → 🟡**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task8 inherited rows smoke and ETH cross-ref"
```

---

### Task 9: Unsupported / Feature-Gated / ⚪ 行

**Files:**
- Read: `makeIsthmusRevisionConfig`, matrix OPStack `unsupported`/`feature-gated` 行
- Work: `_work/task9-unsupported.md`

- [ ] **Step 1: 确认 ⚪ 行未意外激活**

| 行 | 期望 |
|----|------|
| EIP-7212 | `eip7212` 未设；0x0100 不可达 |
| EIP-7823 | `eip7823` 未设 on Isthmus helper |
| BCOS 21000 gas | OP 路径无 `BALANCE_TRANSFER_GAS` |
| `prague_post_execution` | false |
| profile-only 字段 | ADR-004 无 TE consumer |

```bash
rtk grep -n "eip7212\|eip7823\|BALANCE_TRANSFER\|prague_post" bcos-evm/opstack/ bcos-evm/eth/RevisionConfig.h
```

- [ ] **Step 2: 写入 Part 1 ⚪/📋 行；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task9 unsupported and profile-only rows"
```

---

### Task 10: 测试断言审计（横向）

**Files:**
- Read: `bcos-evm/test/opstack/*.cpp`, `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`
- Read: `bcos-evm/test/fixtures/FixtureAssert.h`
- Skill: `fisco-evm-test-coverage` 断言审计流程
- Work: `_work/test-inventory-opstack.md`

- [ ] **Step 1: 枚举全部 opstack 测试用例**

```bash
rtk grep -rn "BOOST_AUTO_TEST_CASE" bcos-evm/test/opstack/ transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp > bcos-evm/docs/audits/_work/test-inventory-opstack.md
```

- [ ] **Step 2: 逐文件断言审计（必读 `BOOST_CHECK` 体）**

每个用例填 Part 3 表：

```markdown
| OpStackFeeTest.cpp | fjord_cost_matches_formula | ✅ | op-geth L1CostFjord literal | 手算一致 |
| OpStackExecuteViaHostSmokeTest.cpp | smoke_success | 🟡 | 仅 status=success | 未断言 fee |
```

重点：`m_isIsthmus` 仅在测试中手动设置 → Part 3 标「生产 wiring 未覆盖」。

- [ ] **Step 3: fixture `isthmus_l1_attributes.bin` spot-check**

确认测试使用的 calldata 长度 176 与 `ISTHMUS_L1_ATTRIBUTES_LEN` 一致。

- [ ] **Step 4: 写入完整 Part 3；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task10 opstack test assertion audit"
```

---

### Task 11: 双轨汇总 — Part 0 / Part 4 / 主合并判定

**Files:**
- Modify: `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`
- Read: 全部 `_work/task*.md`

- [ ] **Step 1: 统计可裁决行 vs 待决行**

```bash
rtk grep -c "| ✅ |" bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md
rtk grep -c "| 🟡 |" bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md
rtk grep -c "| 🔴 |" bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md
rtk grep -c "blocked: ETH audit pending" bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md
```

可裁决 🟡 不含 `blocked:` 前缀行。

- [ ] **Step 2: 写 Part 0 双轨摘要**

```markdown
## Part 0 — 执行摘要

### 可裁决行
| 指标 | 值 |
|------|-----|
| 行数 | N |
| ✅ | n1 |
| 🟡 | n2 |
| 🔴 | n3 |
| 📋 | n4 |
| **主判定** | ❌ / ⚠️ / ✅ |

### 待决行
| 类型 | 行数 |
|------|------|
| blocked: ETH audit pending | n5 |
| matrix-patch-pending（增补清单） | 3 |

### Top 阻断项
1. ...
```

主判定规则：可裁决行中任一 🔴 → ❌；无 🔴 有可裁决 🟡 → ⚠️；否则 ✅。

- [ ] **Step 3: 写 Part 4**

三列表：
- P0 代码修复（🔴，附文件路径 — 含 `m_isIsthmus` wiring 若确认）
- P1 补测/改断言（🟡）
- **Matrix patch（强制）** — 增补三行草案：

```markdown
| OPStack operator fee (Isthmus) | orchestration | unsupported | explicit | `RefundIsthmusTest`, executor wiring test |
| L1 attributes system deposit | orchestration | unsupported | explicit | `L1AttributesDepositTest` |
| Isthmus executor integration | executor-integration | unsupported | explicit | `TestOpStackTransactionExecutorFixture` |
```

- ETH blocked 行清单（待 ETH 报告后关闭）

- [ ] **Step 4: Done 标准自检（设计 spec §8）**

- [ ] Part 0–4 齐全
- [ ] matrix + 增补均有 Part 1 条目
- [ ] optimism/specs SHA 在报告头
- [ ] Part 3 覆盖 `test/opstack/**` 目录扫描 + executor fixture
- [ ] 双轨统计 + 主判定明确

- [ ] **Step 5: Final commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): complete OPStack Isthmus compliance audit report"
```

---

## Spec 覆盖自检

| Spec 章节 | 对应 Task |
|-----------|-----------|
| §3.1 端到端路径 + transaction-executor | Task 0, 1 |
| §3.3 增补清单 S1–S3 | Task 1, 3, 5 |
| §3.6 Executor checklist | Task 1 |
| §4.6 deposit 失败、Fjord、operator fee | Task 2, 3, 4 |
| §4.7 ETH parallel + blocked | Task 0, 8 |
| §5.1 双轨合并判定 | Task 11 |
| §6 Part 0–4 交付物 | 全部 Task |
| §8 Done 标准 | Task 11 Step 4 |

---

## 预估工作量

| Task | 预估 | 可并行 |
|------|------|--------|
| 0 bootstrap | 1–2 h | 串行首任务 |
| 1 executor wiring | 2 h | 与 2–7 |
| 2 Fjord L1 fee | 2–3 h | 是 |
| 3 operator fee | 1–2 h | 是 |
| 4 deposit | 2 h | 是 |
| 5 L1Block + attributes | 2–3 h | 是 |
| 6 floor gas + receipt | 2 h | 是 |
| 7 7702 + blob | 1–2 h | 是 |
| 8 inherited smoke | 2 h | 是 |
| 9 unsupported | 1 h | 是 |
| 10 断言审计 | 3–4 h | 与 1–9 |
| 11 汇总 | 1–2 h | 串行末任务 |

**合计：** 约 16–22 h（单人）；Task 2–10 可多 agent 并行后 Task 11 汇总。

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-20-opstack-isthmus-audit.md`.**

**两种执行方式：**

1. **Subagent-Driven（推荐）** — 每个 Task 派生子 agent，Task 间 review，迭代快（`subagent-driven-development`）
2. **Inline Execution** — 本 session 用 `executing-plans` 按 Task 批量执行，检查点汇报

你更倾向哪一种？

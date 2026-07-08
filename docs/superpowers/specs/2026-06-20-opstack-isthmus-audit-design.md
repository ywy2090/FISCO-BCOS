# OPStack TE Baseline（Isthmus）规范合规审计 — 设计规格

**日期：** 2026-06-20  
**状态：** 待评审（经 grill-me 2026-06-20）  
**类型：** 一次性全量审计（非 PR 增量审查）  
**执行策略：** 方案 1 — Matrix 驱动 + 分级深度

---

## 1. 背景与动机

`bcos-evm` 在 `feat-evm-refactor` 分支上已完成三层执行路径分层：ETH reference（`executeViaEth`）、BCOS TE baseline（`executeViaHost`）、OPStack TE baseline（`opStackExecuteViaHost`）。`capability-matrix.md` 已定义 **OPStack (TE baseline)** 列，覆盖 deposit tx、L1/operator fee、rollup cost、L1Block predeploy、floor gas deviation、receipt meta 等 OP 特有能力。

ETH reference 路径已有独立审计 spec（`2026-06-20-eth-reference-cancun-plus-audit-design.md`），但 **OPStack TE baseline 在 Isthmus profile 下** 尚未做系统性的 Optimism 规范合规盘点。本 spec 定义 AI 辅助审计的任务边界、分级深度、金标准优先级与交付物格式，与 ETH 审计及 `fisco-evm-review` / `fisco-evm-test-coverage` skill 对齐。

---

## 2. 审计目标

判断 FISCO-BCOS 在 **OPStack TE baseline 执行路径**（**Isthmus profile**）端到端（`transaction-executor` + `bcos-evm`）上：

1. **OP 特有 orchestration** 是否与 Optimism 官方规范（`ethereum-optimism/specs` MUST 条款）一致，并经 op-geth 交叉验证；
2. **共享 Ethereum EIP**（matrix OPStack 列 `inherited` 行）在 OP 路径上是否 baseline-reachable 且与 EIP/execution-specs 一致；
3. `bcos-evm/test/opstack/**` 及 executor 层相关测试的断言是否与金标准一致（非「有文件即充分」）。

**非目标：**

- 不审计 BCOS TE baseline（`executeViaHost`）或 ETH reference 路径深审（已有独立 spec）
- 不审计 legacy `bcos-executor` / DAG / `HostContext`
- 不深度审计 evmone 内部 opcode 实现（仅审 revision 传递与 Host 回调）
- 不按 Bedrock→Ecotone→Fjord 全阶梯审计 activation 切换（Isthmus 单点 profile；历史公式仅在 Isthmus 生效点核对）

---

## 3. 范围

### 3.1 执行路径

```
OpStackTxInputBuilder                    ← transaction-executor
        ↓
OpStackTransactionExecutorImpl           ← transaction-executor（profile、rollup cost、receipt）
        ↓
opStackExecuteViaHost()                  ← bcos-evm/opstack（orchestration 主战场）
        ↓
executeMessage()                         ← 共享内核（inherited 行）
        ↓
EthHost + OpHostExtension + evmone
```

**代码根目录（审计范围）：**

| 区域 | 路径 | 职责 |
|------|------|------|
| Executor 集成 | `transaction-executor/bcos-transaction-executor/**` | TxInputBuilder、ExecutorImpl、`makeIsthmusRevisionConfig` 注入、`m_isIsthmus`、rollup/receipt |
| OP orchestration | `bcos-evm/opstack/**` | fee、deposit、floor gas、L1Block、settlement |
| 共享内核（只读） | `bcos-evm/eth/**` | inherited 行内核与 `makeIsthmusRevisionConfig` |
| 入口索引 | `bcos-evm/include/bcos-evm/op_executor.hpp` | TE baseline 头文件入口 |

### 3.2 Isthmus profile 语义

当前实现以 Isthmus 为目标 profile，关键特征：

| 维度 | Isthmus 实现锚点 |
|------|------------------|
| EVM revision | PRAGUE（非 OSAKA） |
| Profile 构建 | `makeIsthmusRevisionConfig()`（`bcos-evm/eth/RevisionConfig.h`） |
| L1 data fee | Fjord 公式（`l1CostFjord`） |
| Operator fee | Isthmus（`operatorCostIsthmus`、`OpStackTxExecutor::m_isIsthmus`） |
| L1 attributes | `ISTHMUS_L1_ATTRIBUTES_LEN = 176` |
| 常量 / 地址 | `OpStackConstants.h`（对齐 op-geth `rollup_cost.go`、`protocol_params.go`） |

审计以 **Isthmus 激活后的行为** 为基准，不追溯更早 OP 升级在 FB 中的独立 profile 构建器（若不存在则标 ⚪）。

**集成风险示例（须在 executor 层验证）：** `OpStackTransactionExecutorImpl` 调用 `makeIsthmusRevisionConfig()` 但未设置 `opTxExecutor.m_isIsthmus = true` 时，operator fee 在 `buyGas` 路径不会生效——属端到端 wiring 审计范围。

### 3.3 能力清单（capability-matrix OPStack 列 + 增补清单）

**主清单：** `bcos-evm/capability-matrix.md` **OPStack (TE baseline)** 列。

**增补清单（matrix 尚未覆盖，Part 1 单独分组「增补能力（待 matrix 合入）」；Part 4 必须输出 matrix patch）：**

| 增补能力 | 审计深度 | 关键实现 / 测试 |
|----------|----------|-----------------|
| OPStack operator fee (Isthmus) | 深审 | `operatorCostIsthmus`, `RefundIsthmusTest`, `m_isIsthmus` |
| L1 attributes system deposit | 深审（独立域） | `parseIsthmusL1Attributes`, `L1AttributesDepositTest`, `isthmus_l1_attributes.bin` |
| Isthmus executor integration wiring | 深审 | `OpStackTransactionExecutorImpl`, `OpStackTxInputBuilder`, `TestOpStackTransactionExecutorFixture` |

按 matrix 状态分配审计深度：

| Matrix 状态 | 审计深度 | 说明 |
|-------------|----------|------|
| `explicit` | **深审** | spec MUST → FB 实现 → op-geth → 测试断言 |
| `deviation` | **深审** | 同上；须验证 deviation 是否有正向测试（matrix Test ref） |
| `inherited` | **Smoke + 交叉引用** | 交叉引用 ETH 审计结论；验证 OP 路径 wiring 与 baseline 测试 |
| `feature-gated` | **验证未激活** | 确认 Isthmus profile 下字段/行为确实 gated；标 📋 或 ⚪ |
| `unsupported` | **标 ⚪** | 确认 OP 路径无意外激活 |

**OP 原生深审域（matrix + 增补）：**

| 域 | 来源 | 关键实现 / 测试 |
|----|------|-----------------|
| Deposit tx | matrix | `OpStackDepositTx`, `DepositTxPreCheckTest`, `DepositMintTest` |
| L1 data fee | matrix | `RollupCost`, `l1CostFjord`, `RollupCostTest`, `OpStackFeeTest` |
| Operator fee | **增补** | `operatorCostIsthmus`, `RefundIsthmusTest`, `m_isIsthmus` |
| L1 attributes deposit | **增补** | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest` |
| Floor gas / 7623 | matrix (`deviation`) | `OpStackFloorGas`, `OpStackSettlement`, `OpStackFloorGasTest` |
| L1Block predeploy | matrix (`deviation`) | `L1BlockPredeploy`, `L1BlockStorage`, `L1BlockGetterTest` |
| Receipt meta | matrix | `OpStackReceiptMeta`, `OpStackSettlementTest` |
| 7702 precheck | matrix | `OpStackPreCheck`, `Eip7702PreCheckTest` |
| Executor wiring | **增补** | `OpStackTransactionExecutorImpl.h`, `OpStackTxInputBuilder` |
| 7702 路径传播 | inherited | `OpStack7702ExecuteViaHostPropagationTest` 等 |

**Inherited smoke 行（交叉引用 ETH 审计，不重复深审）：** 2929 runtime/tx-entry、7702 kernel、2537、4844 profile、6780、builtin precompiles 等。

**明确 ⚪（Isthmus profile）：** EIP-7212（OSAKA）、EIP-7823（未设）、BCOS 特有行（auth、value transfer、21000 gas debit 等）。

### 3.4 排除项

| 排除 | 原因 |
|------|------|
| `bcos-evm/bcos/**` | BCOS TE baseline，非本审计对象 |
| ETH reference 路径深审 | 已有 `2026-06-20-eth-reference-cancun-plus-audit-design.md` |
| `bcos-executor/**` | ADR-001 out of scope |
| evmone opcode 实现体 | 委托给 evmone；FB 只审 Host 边界 |
| Bedrock→Fjord 历史 activation 切换 | Isthmus 单点 profile；仅审 Isthmus 生效公式 |

### 3.5 evmone 边界规则

与 ETH 审计相同：仅当 revision 传错或 Host/OpHostExtension 回调违反 EIP MUST 时，对 evmone-delegated EIP 标 🔴。

### 3.6 Executor 集成层（端到端深审）

因审计边界包含 `transaction-executor/bcos-transaction-executor/**`，以下项为 **必审 checklist**（非可选 smoke）：

| 检查项 | 期望 | 对照 |
|--------|------|------|
| `revisionConfig` | `makeIsthmusRevisionConfig()` | `RevisionConfigProfileTest` / op-geth Isthmus EVM 语义 |
| `m_isIsthmus` | Isthmus 路径为 `true` | op-geth operator fee 激活条件 |
| `rollupCostData` | 自 Web3 tx 正确构建 | op-geth `newRollupCostData` |
| gas caps / blob fields | `fillGasCaps` / `fillWeb3Fields` | op-geth tx parsing |
| receipt 扩展字段 | `l1Fee`, `operatorFee`, `depositNonce` | op-geth receipt / specs |
| baseFee / blobBaseFee | `resolveOpStackBaseFee`, blob base fee | L1Block storage |

---

## 4. 方法论

### 4.1 执行策略（方案 1：Matrix 驱动 + 分级深度）

1. **Matrix + 增补清单驱动（主骨架）：** 从 capability matrix OPStack 列导出全部行，合并 §3.3 增补清单，按深度表分配工作量。
2. **Skill 串联（执行细则）：** 复用 `fisco-evm-review`（`eip` 模式，裁剪 OPStack Isthmus）与 `fisco-evm-test-coverage`（`full` 模式，含断言审计）。
3. **测试向量验证：** op-geth 相关测试/fixture、FB `bcos-evm/test/opstack/**`、`transaction-executor/tests/**`、matrix Test ref——必做步骤。
4. **审计启动前置：** clone `ethereum-optimism/specs` 并 pin commit SHA（见 §4.4）。

### 4.2 每条能力的审计流水线

**深审行（`explicit` / `deviation` / 增补清单）：**

1. 读 Optimism specs / EIP 的 **MUST** 条款（按域选择，见 §4.3）
2. 定位 FB 实现，标注层级（kernel / tx-input / orchestration / host-extension / revision-profile / executor-integration）
3. 对照本地 op-geth（symbol 搜索，不用行号）
4. 查 FB 测试 + op-geth 对照实现/测试
5. 断言审计：读 `EXPECT_*` 体，对照 op-geth literal 或公式推导值

**Inherited 行（smoke）：**

1. 读取 ETH 审计报告对应行结论（若已完成）
2. 若 ETH 审计报告 **未完成** → 标 🟡 `blocked: ETH audit pending`（见 §4.7），**不计入可裁决合并判定**
3. 若 ETH 审计已完成：ETH 🔴 → 继承 🔴；ETH ✅ → 验证 OP wiring + baseline 测试
4. 若 ETH ✅ 但 OP 缺测 → 🟡；若 OP wiring 断裂 → 🔴

### 4.3 金标准优先级（分层 C）

**OP 特有行为**（deposit、L1 data fee、operator fee、L1Block、L1 attributes、receipt meta、OP 侧 7623/floor gas deviation）：

```
optimism/specs MUST（pin commit）>  op-geth  >  FB 测试  >  直觉
```

**共享 Ethereum EIP**（2929、7702、2537、6780、4844 profile、builtin precompiles 等）：

```
EIP MUST / execution-specs  >  geth v1.17.3 ≈ Besu 26.6.0  >  op-geth（OP 路径 wiring）>  FB 测试
```

**冲突裁决：**

- optimism/specs 与 op-geth 分歧 → 标 🟡，记录 spec 引用与 op-geth 符号
- geth 与 Besu 分歧 → 标 🟡，以 EIP MUST 为准
- ETH 审计 🔴 且 OP inherited 行依赖同一内核 → OP 行继承 🔴（注明交叉引用）

**资料获取：**

- 优先读本地 op-geth / geth / Besu / **clone 的 optimism/specs**
- **禁止**跳过本地 op-geth 直接 WebFetch 实现代码（除非本地仓库不可读）
- optimism/specs：审计启动时 clone 并 pin SHA；WebFetch 仅作 fallback

### 4.4 参考仓库路径

| 用途 | 路径 | 版本 |
|------|------|------|
| OP 行为金标准 | `/Users/octopus/octo/code/blockchain-impl/op-geth` | v1.101702.2 |
| Ethereum EIP | `/Users/octopus/octo/code/blockchain-impl/go-ethereum` | v1.17.3 |
| Ethereum EIP | `/Users/octopus/octo/code/blockchain-impl/besu` | tag 26.6.0 |
| OP 规范文字 | clone 目标：`blockchain-impl/ethereum-optimism-specs`（或审计笔记中记录的实际路径） | **pin commit SHA**（写入 Part 0） |

**审计启动步骤（specs）：**

1. `git clone https://github.com/ethereum-optimism/specs` → 建议路径 `blockchain-impl/ethereum-optimism-specs`
2. 选定与 op-geth `v1.101702.2` / Isthmus 文档一致的 commit，记录 SHA
3. 仅读 Isthmus 相关章节（deposit、rollup cost、operator fee、L1 attributes 等）

**op-geth 关键对照文件（入口提示，非 exhaustive）：**

- `core/types/deposit_tx.go` — deposit tx
- `core/state_transition.go` — deposit 失败 nonce/gas（§4.6）
- `core/types/rollup_cost.go` — L1 cost、Fjord、operator fee
- `params/protocol_params.go` — predeploy 地址、Isthmus 常量

**transaction-executor 关键对照文件：**

- `OpStackTransactionExecutorImpl.h` — 端到端输入构建与 receipt
- `OpStackTxInputBuilder`（同目录）— Web3 字段与 rollup cost
- `tests/TestOpStackTransactionExecutorFixture.cpp` — executor smoke

### 4.5 必读上下文文件

1. `bcos-evm/capability-matrix.md`（OPStack 列）
2. `bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md`
3. `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md`
4. `bcos-evm/docs/adr/004-revision-config-field-consumption.md`（profile-only 字段）
5. `bcos-evm/opstack/OpStackConstants.h`
6. `bcos-evm/eth/RevisionConfig.h`（`makeIsthmusRevisionConfig`）
7. ETH 审计 spec（交叉引用）：`docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-audit-design.md`

### 4.6 特殊规则

- **Fjord L1 cost：** `flzCompressLen`、`newRollupCostData`、系数常量须与 op-geth `rollup_cost.go` 逐项对照（非抽样）
- **Operator fee（Isthmus）：** `buyGas` / `refundIsthmusOperatorCost` / `m_isIsthmus` wiring（executor + opstack）与 op-geth Isthmus 分支对照
- **Deposit tx：** mint 规则、fee 豁免、`skipNonceChecks` 等与 specs + op-geth 对照
- **Deposit 失败路径（深审子项）：** 执行失败时 `state.revert()` 后 **nonce 仍 +1**、gas 记为 `gasLimit`（对齐 op-geth `state_transition.go` deposit 失败处理）；与 op-geth 不一致 → 🔴；无独立测试 → 🟡
- **L1 attributes deposit：** 176 字节 calldata 布局、失败不 commit slot、成功后影响后续 L1 fee——独立深审域
- **L1Block：** predeploy 地址、storage slot、getter 语义与 op-geth / specs 对照
- **7623 floor gas deviation：** 深审 OP 特有 settlement（`OpStackFloorGas`、`postExecuteGasSettlement`），与 op-geth OP 路径对照
- **发现 🔴 仍完成全部能力审计**，供修复排期参考
- **Inherited 行不重复 ETH 深审**，但必须验证 OP 路径 baseline-reachable

### 4.7 ETH 审计并行与 inherited 行冻结

OP 审计可与 ETH 审计 **并行** 启动。当 ETH 审计报告未完成时：

- inherited 行统一标 🟡，备注 `blocked: ETH audit pending`
- 该行计入 Part 0 **待决行** 统计，**不计入可裁决合并判定**
- ETH 报告完成后，对 blocked 行做增量更新（或开 follow-up 审计片段）

---

## 5. 严重等级

| 等级 | 含义 | 示例 |
|------|------|------|
| 🔴 阻断 | 违反 spec/EIP MUST；或 op-geth/geth≈Besu 一致但 FB 不一致 | L1 cost 公式错误；`m_isIsthmus` 未接线；失败 deposit nonce 未 bump |
| 🟡 警告 | 规范模糊、单侧实现、OP 路径缺测、断言可疑、spec 与 op-geth 分歧 | inherited 行 ETH ✅ 但无 OP propagation 测试 |
| 🟡 `blocked: ETH audit pending` | inherited 行待 ETH 交叉引用 | 2929 kernel 行 ETH 报告未出 |
| ✅ 一致 | 规范 + 客户端 + 实现一致 | `l1CostFjord` 与 op-geth 一致且有公式测试 |
| 📋 设计选择 | matrix 已标 `unsupported`/`deviation` 且正向测试存在 | floor gas deviation；L1Block predeploy |
| ⚪ 不适用 | Isthmus profile 下不激活 | 7212、7823、BCOS 特有行 |

### 5.1 合并判定（双轨统计）

**可裁决行**（深审行 + 已完成 ETH 交叉引用的 inherited 行 + 增补清单中已审计项）：

| 结果 | 条件 |
|------|------|
| ❌ 不通过 | 任一 🔴 |
| ⚠️ 有条件通过 | 无 🔴，有 🟡（不含 `blocked: ETH audit pending`） |
| ✅ 通过 | 仅 ✅ / 📋 / ⚪ |

**待决行**（不影响主判定）：

- 🟡 `blocked: ETH audit pending` — ETH 审计未完成
- 增补清单项若审计时尚未合入 matrix — Part 4 输出 matrix patch，Part 0 计入「matrix-pending」计数

**Part 0 示例：**

```
可裁决：42 行 → ✅ 30 / 🟡 5 / 🔴 2 → 主判定 ❌ 不通过
待决：  12 行 → blocked:ETH-pending 8 / matrix-patch-pending 4
```

---

## 6. 交付物（完整审计包 C）

**输出路径：** `bcos-evm/docs/audits/YYYY-MM-DD-opstack-isthmus-audit.md`

### Part 0 — 执行摘要（约 1 页）

- 审计范围、分支/commit、op-geth/geth/Besu/**optimism/specs commit SHA** pin
- **双轨统计：** 可裁决行 vs 待决行
- 主合并判定（仅可裁决行）
- Top 5 阻断项（一句话 + 文件指针）

### Part 1 — 合规矩阵

| 能力 | 层级 | 清单来源 | Matrix 状态 | 审计深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |

分组顺序：

1. OP 原生深审（matrix `explicit`/`deviation`）
2. **增补能力（待 matrix 合入）**
3. 共享 inherited（smoke）
4. feature-gated / unsupported

### Part 2 — 偏离项详情（仅 🟡/🔴）

每项含：现象、规范引用、金标准（op-geth/geth 符号或测试名）、严重度、修复建议（文件级）、ETH 审计交叉引用（若 applicable）。

### Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |

覆盖：

- `bcos-evm/test/opstack/**`（目录扫描，不写死文件数）
- `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` 等 executor 测试
- matrix Test ref 引用的 OP 路径共享测试

标出**假覆盖**与缺测（含 deposit 失败路径、L1 attributes fee 联动、`m_isIsthmus` wiring）。

### Part 4 — 后续动作

- 🔴 修复列表（优先级排序）
- 🟡 补测/改断言列表
- **capability matrix OPStack 列 patch 建议（强制）** — operator fee、L1 attributes、executor wiring 等增补行
- inherited 行 baseline-reachable 测试缺口
- ETH blocked 行清单（待 ETH 报告后关闭）

---

## 7. AI 任务描述模板（可复制）

```markdown
# 任务：bcos-evm OPStack TE Baseline（Isthmus）规范合规审计

## 目标
对 FISCO-BCOS **OPStack TE baseline 端到端**（transaction-executor + bcos-evm，Isthmus profile）
做一次性全量合规审计（方案 1：Matrix 驱动 + 分级深度）：
- OP 特有行为对照 optimism/specs（pin SHA）+ op-geth（深审）
- 共享 Ethereum EIP 在 OP 路径上 smoke + 交叉引用 ETH 审计
- test/opstack + executor 测试断言审计

## 范围
- **路径**：OpStackTxInputBuilder → OpStackTransactionExecutorImpl → opStackExecuteViaHost → executeMessage → EthHost + OpHostExtension
- **代码**：`bcos-evm/opstack/**`、`bcos-evm/eth/**`（只读）、`transaction-executor/bcos-transaction-executor/**`
- **Profile**：Isthmus（PRAGUE + operator fee；L1 cost Fjord）
- **排除**：bcos/**、ETH reference 深审、legacy bcos-executor/**、evmone opcode 内部

## 能力清单
- **Matrix OPStack 列** + **增补清单**（operator fee、L1 attributes deposit、executor Isthmus wiring）
- **深审**：deposit（含失败 nonce/gas）、L1 fee、operator fee、L1 attributes、floor gas、L1Block、receipt、7702 precheck、4844 blob、executor 集成
- **Smoke + ETH 交叉引用**（inherited）；ETH 未完成 → blocked:ETH-audit-pending
- **⚪**：7212、7823、BCOS 特有行

## 必读
本设计 spec：`docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`

## 参考仓库（审计启动时 clone specs 并 pin SHA）
| 用途 | 路径 | 版本 |
|------|------|------|
| OP 行为 | blockchain-impl/op-geth | v1.101702.2 |
| OP 规范 | clone ethereum-optimism/specs → pin SHA in Part 0 | commit SHA |
| Ethereum | go-ethereum v1.17.3, besu 26.6.0 | 同 ETH 审计 |

## 合并判定
双轨：可裁决行 → ❌/⚠️/✅；待决行（ETH-pending）单独统计，不影响主判定

## 交付物
bcos-evm/docs/audits/YYYY-MM-DD-opstack-isthmus-audit.md（Part 0–4）

## Skill
fisco-evm-review（eip，OPStack Isthmus）；fisco-evm-test-coverage（full + 断言）
```

### 触发词

> OPStack Isthmus 合规审计 / opStackExecuteViaHost spec 对照 / OPStack TE baseline 审计 / OPStack matrix 审计

---

## 8. Done 标准

- [ ] 审计报告 Part 0–4 齐全
- [ ] matrix OPStack 列每一行 + 增补清单每一项均有 Part 1 条目
- [ ] 每条可裁决 🟡/🔴 有规范引用与 FB 文件指针
- [ ] Part 3 覆盖 `test/opstack/**`（目录扫描）+ executor 集成测试
- [ ] 报告头注明 branch/commit、op-geth/geth/Besu tag、**optimism/specs commit SHA**
- [ ] inherited 行注明 ETH 交叉引用或 `blocked: ETH audit pending`
- [ ] Part 0 双轨统计 + 主合并判定明确
- [ ] Part 4 含 matrix patch 建议

---

## 9. 与现有 skill / spec 的关系

| 资产 | 本审计中的角色 |
|------|----------------|
| `fisco-evm-review` | 实现走读、op-geth/geth/Besu 交叉、严重等级 |
| `fisco-evm-test-coverage` | 覆盖矩阵、断言审计、假覆盖检测 |
| ETH 审计 spec / 报告 | inherited 行交叉引用；未完成则 blocked |
| `capability-matrix.md` | 清单骨架；增补项 Part 4 回填 |

---

## 10. Grill-me 决策记录（2026-06-20）

| # | 问题 | 决策 |
|---|------|------|
| 1 | 审计边界是否含 transaction-executor | **B** — 完整 TE baseline 端到端 |
| 2 | ETH 审计未完成时 inherited 行 | **C** — 🟡 `blocked: ETH audit pending`，不计入主判定 |
| 3 | matrix 未覆盖项（operator fee 等） | **B** — Part 1 增补分组 + Part 4 强制 matrix patch |
| 4 | L1 attributes deposit | **B** — 独立深审域（增补清单） |
| 5 | deposit 失败 nonce/gas | **A** — deposit 深审子项；不一致 🔴，缺测 🟡 |
| 6 | optimism/specs 版本 | **A** — 审计启动 clone 并 pin commit SHA |
| 7 | 合并判定 | **A** — 双轨统计（可裁决 vs 待决） |

---

## 11. 后续

用户评审本 spec 通过后，使用 `writing-plans` skill 生成审计执行计划（分阶段、可并行子任务、预估工作量）。

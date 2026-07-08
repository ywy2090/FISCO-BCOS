# ETH Reference 路径 CANCUN+ 全量 EIP 合规审计 — 设计规格

**日期：** 2026-06-20  
**状态：** 待评审  
**类型：** 一次性全量审计（非 PR 增量审查）

---

## 1. 背景与动机

`bcos-evm` 在 `feat-evm-refactor` 分支上已完成 ETH reference 执行路径（`executeViaEth`）与共享内核 `executeMessage()` 的分层。`capability-matrix.md` 与 ADR-001 已冻结「reference path vs TE baseline path」的语义，但尚未对 **ETH reference 路径从 CANCUN 起的 EIP 实现** 做过系统性的规范合规盘点。

本 spec 定义一次 AI 辅助审计的任务边界、方法论、严重等级与交付物格式，使审计可复现、可存档，并与现有 `fisco-evm-review` / `fisco-evm-test-coverage` skill 对齐。

---

## 2. 审计目标

判断 `bcos-evm/eth/**` 在 **ETH reference 路径** 上，从 **CANCUN** 硬分叉至 **OSAKA** 阶梯内涉及的 EIP/EVM 行为是否与以太坊官方规范一致，并通过 geth/Besu 交叉验证与测试向量断言审计佐证结论。

**非目标：**

- 不审计 BCOS TE baseline（`executeViaHost`）或 OPStack TE baseline（`opStackExecuteViaHost`）——除非作为交叉参考
- 不审计 legacy `bcos-executor` / DAG / `HostContext`
- 不深度审计 evmone 内部 opcode 实现（仅审 revision 传递与 Host 回调）

---

## 3. 范围

### 3.1 执行路径

```
EthTxInputBuilder / ExecuteViaEth.cpp
        ↓
executeViaEth()
        ↓
executeMessage()          ← 共享内核
        ↓
EthHost / State / EthPrecompiles + evmone
```

**代码根目录：** `bcos-evm/eth/**`  
**公共头（入口索引）：** `bcos-evm/include/bcos-evm/eth_executor.hpp`, `executor.hpp`

### 3.2 分叉阶梯

按 `EthPolicy.h` 的 `evmcRevisionFromBlockNumber` 与 `computeRevisionConfig`：

| Revision | 区块阈值（EthPolicy） | 本审计纳入的 EIP |
|----------|----------------------|------------------|
| CANCUN | ≥ 19,426,587 | 1153, 4844 (profile), 5656†, 6780 |
| PRAGUE | ≥ 22,000,000 | 2537, 7623, 7702 |
| OSAKA | ≥ 25,000,000 | 7212, 7823 |

† **5656 (MCOPY)** 等为 evmone-delegated：只验证 `evmc_revision` 正确传入，不审 evmone 源码。

### 3.3 前置仍生效的 EIP（ETH reference 路径）

审计 CANCUN+ 时须确认其前置依赖在 ETH 路径上仍正确，包括但不限于：

- EIP-2929 runtime warm/cold（kernel）
- Builtin precompiles 0x01–0x11（kernel）
- EIP-7702 依赖的 delegation 状态机（kernel + tx-input）

完整行清单以 `bcos-evm/capability-matrix.md` **ETH (reference)** 列为准，裁剪为 CANCUN+ 相关行及直接前置行。

### 3.4 排除项

| 排除 | 原因 |
|------|------|
| `bcos-evm/bcos/**` | BCOS TE baseline，非本审计对象 |
| `bcos-evm/opstack/**` | OPStack TE baseline |
| `bcos-executor/**` | ADR-001 明确 out of scope |
| evmone opcode 实现体 | 委托给 evmone；FB 只审 Host 边界 |

### 3.5 evmone 边界规则

| FB 负责 | evmone 负责 |
|---------|-------------|
| Host 接口回调、state 转换、预编译、access list、gas 记账 | Opcode 语义（PUSH0、MCOPY、RJUMP 等） |
| tx input 字段映射、orchestration（precheck、settlement） | EVM 解释器内部 |
| `RevisionConfig` / `evmc_revision` 传递 | 按 revision 启用 opcode |

仅当 revision 传错或 Host 回调违反 EIP MUST 时，对 evmone-delegated EIP 标 🔴。

---

## 4. 方法论

### 4.1 执行策略（推荐混合）

1. **EIP 清单驱动（主骨架）：** 从 `EthPolicy` + capability matrix ETH 列生成 CANCUN→OSAKA 清单，逐条审计。
2. **Skill 串联（执行细则）：** 复用 `fisco-evm-review`（`eip` 模式，裁剪范围）与 `fisco-evm-test-coverage`（`full` 模式，含断言审计）。
3. **测试向量验证（每条 EIP 的验证步骤）：** geth testdata、Besu referencetests、FB `bcos-evm/test` fixture——非唯一入口，但是必做步骤。

### 4.2 每条 EIP 的审计流水线

1. 读 EIP 正文与 [execution-specs](https://github.com/ethereum/execution-specs) 的 **MUST** 条款
2. 定位 FB 实现，标注层级（kernel / tx-input / orchestration / revision-profile / evmone-delegated）
3. 对照本地 geth v1.17.3（symbol 搜索，不用行号）
4. 对照本地 Besu tag 26.6.0
5. 查 FB 测试 + geth/Besu 参考测试
6. 断言审计：读 `EXPECT_*` 体，对照金标准 literal 期望值

### 4.3 资料优先级（冲突裁决）

```
EIP MUST / execution-specs  >  geth v1.17.3 ≈ Besu 26.6.0  >  FB 测试通过  >  EIP 页面直觉
```

- geth 与 Besu 在 Prague 语义上应一致；若二者分歧，以规范 MUST 为准并标 🟡
- **禁止**跳过本地 geth/Besu 直接 WebFetch raw（除非本地仓库不可读）

### 4.4 参考仓库路径

| 客户端 | 路径 | 版本 |
|--------|------|------|
| geth | `/Users/octopus/octo/code/blockchain-impl/go-ethereum` | v1.17.3 |
| Besu | `/Users/octopus/octo/code/blockchain-impl/besu` | tag 26.6.0 |

### 4.5 必读上下文文件

1. `bcos-evm/capability-matrix.md`（ETH 列）
2. `bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md`
3. `bcos-evm/docs/adr/004-revision-config-field-consumption.md`
4. `bcos-evm/eth/vm/EthPolicy.h`
5. `bcos-evm/eth/RevisionConfig.h`

### 4.6 特殊规则

- **EIP-2537：** BLS 预编译折扣表 128 项须逐项对比，不可抽样
- **EIP-4844：** ETH reference 路径无 blob orchestration（matrix 标 `unsupported`），但仍须审 revision profile 与内核边界
- **发现 🔴 仍完成全部 EIP 审计**，供修复排期参考

---

## 5. 严重等级

| 等级 | 含义 | 示例 |
|------|------|------|
| 🔴 阻断 | 违反 EIP MUST；或 geth≈Besu 一致但 FB 不一致 | 7623 floor gas 算错；7702 delegation 未应用 |
| 🟡 警告 | 规范模糊、单侧实现、测试覆盖不足、断言可疑 | 有测但期望值与 geth 不符 |
| ✅ 一致 | 规范 + 客户端 + 实现一致 | 2929 warm/cold 与 geth 一致 |
| 📋 设计选择 | 有意偏离且 matrix/ADR 已文档化 | 4844 blob orchestration `unsupported` |
| ⚪ 不适用 | 不在 CANCUN+ ETH reference 范围 | BCOS 21000 gas debit |

**合并判定：**

| 结果 | 条件 |
|------|------|
| ❌ 不通过 | 任一 🔴 |
| ⚠️ 有条件通过 | 无 🔴，有 🟡 |
| ✅ 通过 | 仅 ✅ / 📋 / ⚪ |

---

## 6. 交付物（完整审计包 C）

**输出路径：** `bcos-evm/docs/audits/YYYY-MM-DD-eth-reference-cancun-plus-audit.md`

### Part 0 — 执行摘要（约 1 页）

- 审计范围、分支/commit、geth/Besu 版本 pin
- 统计：EIP 行数 / ✅ / 🟡 / 🔴 / 📋
- 合并判定
- Top 5 阻断项（一句话 + 文件指针）

### Part 1 — 合规矩阵

| EIP | 层级 | 状态 | Spec 依据 | FB 实现 | geth 对照 | Besu 对照 | FB 测试 | 缺口 |

按 CANCUN → PRAGUE → OSAKA 分组；`evmone-delegated` 行单独标注。

### Part 2 — 偏离项详情（仅 🟡/🔴）

每项含：现象、规范引用、金标准（geth/Besu 符号或测试名）、严重度、修复建议（文件级）。

### Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |

标出**假覆盖**（有文件但断言错误或路径未命中）与缺测。

### Part 4 — 后续动作

- 🔴 修复列表（优先级排序）
- 🟡 补测/改断言列表
- capability matrix ETH 列建议更新（如有）

---

## 7. AI 任务描述模板（可复制）

```markdown
# 任务：bcos-evm ETH Reference 路径 CANCUN+ 全量 EIP 合规审计

## 目标
对 FISCO-BCOS `bcos-evm` 的 **ETH reference 执行路径** 做一次性全量审计，
判断实现是否与以太坊官方规范（EIP MUST + execution-specs）一致，
并用 geth v1.17.3 / Besu 26.6.0 交叉验证，辅以测试向量与断言审计。

## 范围
- **路径**：`executeViaEth` → `executeMessage()` → `EthHost` / `State` / `evmone`
- **代码**：`bcos-evm/eth/**`（不含 `bcos/`、`opstack/`、legacy `bcos-executor/`）
- **分叉**：`EVMC_CANCUN` ≤ revision ≤ `EVMC_OSAKA`（按 `EthPolicy.h` 阶梯）
- **EIP 清单**：1153, 4844(profile), 5656(evmone), 6780, 2537, 7623, 7702, 7212, 7823；
  以及 ETH reference 路径上仍生效的前置 EIP（2929 runtime、预编译 0x01–0x11 等）
- **排除**：evmone 内部 opcode 实现（仅审 revision 传递与 Host 回调）

## 必读上下文
1. `bcos-evm/capability-matrix.md`（ETH 列）
2. `bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md`
3. `bcos-evm/docs/adr/004-revision-config-field-consumption.md`
4. 设计 spec：`docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-audit-design.md`
5. 本地 geth：`/Users/octopus/octo/code/blockchain-impl/go-ethereum`（v1.17.3）
6. 本地 Besu：`/Users/octopus/octo/code/blockchain-impl/besu`（tag 26.6.0）

## 方法论（每条 EIP 同一流程）
1. 读 EIP / execution-specs 的 MUST 条款
2. 定位 FB 实现（kernel / tx-input / orchestration / revision-profile）
3. 对照 geth 实现（symbol 搜索，不用行号）
4. 对照 Besu 实现
5. 查 FB 测试 + geth testdata + Besu referencetests
6. 做断言审计（读 EXPECT_* 体，禁止「有文件即充分」）

**冲突裁决**：规范 MUST > geth ≈ Besu > FB 测试 > 直觉

## 严重等级
- 🔴 阻断：违反 MUST；或 geth≈Besu 一致但 FB 不一致
- 🟡 警告：规范模糊、覆盖不足、断言可疑
- ✅ 一致
- 📋 设计选择（matrix 已标 unsupported/deviation）
- ⚪ 不适用

## 交付物
写入 `bcos-evm/docs/audits/YYYY-MM-DD-eth-reference-cancun-plus-audit.md`：
- Part 0 执行摘要
- Part 1 合规矩阵（EIP × 层级）
- Part 2 偏离项详情（🟡/🔴）
- Part 3 测试断言审计表
- Part 4 后续动作建议

## 约束
- 禁止跳过本地 geth/Besu 直接 WebFetch raw（除非本地不可读）
- evmone 层 EIP（PUSH0、MCOPY 等）标「evmone-delegated」，不深度审 evmone 源码
- EIP-2537 折扣表 128 项需逐项对比，不可抽样
- 发现 🔴 仍完成全部 EIP 审计，供修复参考

## 可选 Skill 引用
- 实现合规：`fisco-evm-review`（eip 模式，裁剪为 CANCUN+ ETH path）
- 测试覆盖：`fisco-evm-test-coverage`（full 模式，含断言审计）
```

### 触发词

> ETH reference 全量审计 / CANCUN+ 合规审计 / executeViaEth spec 对照 / EIP 合规盘点

---

## 8. Done 标准

- [ ] 审计报告四部分齐全（Part 0–4）
- [ ] capability matrix ETH 列涉及的 CANCUN+ 行均有矩阵条目
- [ ] 每条 🟡/🔴 有规范引用与 FB 文件指针
- [ ] 测试断言审计覆盖 `bcos-evm/test/eth/**` 与相关 `test/state/**`、`test/fixtures/**`
- [ ] 报告头注明 branch/commit、geth/Besu tag
- [ ] 合并判定明确（通过 / 有条件通过 / 不通过）

---

## 9. 与现有 skill 的关系

| Skill | 本审计中的角色 |
|-------|----------------|
| `fisco-evm-review` | 实现走读、geth/Besu 交叉、严重等级 |
| `fisco-evm-test-coverage` | 覆盖矩阵、断言审计、假覆盖检测 |

本 spec **不替代**上述 skill 的流程细节，而是为其增加：**固定范围（ETH reference + CANCUN+）** 与 **固定交付物格式（审计包 C）**。

---

## 10. 后续

用户评审本 spec 通过后，使用 `writing-plans` skill 生成审计执行计划（分阶段、可并行子任务、预估工作量）。

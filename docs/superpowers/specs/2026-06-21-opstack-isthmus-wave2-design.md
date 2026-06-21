# OPStack Isthmus Wave 2 — ⚠️→✅ 闭合设计规格

**日期：** 2026-06-21  
**状态：** 已评审（用户同意 A 方案）；**Grill 2026-06-21** 闭合范围歧义（ADR-008～013）

**Grill 决策摘要：**

| 主题 | 决策 | ADR |
|------|------|-----|
| Receipt scalar/constant | TE 内存 only | 008 |
| EIP-4844 E2E | orchestration 够，无 TE blob E2E | 009 |
| 4844 preCheck | 全覆盖 | 009 |
| CI gate | 触发即全量 build + ctest | 010 |
| L1 attributes | TE `isthmus_l1_attributes.bin` E2E | 011 |
| Fjord / signed RLP | 执行 Task 4/5，非 verify-only | 012 |
| 7702 fixture | 仅文档标注 hand-crafted | 013 |
| 7702 refund | 必须新增 existence refund case | 013 |  
**前置：** Remediation OP-01～15 @ `54e17a62c`；复审计主判定 **⚠️ 有条件通过**  
**关联审计：** `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`  
**实现计划：** `docs/superpowers/plans/2026-06-21-opstack-isthmus-wave2-fix.md`

---

## 1. 目标

将 **OPStack Isthmus TE baseline**（`transaction-executor` + `bcos-evm`）从 **⚠️ 有条件通过** 提升为 **✅ 通过**：

1. 闭合复审计 Part 4 **放行条件**（R1–R2）
2. 清零 Part 2 全部 **🟡**（测试断言、数值 parity、文档）
3. `task10-assertions.md` OPStack 相关行 **🟡→✅**，**🔴=0**

---

## 2. 范围

### 2.1 In scope

| 域 | 内容 |
|----|------|
| 协议层 | `TransactionReceipt` 暴露 `operatorFeeScalar` / `operatorFeeConstant` |
| 编排层 | `OpStackTransactionExecutorImpl::makeReceipt` 从 `OpStackReceiptMeta` 写入 receipt |
| 测试 | Fjord 精确向量、signed RLP L1 fee TE E2E、deposit nonce、4844 orchestration preCheck、7702 refund、fixture 修正 |
| CI | `capability-gate` 合入；opstack + executor fixture 全量 CTest |
| 文档 | 审计报告主判定 ✅；task*.md / inventory 同步 |

### 2.2 Out of scope（⚪，本 epic 不做）

- GPO predeploy `0x4200…000F`
- `setFeature`、`proxyAdmin*`、Bedrock/Jovian setter、`setIsthmus()` 升级迁移
- BCOS TE EIP-7702 intrinsic/precheck（ADR-006 `unsupported`）
- ETH reference `executeViaEth` 7702 intrinsic 扣费
- legacy `bcos-executor` CompatEip7702
- 修改 `loadOpStackFeeParams` / L1Block 计费 orchestration（OP-14 约束保持）
- TE blob type-0x03 全链路 E2E（ADR-009；4844 以 orchestration 单测闭合 D7-1）

---

## 3. 缺陷 → 工作包映射（FIX-01～15）

### Wave 1 — 放行门

| ID | 审计引用 | 问题 | 交付 |
|----|----------|------|------|
| FIX-01 | R1, D3-2 | Receipt 仅有 `operatorFee`，无 scalar/constant | 协议字段 + `makeReceipt` 接线 + fixture 断言 |
| FIX-02 | R2, E1 | 无 CI opstack 全量 gate | `.github/workflows/capability-gate.yml` 合主分支 + job |
| FIX-03 | E3 | 未在最新 HEAD 验证 | 记录 CTest 结果于审计报告 |

### Wave 2 — 🟡 测试与 parity

| ID | 审计引用 | 问题 | 交付 |
|----|----------|------|------|
| FIX-04 | R3, D2-1 | 无 Fjord Solidity 精确向量 | **执行 Task 4**（ADR-012；不因 HEAD 已有用例而 verify-only） |
| FIX-05 | R4, D2-2 | signed RLP → L1 fee 无 TE E2E | **执行 Task 5**（ADR-012；补全/加固 TE 断言） |
| FIX-06 | D5-4 | L1 attributes 无 depositNonce/nonce | **TE** 跑 `isthmus_l1_attributes.bin` + receipt nonce（ADR-011） |
| FIX-07 | D1-1 | TE revert 未断言 operator fee | fixture 失败路径 meta/receipt |
| FIX-08 | D7-1 | ~~4844 无 full executor E2E~~ | **⚪ 取消**（ADR-009）；orchestration 证据足够 |
| FIX-09 | D7-1 | 4844 preCheck 形状不全 | **全覆盖** `BlobGasBalanceTest` / `OpStackPreCheck`（grill C） |
| FIX-10 | T5 | `stEIP7702_delegation` 假覆盖 | **文档标注** hand-crafted（ADR-013 A；不改名/不移除） |
| FIX-11 | T6 | 7702 existence refund 无单测 | **新增** refund case（ADR-013 D） |
| FIX-12 | D8-1, T8 | inherited profile 稀疏 | matrix 脚注 + task8 闭合 |

### Wave 3 — 审计签收

| ID | 交付 |
|----|------|
| FIX-13 | `opstack-isthmus-audit.md` 主判定 ✅ |
| FIX-14 | `task10-assertions.md`、`test-inventory-opstack.md` 同步 |
| FIX-15 | `inheritance-work-tracker` #6 `[x]` |

---

## 4. 架构要点

### 4.1 FIX-01 Receipt 字段（D3-2）

**现状：**

- `OpStackReceiptMeta` 已有 `operatorFeeScalar`、`operatorFeeConstant`（`OpStackExecuteViaHost` 在 Isthmus 且 slot 非零时填充）
- `bcos-framework/TransactionReceipt.h` 仅有 `operatorFee()` / `setOperatorFee()`
- `OpStackTransactionExecutorImpl::makeReceipt` 仅调用 `setOperatorFee`

**目标：**

- 扩展 `TransactionReceipt` 虚接口：`operatorFeeScalar()` / `operatorFeeConstant()` + setter
- `bcostars::TransactionReceiptImpl` **sidecar**（与 `l1Fee` 同模式；**不进** tars hash / wire）
- `makeReceipt` 在 meta 有值时写入 hex 字符串（与 `l1Fee` / `operatorFee` 同风格）
- `TestOpStackTransactionExecutorFixture` 已有 literal operator fee 用例 → 扩展断言 scalar/constant

**放行边界（ADR-008）：** TE 内存 `TransactionReceipt` 可读即可；JSON-RPC / tars 同步 **本 wave 不要求**。

**金标准：** 编排层 meta 与 `makeReceipt` 接线；hex 编码风格与现有 `l1Fee` 一致。

### 4.2 测试 parity 原则

- Fjord / L1 fee 向量 **pin** op-geth `e8800cffe` 数值，注释来源
- TE E2E 使用 **真实 Web3 签名 RLP**（复用 `OpStackTxInputBuilderTest` / `Web3SignedTxEncoder`）
- 不断开 OP-14：L1Block getter/setter 测试已 PASS，本 wave 不回归

### 4.3 CI（FIX-02）

- Workflow：`bcos-evm/capability-matrix.md` lint + surface-change gate（已有 `.github/workflows/capability-gate.yml`）
- **触发时全量：** configure + build + 两套 opstack ctest（ADR-010）；不单跑 lint
- `inheritance-work-tracker` 项 #6 在 workflow 合入且绿后闭合

---

## 5. PR 策略

| PR | 包含 FIX | 标题建议 |
|----|----------|----------|
| PR-1 | 01–03 | `fix(opstack): receipt operator fee fields and CI gate` |
| PR-2 | 04–11 | `test(opstack): close Isthmus assertion and parity gaps` |
| PR-3 | 12–15 | `docs(opstack): Isthmus audit sign-off to pass` |

---

## 6. 验收清单（全局）

```bash
# bcos-evm opstack 单测
ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' \
  --test-dir build/bcos-evm/test -C Debug --output-on-failure

# transaction-executor fixture
ctest -R TestOpStackTransactionExecutorFixture --test-dir build -C Debug --output-on-failure

# matrix lint
bash bcos-evm/tools/ci/check-capability-matrix.sh
```

**文档：** `opstack-isthmus-audit.md` 主判定 **✅**；Part 2 🟡 表为空或全部标 CLOSED。

---

## 7. 风险

| 风险 | 缓解 |
|------|------|
| 协议字段跨 tars/RPC | 最小增量；与 `l1Fee` 同模式 |
| tars 代码生成 | 改 `.tars` 后跑项目代码生成流程 |
| 测试与 op-geth 漂移 | 向量注释 commit pin |

---

## 8. 工作量

| Wave | FIX 数 | 估时 |
|------|--------|------|
| 1 | 3 | 2–4 天 |
| 2 | 9 | 4–6 天 |
| 3 | 3 | 1 天 |
| **合计** | **15** | **7–11 天** |

---

**审批：** 用户 2026-06-21 同意方案 A（OPStack TE → ✅，不含 ⚪ out-of-scope）。

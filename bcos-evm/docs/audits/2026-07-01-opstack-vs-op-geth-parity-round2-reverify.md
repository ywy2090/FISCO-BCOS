# bcos-evm/opstack vs op-geth Parity 复核报告（Round 2 独立重验）

**日期：** 2026-07-01  
**方法：** 4 个 explore sub-agent **独立重读源码**，不采信 Round 1 / validation 结论  
**基线：** Isthmus+ 产品基线；op-geth `v1.101702.2` @ `e8800cffe`  
**关联文档：**
- [Phase 0–2 初稿](./2026-07-01-opstack-vs-op-geth-parity-phase0-2.md)
- [Round 1 校验](./2026-07-01-opstack-vs-op-geth-parity-validation.md)

---

## Round 2 总判定

| 维度 | 结论 |
|------|------|
| **Isthmus+ 执行语义** | ✅ **基本成立** — 无 🔴 阻断项经两轮复核 |
| **Round 1 结论** | **全部维持或细化** — 无 major REFUTED |
| **新增发现** | 4 项 deposit/system tx 细节；D8 顺序表述修正；N3/N4 升为文档债 |
| **优先修复** | N1 `DepositReceiptVersion`；N2 Jovian DA footprint；N6 7702 REVERT 测试；文档/matrix/ADR 同步 |

---

## 全量差异复核表（两轮合并）

| ID | Round 1 | Round 2 | 最终等级 | Round 2 说明 |
|----|---------|---------|----------|--------------|
| D1 | 📋 | **CONFIRMED** | **📋** | pre-Fjord throw；首 Ecotone Bedrock fallback 亦缺 |
| D2 | 📋 | **CONFIRMED** | **📋** | geth 无 GPO dispatch；bcos 有；结算路径无影响 |
| D3 | 🟡→✅ | **CONFIRMED** | **✅** | OpStack 7702 REVERT = failed receipt，与 geth 一致 |
| D4 | 🟡→📋 | **CONFIRMED** | **📋** | 历史 fork 有意不支持（ADR-014） |
| D5 | ✅ | **CONFIRMED** | **✅** | buyGas 逐项对齐；gas=1618 数值向量一致 |
| D6 | ✅ | **CONFIRMED** | **✅** | applyAuthorization timing/refund/nonce 对齐 |
| D7 | ✅ | **CONFIRMED** | **✅** | deposit 失败入块语义对齐 |
| D8 | 🟡 | **PARTIAL** | **🔵/🟡** | 拒绝集等价；geth 顺序为 buyGas→intrinsic检查→floor→扣减 |
| D9 | 🔵 | **PARTIAL** | **🔵** | 模块拆分无共识风险；buyGas 失败入块策略另见 NEW-5 |
| D10a | ✅ | **CONFIRMED** | **✅** | Jovian operator/L1 attrs/GPO 已实现 |
| D10b | 🟡 | **部分闭合（receipt 层已实现，块级/出块待 Phase 2）** | **🟡** | receipt `daFootprintGasScalar`/`blobGasUsed` 已实现；`CalcDAFootprint`/miner DA 待 Phase 2 |
| D10c | 🟡 | **CONFIRMED** | **🟡** | superchain vs TE 硬编码 |
| N1 | 🟡 | **CONFIRMED** | **🟡** | `DepositReceiptVersion` 全链路缺失 |
| N2 | 🟡 | **部分闭合（receipt 层已实现，块级/出块待 Phase 2）** | **🟡** | receipt 字段已实现；块级累计/出块限流待 Phase 2 |
| N3 | 🟡 | **CONFIRMED** | **🟡 文档债** | capability-matrix 与代码矛盾 |
| N4 | 📋 | **CONFIRMED** | **🟡 文档债** | ADR-014 Jovian 描述过时 |
| N5 | 🟡 | **CONFIRMED** | **🟡** | TE 错误码不对齐 |
| N6 | 🟡 | **CONFIRMED** | **🟡** | 7702 REVERT E2E 测试缺口 |

---

## 1. Deposit 路径（Agent: deposit-reverify）

### 维持 CONFIRMED ✅

| 子项 | geth 锚点 | bcos 锚点 |
|------|-----------|-----------|
| D7 失败仍入块 | `state_transition.go:486-510, 681-688` | `OpStackTxFinalize.cpp:87-119` |
| mint 在 snapshot 前 | `state_transition.go:474-481` | `ApplyOpStackMessage.cpp:93-101` |
| 无 L1/operator fee | `state_transition.go:713` | `OpStackFeeSettlement.cpp:28-30` |
| DepositNonce 执行前 | `state_processor.go:173-176` | `ApplyOpStackMessage.cpp:93` |

**测试：** `DepositNoFeeRoutingTest`, `OpStackDepositSettlementTest`, `L1AttributesDepositFailureTest`

### 维持 CONFIRMED 🟡

**N1 — `DepositReceiptVersion`（Canyon+）**

- geth: `state_processor.go:221-226` → `CanyonDepositReceiptVersion = 1`
- bcos: `OpStackReceiptMeta.h` 仅 `depositNonce`；TE 无 setter
- 影响：Canyon+ receipt hash / RPC / 下游索引

**D7-sub — System tx 错误码**

- geth: `ErrSystemTxNotSupported`（Regolith+）
- bcos: `TransactionStatus::Malformed`
- **Round 2 补充：** 非仅错误码差异

### Round 2 新发现

| ID | 问题 | 等级 |
|----|------|------|
| NEW-1 | `depositNonce` 无 Regolith fork 门控（bcos 凡 deposit 必写） | 📋 |
| NEW-2 | System tx + 非零 mint：geth mint-then-reject vs bcos reject-before-mint | 🟡 |
| NEW-3 | System tx 失败：geth 阻断 `ApplyTransaction` vs bcos soft `earlyExit` | 🟡 |
| NEW-4 | `DepositReceiptVersion` 未接入 `TransactionReceiptImpl` | 🟡 |

---

## 2. EIP-7702（Agent: eip7702-reverify）

### D3 — CONFIRMED ✅（维持 Round 1 降级）

| 路径 | REVERT receipt | 授权持久化 |
|------|----------------|------------|
| op-geth | Failed (status=0) | ✅ Call 前 apply |
| bcos-opstack | Failed (`RevertInstruction=16`) | ✅ pre-commit + TE applyStateDiff on REVERT |
| bcos-eth | Success（ADR-015 归一化） | ✅ |

**关键锚点：**
- geth: `state_transition.go:601-622, 739-744`；`state_processor.go:206-210`
- bcos: `OpStackStateTransitionErrorPolicy.h`（无 `onFinalizeGasUsed`）
- TE: `OpStackTransactionExecutorImpl.h:179-185, 246`

**🔴 风险场景复核：**
- Auth 持久化但 receipt 标 success（OpStack）：**REFUTED**
- REVERT 导致 auth 丢失：**REFUTED**

**仍正确：** 勿向 OpStack 移植 `normalizeSetCodeTransactionVmerr`

### D6 — CONFIRMED ✅

- Refund: `12500` = `CallNewAccountGas - TxAuthTupleGas` = `25000 - 12500`
- 顺序: sender nonce bump → `applyAuthorizations` → Call

### N6 — CONFIRMED 🟡

缺失：OpStack `applyOpStackMessage` + reverting callee + receipt failed + delegation 在 state  
EEST opstack manifest 无 `self_sponsored_set_code` revert 变体

---

## 3. 费用公式（Agent: fee-reverify）

### D1 — CONFIRMED 📋

`OpStackFeeParams.cpp:53-56` throw vs geth `rollup_cost.go:157-192` Bedrock/Ecotone/first-Ecotone

### D5 — CONFIRMED ✅

逐项对齐；数值 oracle：
- Fjord empty tx L1 = **3_203_000**
- Isthmus operator(1618) = **1256417826611659930**
- Jovian operator(1618) = **1256650673615173860**

### D8 — PARTIAL 🔵/🟡

**Round 2 修正 prior 表述：**

| 阶段 | op-geth | bcos |
|------|---------|------|
| 1 | buyGas（preCheck 内） | buyGas |
| 2 | intrinsic **检查** | floor 检查 |
| 3 | floor 检查 | intrinsic 扣减 |
| 4 | intrinsic 扣减 | — |

- 拒绝集：**等价**（均用 `gasLimit` 比 floor）
- `abortNormalAfterBuyGas` revert buyGas：**CONFIRMED**
- 错误优先级：intrinsic vs floor 同时失败时**先报哪个不同**

### N5 — CONFIRMED 🟡

| 场景 | geth | bcos |
|------|------|------|
| 余额不足 | `ErrInsufficientFunds` | `NotEnoughCash` |
| floor 失败 | `ErrFloorDataGas` | `OutOfGasLimit` |

### Empty rollup nil vs 0 — CONFIRMED 无差异

---

## 4. Fork / GPO / Jovian（Agent: fork-reverify）

### D2 — CONFIRMED 📋

GPO 仅链上 CALL 语义；`wireL1CostFuncWithState` 与 geth 一致

### D4 — CONFIRMED 📋

geth 有、bcos 无：Bedrock, Regolith, Canyon, Ecotone, Granite, Holocene, Karst, Interop  
bcos 有：Fjord, Isthmus, Jovian（部分）

### D9 — PARTIAL 🔵

- 成功路径：无共识风险
- **NEW-5（关联）：** buyGas 失败时 bcos TE 可能仍入块 failed receipt；geth 拒绝应用 tx（`GAP-TE-005`）

### D10 拆分

| 子项 | Round 2 | 说明 |
|------|---------|------|
| D10a operator/L1/GPO | ✅ CONFIRMED | `operatorCostJovian`, `parseJovianL1Attributes` |
| D10b DA footprint | 🟡 部分闭合（receipt 层已实现，块级/出块待 Phase 2） | receipt 已实现；`CalcDAFootprint`/块级/miner DA 待 Phase 2 |
| D10c superchain | 🟡 CONFIRMED | TE 硬编码 `makeIsthmusPlusForkSchedule()` |

### N3 — CONFIRMED 🟡 文档债

`capability-matrix.md:66` 写「no GPO」与 `GasPriceOraclePredeploy` 实现矛盾

### N4 — CONFIRMED 🟡 文档债

ADR-014 §3「Jovian extension only」与 `operatorCostJovian` 已实现矛盾

---

## Round 1 → Round 2 结论变化

| 变更 | 说明 |
|------|------|
| D3 维持 ✅ | 两轮独立确认，非误降级 |
| D8 细化 | 修正 geth 顺序描述；可降为 🔵（拒绝集等价） |
| N3/N4 升级 | 从功能缺口 → **文档/matrix/ADR 过时** |
| 新增 NEW-1~5 | deposit/system tx、buyGas 失败入块 |
| 无 REFUTED | 两轮所有原 finding 均未被推翻 |

---

## 严重等级（Round 2 最终）

| 等级 | 数量 | 项目 |
|------|------|------|
| 🔴 阻断 | **0** | — |
| 🟡 警告 | **8** | N1, N2, N5, N6, D7-sub, D10b, NEW-2~5 |
| ✅ 一致 | **7** | D3, D5, D6, D7, D10a + 核心 deposit/fee |
| 📋 设计选择 | **3** | D1, D2, D4 |
| 🔵 架构差异 | **2** | D8, D9 |
| 🟡 文档债 | **2** | N3, N4 |

---

## 建议行动（按优先级）

### P0 — 功能缺口（Jovian / receipt）

1. 实现 Jovian DA footprint：`CalcDAFootprint` + receipt `BlobGasUsed` / `DAFootprintGasScalar`（D10b/N2）— **receipt 部分已闭合**
2. 实现 `DepositReceiptVersion` + TE wire（N1/NEW-4）

### P1 — 测试补强

3. OpStack 7702 REVERT E2E（N6）
4. OpStack EEST manifest revert 变体

### P2 — 文档同步

5. 更新 `capability-matrix.md` GPO 条目（N3）
6. 更新 ADR-014 Jovian 范围（N4）
7. 修正 validation 文档 D8 geth 顺序描述

### P3 — 可选对齐

8. TE 错误码映射（N5）
9. System tx 错误码与 geth 对齐（D7-sub/NEW-3）
10. buyGas 失败入块策略（NEW-5 / GAP-TE-005）

### 明确不做

- **勿**向 OpStack 移植 `normalizeSetCodeTransactionVmerr`（D3 两轮确认）
- **勿**为 Isthmus+ 产品基线实现 Bedrock/Ecotone L1（D1 有意偏离）

---

## 校验 Agent 索引（Round 2）

| Agent | 范围 | ID |
|-------|------|-----|
| deposit-reverify | Deposit 全路径 + N1 | D7, N1, NEW-1~4 |
| eip7702-reverify | 7702 REVERT + D6 | D3, D6, N6 |
| fee-reverify | L1/buyGas/floor | D1, D5, D8, N5 |
| fork-reverify | Fork/GPO/Jovian/docs | D2, D4, D9, D10, N2~N4, NEW-5 |

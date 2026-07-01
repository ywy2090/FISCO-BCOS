# bcos-evm/opstack vs op-geth Parity 校验报告（Sub-agent Round 1）

**日期：** 2026-07-01  
**Round 2 复核：** [2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md](./2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md)（独立重读源码，不采信本报告结论）  
**基线报告：** [2026-07-01-opstack-vs-op-geth-parity-phase0-2.md](./2026-07-01-opstack-vs-op-geth-parity-phase0-2.md)  
**op-geth：** `v1.101702.2` @ `e8800cffe`  
**校验方式：** 4 个 explore sub-agent 并行（deposit / 7702 / fee / fork）

---

## Round 2 复核摘要（2026-07-01）

4 个 sub-agent **独立重验**（不采信 Round 1）：

| 结论 | 说明 |
|------|------|
| **无 REFUTED** | Round 1 全部 finding 维持或细化 |
| **D3 维持 ✅** | 7702 REVERT 与 geth 一致，非误降级 |
| **D8 表述修正** | geth 顺序为 buyGas→intrinsic检查→floor→扣减 |
| **新增 NEW-1~5** | deposit/system tx 细节、buyGas 失败入块 |
| **N3/N4 升为文档债** | capability-matrix / ADR-014 与代码矛盾 |

详见 [Round 2 报告](./2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md)。

---

## 校验结论总表（Round 1）

| ID | Phase 0–2 初判 | 校验结论 | 最终等级 | 说明 |
|----|----------------|----------|----------|------|
| D1 | 📋 Bedrock/Ecotone 不支持 | ✅ 确认偏离 | **📋** | pre-Fjord throw；首 Ecotone Bedrock fallback 亦缺失 |
| D2 | 📋 GPO dispatch 差异 | ✅ 确认 | **📋** | geth 无 Go dispatch；bcos 有 C++ dispatch；结算路径无影响 |
| D3 | 🟡 缺少 vmerr 归一化 | **降级** | **✅** | OpStack 与 geth 一致（7702 REVERT → failed receipt）；eth 路径才有 ADR-015 归一化 |
| D4 | 🟡 历史 fork 覆盖 | ✅ 确认 | **📋** | ADR-014 有意范围；非未知缺口 |
| D5 | ✅ buyGas 一致 | ✅ 确认 | **✅** | Isthmus+ 公式与 magic numbers 对齐 |
| D6 | ✅ 7702 授权应用 | （未单独重审） | **✅** | 维持初判 |
| D7 | ✅ deposit 失败入块 | ✅ 确认 | **✅** | Regolith+ 语义对齐；见 deposit 专节 |
| D8 | 🟡 floor gas 时机 | 🟡 部分确认 | **🟡** | buyGas→floor→intrinsic 顺序一致；错误码不等价 |
| D9 | 🔵 模块拆分 | （未单独重审） | **🔵** | 维持初判 |
| D10 | 🟡 Jovian/superchain | ⚠️ 拆分 | **🟡** | operator/L1 attrs ✅；DA footprint/receipt ❌ |

**更新后合并判定：** ✅ **Isthmus+ 执行语义 parity 基本成立**；残留 🟡 主要为错误码映射、Jovian DA footprint、测试缺口。

---

## 1. Deposit 路径校验（D7）

**Agent：** deposit-validator  
**范围：** Isthmus+ / Regolith+ deposit

| 子项 | Verdict | 测试覆盖 |
|------|---------|----------|
| Mint 时机（snapshot/checkpoint 前） | ✅ | `DepositMintTest`, `DepositNoFeeRoutingTest` |
| 失败 revert + nonce bump + gasUsed | ✅ | `OpStackDepositSettlementTest`, `L1AttributesDepositFailureTest` |
| 无 L1/operator fee 路由 | ✅ | `DepositNoFeeRoutingTest` |
| DepositNonce（执行前 nonce） | ✅ | `DepositMintTest`, `DepositNoFeeRoutingTest` |
| System tx 拒绝 | 🟡 错误码差异 | `DepositTxPreCheckTest`（Malformed vs ErrSystemTxNotSupported） |
| Pre-Regolith gas 报告 | 📋 不支持 | 有意（Isthmus+ 基线） |

**残留缺口（非 D7 阻断）：**
- `DepositReceiptVersion`（Canyon+）未写入 `OpStackReceiptMeta`
- TE E2E 对失败 deposit 的 receipt 断言深度不足

**锚点：**
- geth: `state_transition.go:474-510, 627-688`；`state_processor.go:217-226`
- bcos: `ApplyOpStackMessage.cpp:85-119`；`OpStackTxFinalize.cpp:87-131`

---

## 2. EIP-7702 REVERT 校验（D3）

**Agent：** eip7702-validator  
**结论：初判 🟡 → 降级 ✅**

### 三方对比

| 维度 | op-geth | bcos-opstack | bcos-eth |
|------|---------|--------------|----------|
| 授权应用时机 | Call 前 | Call 前（`apply7702TxAuthorizationsIfNeeded`） | 同 kernel |
| REVERT 后授权 | ✅ 持久化 | ✅ 持久化 | ✅ 持久化 |
| Receipt status | **Failed (0)** | **Failed (RevertInstruction)** | **Success**（ADR-015 有意） |
| vmerr 归一化 | 无 | 无 | `normalizeSetCodeTransactionVmerr` |

**重要纠正：** OpStack **不应**移植 eth 的 `normalizeSetCodeTransactionVmerr`——会导致与 op-geth receipt 分叉。

**测试缺口：**
1. OpStack E2E：7702 + reverting callee → receipt failed + delegation 仍在 state
2. OpStack EEST manifest 增加 `self_sponsored_set_code` revert 变体

**锚点：**
- geth: `state_transition.go:604-622, 739-744`；`state_processor.go:206-210`
- bcos: `OpStackStateTransitionErrorPolicy.h`；`OpStackTransactionExecutorImpl.h:246,179-185`

---

## 3. 费用公式校验（D1 / D5 / D8）

**Agent：** fee-formula-validator

### D1 — pre-Fjord L1

| | |
|--|--|
| geth | Bedrock / Ecotone / 首 Ecotone fallback |
| bcos | `OpStackFeeParams.cpp:55` throw |
| 结论 | **📋 确认** ADR-014 有意偏离 |

### D5 — buyGas

| | |
|--|--|
| 结论 | **✅ 确认** — L1 + operator + blob + 1559 caps 逐项对齐 |
| 测试 | `OpStackPreDebitCharacterizationTest`, `GasFeeCapBalanceTest`, `BlobGasBalanceTest` |

**新发现：** buyGas 失败错误码 geth `ErrInsufficientFunds` vs bcos `NotEnoughCash`（TE 映射层差异，非金额差异）。

### D8 — Floor gas

| | |
|--|--|
| 顺序 | 两边均为 **buyGas → floor check → intrinsic**（初判「geth intrinsic 前」表述不够精确） |
| 语义 | **等价** — floor 失败时 bcos `abortNormalAfterBuyGas` revert buyGas 扣款 |
| 错误码 | **不等价** — geth `ErrFloorDataGas` vs bcos `OutOfGasLimit` |
| 结论 | **🟡** 语义等价、错误码不对齐 |

### Fjord L1 + Operator fee

| | |
|--|--|
| Fjord 公式 | **✅** 常量与 `NewL1CostFuncFjord` 逐行对齐 |
| Operator Isthmus/Jovian | **✅** slot 布局、×100、refund、vault 对齐 |
| 测试 | `OpStackFeeTest`, `OpStackPostSettlementCharacterizationTest` |

---

## 4. Fork / Jovian / GPO 校验（D2 / D4 / D10）

**Agent：** fork-jovian-validator

### D2 — GasPriceOracle

- geth：执行路径无 `0x4200…000F` dispatch，直接 `NewL1CostFunc` 读 L1Block
- bcos：`GasPriceOraclePredeploy::dispatch` 响应链上 CALL
- **📋 确认**，结算无影响

### D4 — 历史 fork

geth 有、bcos 无独立门控：Bedrock, Ecotone, Regolith, Canyon, Granite, Holocene, Karst, Interop  
bcos 有：Fjord, Isthmus, Jovian（部分）

**建议：** 产品范围审计降为 **📋**；主网历史 replay 维持 **🟡**

### D10 — 建议拆分为三条

| 子项 | 状态 |
|------|------|
| D10a Jovian operator/L1 attrs/GPO | ✅ `operatorCostJovian`, `parseJovianL1Attributes` 已对齐 |
| D10b DA footprint / receipt / 出块 | ❌ geth `deriveOPStackFields` + `CalcDAFootprint`；bcos 未实现 |
| D10c superchain vs TE 硬编码 | 🟡 `makeIsthmusPlusForkSchedule()` 默认；ADR-014 Phase 2 待做 |

---

## 5. 新发现（初稿未列）

| # | 发现 | 等级 |
|---|------|------|
| N1 | `DepositReceiptVersion`（Canyon+）缺失 | 🟡 |
| N2 | Jovian DA footprint 块级限制 + receipt 字段缺失 | 🟡 |
| N3 | `capability-matrix.md` 写「no GPO」与实现矛盾 | 📋 文档 |
| N4 | ADR-014 写「Jovian extension only」已过时 | 📋 文档 |
| N5 | buyGas/floor 失败 TE 错误码与 geth 枚举不对齐 | 🟡 |
| N6 | 7702 REVERT OpStack 测试缺口 | 🟡 |

---

## 6. 建议后续行动

### 无需修复（parity 正确）
- D3：保持 OpStack 无 `normalizeSetCodeTransactionVmerr`
- D1/D4：维持 ADR-014 产品范围

### 测试补强（优先）
1. OpStack 7702 REVERT + receipt status + state 持久化 E2E
2. Jovian DA footprint（待实现后）

### 文档同步
1. 更新 Phase 0–2 报告 D3 等级
2. 修正 `bcos-evm-vs-geth-comparison.md` 关于 7702 REVERT receipt 的描述
3. 更新 `capability-matrix.md` GPO 条目
4. 更新 ADR-014 Jovian 范围说明

### Phase 3–7 剩余
- Phase 4 数值：已完成（Fjord/operator ✅）
- Phase 6 Jovian DA footprint：待实现对照
- Phase 7 EEST opstack manifest smoke

---

## 校验 Agent 索引

| Agent | 任务 | ID |
|-------|------|-----|
| deposit-validator | Deposit 全路径 | D7 |
| eip7702-validator | 7702 REVERT receipt | D3 |
| fee-formula-validator | L1/buyGas/floor | D1, D5, D8 |
| fork-jovian-validator | Fork/GPO/Jovian | D2, D4, D10 |

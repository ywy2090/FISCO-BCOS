# bcos-evm/opstack vs op-geth Parity 报告（Phase 0–2）

**状态：** Phase 0–2 初稿 + Round 1 & Round 2 sub-agent 校验完成  
**最新复核：** [Round 2 独立重验](./2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md)  
**Round 1 校验：** [validation 报告](./2026-07-01-opstack-vs-op-geth-parity-validation.md)  
**日期：** 2026-07-01  
**范围：** Isthmus+ 产品基线（`makeIsthmusPlusForkSchedule` + `makeIsthmusRevisionConfig`）

---

## 元信息

| 项 | 值 |
|----|-----|
| op-geth | `v1.101702.2` @ `e8800cffe` |
| op-geth 路径 | `/Users/octopus/octo/code/blockchain-impl/op-geth` |
| bcos worktree | `feat-evm-refactor` |
| bcos opstack | `bcos-evm/opstack/` |
| bcos eth 内核 | `bcos-evm/eth/`（opstack 复用） |
| 审计方法 | Phase 0–2：符号映射 + normal tx 逐步对照 + 前 10 条差异 |

---

## 执行摘要

bcos `opstack/` 在架构上与 op-geth **语义对齐但组织方式不同**：geth 把 OP 逻辑集中在 `state_transition.go`；bcos 拆为 `applyOpStackMessage` → Hooks → FeeSettlement → `stateTransitionExecute`（复用 eth 内核）。

在 **Isthmus+ 基线**下，L1 Fjord 公式、operator fee、deposit 失败入块、7702 授权应用等主路径与 geth **大体一致**。已确认差异包括：Bedrock/Ecotone 历史 L1 公式有意不支持、GasPriceOracle 有 C++ dispatch 而 geth 执行路径不调用、OpStack 错误策略缺少 eth 侧的 vmerr 归一化钩子、以及模块拆分导致的 buyGas / floor gas 时序差异（语义上通过 checkpoint/revert 兜底）。

**Phase 0–2 合并判定：** ⚠️ 条件 parity — 无明确 🔴 状态根级 bug，若干 🟡 待 Phase 3–7 深挖。

**校验后合并判定（Round 2，2026-07-01）：** ✅ **Isthmus+ 执行语义 parity 基本成立** — 两轮 8 个 sub-agent 独立重验，**无 🔴 阻断**；无原 finding 被 REFUTED。残留：N1/N2 Jovian receipt、N6 测试缺口、文档债（N3/N4）。

---

## Phase 0 — 基线

### 架构对照

```text
op-geth:  StateProcessor → ApplyTransactionWithEVM → ApplyMessage → stateTransition.execute
bcos:     OpStackTransactionExecutorImpl → applyOpStackMessage → stateTransitionExecute → innerExecute
```

### 设计约束（ADR-014）

- `RevisionConfig`（EVM/EIP）与 `OpStackForkSchedule`（Fjord/Isthmus/Jovian 费用）**正交**
- 产品基线：Fjord+Isthmus 在 blockTime=0 即激活
- Bedrock/Ecotone **故意不支持**（ADR-014 Phase 1）
- L1 pre-Fjord：bcos `throw`；op-geth 回退 Bedrock 公式

### op-geth 重点文件

| 文件 | 职责 |
|------|------|
| `core/state_transition.go` | ApplyMessage, preCheck, buyGas, execute, applyAuthorization |
| `core/state_processor.go` | ApplyTransactionWithEVM, MakeReceipt |
| `core/evm.go` | NewEVMBlockContext |
| `core/types/rollup_cost.go` | L1/operator cost, L1Block slots |
| `core/types/deposit_tx.go` | Deposit tx 类型 |
| `params/config.go`, `config_op.go` | Fork schedule |

### bcos 重点文件

| 文件 | 职责 |
|------|------|
| `opstack/apply/ApplyOpStackMessage.*` | 链入口 |
| `opstack/apply/OpStackStateTransitionHooks.*` | OP 预检与 pipeline 调优 |
| `opstack/settlement/*` | buyGas / refund / finalize |
| `opstack/fee/*` | L1 cost、floor gas、plan |
| `opstack/adapter/OpStackChainCallTargetAdapter.*` | L1Block/GasPriceOracle |
| `eth/eip/Eip7702.*` | 7702 共享实现 |

---

## Phase 1 — 符号映射总表（40 行）

| # | 关注点 | op-geth | bcos-evm/opstack |
|---|--------|---------|------------------|
| 1 | 链级单笔入口 | `core.ApplyMessage` | `applyOpStackMessage` |
| 2 | 请求 DTO | `core.Message` | `OpStackMessageRequest` |
| 3 | 结果 DTO | `core.ExecutionResult` | `OpStackMessageResult` |
| 4 | 块内 tx 应用 | `ApplyTransactionWithEVM` | TE 循环 `applyOpStackMessage` |
| 5 | 块级处理 | `StateProcessor.Process` | `BaselineScheduler` + TE |
| 6 | 状态机内核 | `stateTransition.innerExecute` | `stateTransitionExecute` → `innerExecute` |
| 7 | OP 策略绑定 | 内联 `state_transition.go` | `OpStackStateTransitionBindings::bind` |
| 8 | OP 生命周期钩子 | `preCheck` / `execute` 分支 | `OpStackStateTransitionHooks` |
| 9 | OP 错误策略 | `execute()` deposit 失败包装 | `OpStackStateTransitionErrorPolicy` |
| 10 | EVM 块上下文 | `NewEVMBlockContext` | `wireExecutionEnvironment` + `chainAdapter` |
| 11 | L1CostFunc 注入 | `types.NewL1CostFunc` → `vm.BlockContext` | `wireL1CostFuncWithState` |
| 12 | OperatorCostFunc | `types.NewOperatorCostFunc` | `wireOperatorCostFuncWithState` |
| 13 | buyGas / 预扣费 | `stateTransition.buyGas` | `OpStackFeeSettlement::buyGas` |
| 14 | 结算 / 分账 | `execute` 末尾 + fee recipients | `refundGas` + `OpStackTxFinalize` |
| 15 | Operator 多扣退还 | `refundIsthmusOperatorCost` | `planOpStackPostSettlement` senderOperatorRefund |
| 16 | RollupCostData | `types.RollupCostData` | `RollupCostData` |
| 17 | Fjord L1 cost | `NewL1CostFuncFjord` | `l1CostFjord` |
| 18 | Ecotone/Bedrock L1 | `newL1CostFuncEcotone/Bedrock` | **throw**（ADR-014） |
| 19 | L1 参数加载 | `statedb.GetState(L1BlockAddr, slot)` | `loadOpStackFeeParams` |
| 20 | L1Block predeploy | genesis Solidity + 读槽 | `L1BlockPredeploy::dispatch` |
| 21 | GasPriceOracle | genesis 字节码（执行路径不 dispatch） | `GasPriceOraclePredeploy::dispatch` |
| 22 | L1 attributes 解析 | `extractL1GasParams*` | `parseIsthmus/JovianL1Attributes` |
| 23 | Deposit 类型 | `DepositTxType` (0x7E) | `DEPOSIT_TX_TYPE` / `OpStackDepositTx` |
| 24 | Deposit 识别 | `Transaction.IsDepositTx` | `isDepositTx()` |
| 25 | Deposit mint | `execute()` 开头 `AddBalance` | `applyOpStackMessage` mint 分支 |
| 26 | Deposit 失败入块 | `execute()` revert+nonce++ | `finalizeDeposit` revert+nonce++ |
| 27 | System tx 拒绝 | `ErrSystemTxNotSupported` (Regolith+) | `lifecycleCheckEntryRules` Malformed |
| 28 | Deposit receipt | `MakeReceipt` DepositNonce | `OpStackReceiptMeta.depositNonce` |
| 29 | Gas pool | `GasPool.SubGas/ReturnGas` | `gasPoolSubGasHook/ReturnGasHook` |
| 30 | Floor data gas 预检 | `innerExecute` `FloorDataGas` vs `GasLimit` | `opStackFloorGasPrecheck` |
| 31 | Intrinsic gas | `IntrinsicGas(..., authList, ...)` | `deductIntrinsicGas(OpStackEntry)` |
| 32 | EIP-1559 预检 | `preCheck` fee cap vs baseFee | `lifecycleCheckEntryRules` |
| 33 | EIP-4844 blob 预检 | `preCheck` blob hashes/cap | `lifecycleCheckEntryRules` + blob checks |
| 34 | EIP-7702 预检 | `preCheck` sender EOA / auth+CREATE | `lifecycleCheckEntryRules` |
| 35 | EIP-7702 应用 | `applyAuthorization` | `applyAuthorizations` (eth/eip) |
| 36 | 7702 delegation 解析 | `ParseDelegation` + `evm.Call` | `parseDelegationTarget` + `loadFrameBytecode` |
| 37 | Fork schedule | `ChainConfig.Fjord/Isthmus/JovianTime` | `OpStackForkSchedule` |
| 38 | EVM revision | `Rules` → Prague/Osaka | `makeIsthmusRevisionConfig()` → Prague |
| 39 | Fee recipients | `Optimism*FeeRecipient` | `OP_*_FEE_RECIPIENT` |
| 40 | Receipt L1 元数据 | `Receipts.deriveOPStackFields`（块级） | `OpStackReceiptMeta`（单笔） |

---

## Phase 2 — Normal Tx 逐步对照

| 步骤 | op-geth | bcos opstack | 对齐？ |
|------|---------|--------------|--------|
| 1. 入口 | `ApplyMessage` → `execute` | `applyOpStackMessage` | ✅ |
| 2. 入口规则 | `preCheck` | `lifecycleCheckEntryRules`（buyGas 前） | ✅ 规则同，时序略异 |
| 3. Gas pool | `gp.SubGas(GasLimit)` | `gasPoolSubGasHook` | ✅ |
| 4. buyGas | L1+operator+blob 余额检查+扣款 | `OpStackFeeSettlement::buyGas` | ✅ Isthmus+ |
| 5. Intrinsic gas | `IntrinsicGas` | `deductIntrinsicGas(OpStackEntry)` | ✅ |
| 6. Floor gas | `GasLimit < floorDataGas` | `opStackFloorGasPrecheck`（buyGas 后） | 🟡 revert 兜底 |
| 7. 7702 apply | `applyAuthorization` | `applyAuthorizations` | ✅ |
| 8. EVM 执行 | `evm.Call/Create` | `runCallFrame` + evmone | ✅ |
| 9. Refund+floor | `calcRefund` + EIP-7623 | `postExecuteGasSettlement` | ✅ |
| 10. Fee 分账 | baseFee/L1/operator/coinbase | `refundGas` | ✅ |
| 11. Operator 退还 | `refundIsthmusOperatorCost` | `senderOperatorRefund` | ✅ |

---

## 差异清单（Phase 0–2）

### D1 — 📋 Bedrock/Ecotone L1 公式不支持

| | |
|--|--|
| **geth** | `rollup_cost.go` Bedrock/Ecotone；pre-Fjord 可用 |
| **bcos** | `OpStackFeeParams.cpp:55` pre-Fjord `throw` |
| **ADR** | ADR-014 有意偏离 |

### D2 — 📋 GasPriceOracle dispatch 差异

| | |
|--|--|
| **geth** | 执行路径不经 oracle 合约 dispatch |
| **bcos** | `GasPriceOraclePredeploy::dispatch` |
| **影响** | L1 费结算路径无影响；仅链上 CALL 语义 |

### D3 — ✅ OpStack 7702 REVERT 与 geth 一致（校验后降级）

| | |
|--|--|
| **geth** | REVERT → `ExecutionResult.Err` → receipt **failed**；授权在 Call 前持久化 |
| **bcos eth** | `normalizeSetCodeTransactionVmerr`（REVERT→SUCCESS）— ADR-015 有意偏离 geth |
| **bcos opstack** | 无归一化 → receipt **failed** — **与 geth 对齐** |
| **校验** | 勿向 OpStack 移植 eth 归一化；补 7702 REVERT receipt 测试 |

### D4 — 🟡 历史 OP fork 覆盖

| | |
|--|--|
| **geth** | Regolith/Canyon/Ecotone/Holocene/Jovian 完整 |
| **bcos** | Isthmus+ 基线；历史 replay 不支持 |

### D5 — ✅ buyGas（Isthmus+）

geth `state_transition.go:282-343` ↔ bcos `OpStackFeeSettlement.cpp:23-68`

### D6 — ✅ EIP-7702 授权应用

geth `applyAuthorization` ↔ bcos `Eip7702.cpp` `applyAuthorizations`

### D7 — ✅ Deposit 失败仍入块

geth `execute()` revert+nonce++ ↔ bcos `finalizeDeposit`

### D8 — 🟡 Floor gas 检查时机

geth intrinsic 前 vs bcos buyGas 后（`abortNormalAfterBuyGas` revert）

### D9 — 🔵 buyGas 与 pipeline 模块拆分

geth `preCheck` 内 buyGas vs bcos `NormalTxFeeCoordinator` 外置

### D10 — 🟡 Jovian / superchain 配置

geth `LoadOPStackChainConfig` vs bcos TE 硬编码 Isthmus+

---

## 严重等级汇总

### 初稿（Phase 0–2）

| 等级 | 数量 | ID |
|------|------|-----|
| 🔴 阻断 | 0 | — |
| 🟡 警告 | 4 | D3, D4, D8, D10 |
| ✅ 一致 | 3 | D5, D6, D7 |
| 📋 设计选择 | 2 | D1, D2 |
| 🔵 架构差异 | 1+ | D9, receipt 块级 vs 单笔 |

### 校验后（sub-agent 2026-07-01）

| 等级 | 数量 | ID |
|------|------|-----|
| 🔴 阻断 | 0 | — |
| 🟡 警告 | 2 | D8, D10b（DA footprint） |
| ✅ 一致 | 5 | D3, D5, D6, D7 + 7702 receipt |
| 📋 设计选择 | 4 | D1, D2, D4, D10c |
| 🔵 架构差异 | 1 | D9 |

---

## 后续 Phase 计划

| Phase | 内容 | 校验 agent |
|-------|------|------------|
| 3 | Deposit 全路径 | deposit-validator |
| 4 | L1 cost 数值公式 | fee-formula-validator |
| 5 | EIP-7702 REVERT receipt | eip7702-validator |
| 6 | Jovian / fork schedule | fork-jovian-validator |

---

## 校验记录（sub-agent）

> 完整校验报告：[2026-07-01-opstack-vs-op-geth-parity-validation.md](./2026-07-01-opstack-vs-op-geth-parity-validation.md)

| 差异 ID | 初判 | 校验结论 | Agent / 日期 |
|---------|------|----------|--------------|
| D1 | 📋 | ✅ 确认偏离 | fee-formula / 2026-07-01 |
| D2 | 📋 | ✅ 确认 | fork-jovian / 2026-07-01 |
| D3 | 🟡 | ✅ **降级**（与 geth 一致） | eip7702 / 2026-07-01 |
| D4 | 🟡 | 📋 降为设计选择 | fork-jovian / 2026-07-01 |
| D5 | ✅ | ✅ 确认 | fee-formula / 2026-07-01 |
| D6 | ✅ | 维持 | — |
| D7 | ✅ | ✅ 确认 | deposit / 2026-07-01 |
| D8 | 🟡 | 🟡 语义等价、错误码不等价 | fee-formula / 2026-07-01 |
| D9 | 🔵 | 维持 | — |
| D10 | 🟡 | ⚠️ 拆分（operator ✅ / DA footprint ❌） | fork-jovian / 2026-07-01 |

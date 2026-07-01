# bcos-evm 错误处理 / 错误码 geth·op-geth Parity 报告（v2）

**审查日期：** 2026-06-26  
**v2 修订日期：** 2026-06-26（4 路子代理交叉复核后）  
**审查类型：** 只读审查，不改代码  
**v1 路径：** `docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26.md`  
**审查范围：** `bcos-evm/eth/` + `bcos-evm/opstack/` + `transaction-executor/` 错误链路 vs geth / op-geth  
**排除：** `bcos/` FISCO 扩展语义（仅附录标注有意偏离）

---

## v2 修订摘要（相对 v1）

| 变更 | 说明 |
|------|------|
| GAP-002 | P0 → **P1 inclusion gap**；ADR-025 settlement 侧 **已解决** |
| GAP-003 | P0 → **P1**；主路径不抛 `UnknownEVMCStatus` |
| GAP-001 | 保留 P0/P1；删除「负 gasUsed」表述 → **`gasUsed=gasLimit`** |
| ADR-025 §6 | 「仍 open」→ **已解决（ADR 范围）** |
| 新增 | GAP-009~011、GAP-TE-001~006 |
| §2 | 补充运行时主路径为 `adoptEvmcResult` → `evmcStatusToErrorMessage` |
| §5 | `OpStackEntryFailureAbortNormal` 标为**已有**；补充 ADR-025 测试覆盖 |

---

## Executive Summary

### P0 / P1 — 需关注（共识或参考 parity）

1. **GAP-001**（P0/P1，**CONFIRM**）：ETH 参考路径 entry failure（intrinsic / buyGas）仍产 **receipt** + `OutOfGasLimit(2)`，与 geth **reject tx** 不符。`gas_left=0` → `gasUsed=gasLimit`（非负 gasUsed）。`buyGas` 的 `co_return {}` **不是空 receipt**，Finalize 仍 `makeReceipt()`。
   - **我方:** `EthOrchestrationErrorPolicy.h:14-21`；`EthTransactionExecutorImpl.h:169-171,285,197-199,204-221`；`EthTxFeeLedger.h:69-80,137-169`
   - **GETH:** `state_transition.go:565-567`（intrinsic）；`state_processor.go:176-178`（err → no receipt）

2. **GAP-004**（P1，**CONFIRM**）：嵌套 CALL `InsufficientBalance` 硬编码 `gas_left=0`；geth `evm.Call` 保留 gas。
   - **我方:** `ExecutionFrame.cpp:147-154`
   - **GETH:** `core/vm/evm.go:262-264`

3. **GAP-002 inclusion 部分**（P1，**仍 open**）：OpStack/ETH 仍 **include failed tx**（产 `OutOfGasLimit` receipt）vs op-geth/geth **区块级 reject**。与 ADR-025 **无关**（ADR-025 已修 settlement）。
   - **我方:** `OpStackOrchestrationErrorPolicy.h:11-15`；`OpStackTransactionExecutorImpl.h:230-295`
   - **OPGETH:** `state_transition.go:527-529`；`state_processor.go:109-112`

4. **GAP-TE-002**（P1）：`m_topLevelIncludedTxVmError` 未参与 TE `settleGasUsedFromEvmResult`；plan Task 3 未落地。
   - **我方:** `EthTransactionExecutorImpl.h:204-221,252`；无 `finalizeEthTxGasUsed`

5. **GAP-TE-003**（P1）：top-level `EVMC_INSUFFICIENT_BALANCE` 不 normalize → TE 跳过 `applyStateDiff`，可能丢 partial state（如 7702 auth）。
   - **我方:** `normalizeIncludedTxVmerr.h:19`；`EthTransactionExecutorImpl.h:179-188`

6. **GAP-009**（P1，复核新增）：同 `EVMC_INSUFFICIENT_BALANCE` 对应两个 `TransactionStatus`——precheck `InsufficientFunds(10015)` vs EVM `NotEnoughCash(7)`。
   - **我方:** `EthPrecheckPolicy.cpp:73`；`EVMCResult.cpp:107-108`

7. **GAP-010**（P1，复核新增）：`evmcStatusToTransactionStatus` 与 `evmcStatusToErrorMessage` 双表不一致。
   - **我方:** `EVMCResult.cpp:97-118` vs `:121-163`

### P1 — 参考路径 parity（严重性下调或拆分）

8. **GAP-003**（P1，v1 P0 **DOWNGRADE**）：`onPipelineException` 映射为 `EVMC_INTERNAL_ERROR` + `Unknown`，**不抛** `UnknownEVMCStatus`。与 GAP-005 重叠。
   - **我方:** `EthOrchestrationErrorPolicy.h:36-47`；`adoptEvmcResult.h:13-18`
   - **GETH:** `state_transition.go:550-552`；`vm/errors.go:205`

9. **GAP-005**（P1，API 层 **CONFIRM**；主路径崩溃 **DOWNGRADE**）：主 Tx 管线经 `adoptEvmcResult` → `evmcStatusToErrorMessage` default → `Unknown`，**不崩溃**。`evmcStatusToTransactionStatus` 仍抛异常（次要路径 `VMInstance.cpp:22-23`）。
   - **我方:** `EVMCResult.cpp:116-117,160-162`；`VMInstance.cpp:22-23`
   - **GETH:** `vm/errors.go:205`

10. **GAP-TE-004/005**（P1）：ETH/OpStack entry failure 仍产 failed receipt（设计性 inclusion 差异，Task 8 不宜标 PASS）。

### P2 — 文档/测试缺口

11. **GAP-006**（P2，**CONFIRM**）：`checkBalanceAndValue` 失败强制 `exitKind=GasAffordRejected`，命名误导。
12. **GAP-007**（P2，**DOWNGRADE**）：OpStack precheck 大项已对齐；system tx 为语义差异（`Malformed` vs `ErrSystemTxNotSupported`），已有测试。
13. **GAP-008**（P2，**CONFIRM**）：EIP-3529 cap **已实现**（`TxIntrinsicGas.h:113-119`），**缺 characterization 断言**。
14. **GAP-011**（P2）：`VMInstance` 单参构造可抛 `UnknownEVMCStatus`（次要路径）。
15. **GAP-TE-006**（P2 笔误）：v1 写 `OutOfGasLimit (0)` → 实际 **`OutOfGasLimit = 2`**，`None = 0`。

### 已解决 / Accepted

| 项 | 状态 |
|----|------|
| **ADR-025** phantom fee / settlement abort | **已解决** — `abortNormalAfterBuyGas` + 8 个 lifecycle characterization 用例 |
| OpStack deposit entry failure | **PASS** — 对齐 op-geth `execute():486-510` |
| ADR-015 included-tx vmerr | **已解决** |
| Task 8 `applyStateDiff` / `makeReceipt(status)` | **PASS**（entry failure 语义另见 GAP-TE-004/005） |
| ACCEPTED-001/002 | FISCO 有意偏离（不变） |

---

## 环境

- **GETH commit:** `117e067f0f0bae1a17082321f224dedb6765b10f`
- **OPGETH commit:** `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`
- **WORKSPACE branch:** `worktree-feat-evm-refactor`

---

## §1 Taxonomy 对照总表

| 维度 | geth | op-geth | bcos-evm (ETH) | bcos-evm (OpStack) | bcos-evm (FISCO) |
|------|------|---------|----------------|---------------------|------------------|
| 交易未进入 EVM（precheck/intrinsic/buyGas） | `execute()` → `(nil, err)`, tx **reject** | 同 geth + deposit/system 分支 | early-exit 产 evmcResult + TE **仍 makeReceipt** | 同 + **`abortNormalAfterBuyGas`**（state/fee 已对齐） | 同 early-exit |
| Top-level EVM vmerr 但 tx 仍 included | `(&result, nil)`, `result.Err` 非 nil | 同 + deposit revert | `normalizeIncludedTxVmerr` → SUCCESS | 无 normalize（deposit 除外） | N/A |
| Top-level REVERT | `ErrExecutionReverted`, state 保留 | 同 | `RevertInstruction`, state 保留 | 同 | 同（`fix_revert_logs`） |
| Nested CALL/CREATE 失败 | revert snapshot; 非 REVERT 则 exhaust gas | 同 | `finalizeFrame` revert; REVERT 保留 gas | 同 | 同 |
| INSUFFICIENT_BALANCE | Top: reject; Nested: vmerr, **保留 gas** | 同 | `transferOrFail` → **gas_left=0**（GAP-004） | 同 | 同 |
| OUT_OF_GAS（intrinsic vs execution） | intrinsic → reject; execution → vmerr | 同 | intrinsic → `OutOfGasLimit`; execution → OOG | intrinsic → `OutOfGasLimit` + abort | `OutOfGas` |
| INTERNAL_ERROR / 未映射 | `VMErrorCodeUnknown` fallback | 同 | 主路径 `Unknown` fallback；`evmcStatusToTransactionStatus` 可抛异常（次要） | `makeInternalErrorResult` | `Unknown`/`OutOfGas`（flag） |

**关键差异（v2 澄清）：**
- **Inclusion 模型**：FISCO/我方 TE 可 **include failed tx**（failed receipt）；geth/op-geth **reject** 无效 tx。这是产品层设计差异，但 ETH 参考路径需明确是否接受。
- **ADR-025 已闭合**：OpStack normal entry failure 的 **state 回滚、gasUsed=0、无 phantom l1Fee/operatorFee** 已与 op-geth settlement 对齐；**未闭合**的是 inclusion/receipt 语义（GAP-002）。

---

## §2 Status 映射矩阵

> **v2 说明：** 运行时主路径为 `adoptEvmcResult` → `evmcStatusToErrorMessage`（`adoptEvmcResult.h:13-18`），**不是** `evmcStatusToTransactionStatus`。下表「运行时映射」列反映实际行为。

| evmc_status_code | 运行时 TransactionStatus | geth vm.Error | receipt 语义 | 备注 |
|------------------|--------------------------|---------------|--------------|------|
| `EVMC_SUCCESS` | `None` | `err == nil` | 成功 | 一致 |
| `EVMC_REVERT` | `RevertInstruction` | `ErrExecutionReverted` | 失败 | 一致 |
| `EVMC_OUT_OF_GAS` | `OutOfGas` / `OutOfGasLimit(2)` | `ErrOutOfGas` | 失败 | ErrorPolicy 用 `OutOfGasLimit` |
| `EVMC_INSUFFICIENT_BALANCE` | `NotEnoughCash(7)` / precheck `InsufficientFunds(10015)` | `ErrInsufficientBalance` | 失败 | **GAP-009** 双枚举 |
| `EVMC_STACK_OVERFLOW` | `OutOfStack` | `ErrStackOverflow` | 失败 | 一致 |
| `EVMC_STACK_UNDERFLOW` | `StackUnderflow` | `ErrStackUnderflow` | 失败 | 一致 |
| `EVMC_INVALID_INSTRUCTION` | `BadInstruction` | `ErrInvalidOpCode` | 失败 | 一致 |
| `EVMC_UNDEFINED_INSTRUCTION` | `BadInstruction` | `ErrInvalidOpCode` | 失败 | 一致 |
| `EVMC_BAD_JUMP_DESTINATION` | `BadJumpDestination` | `ErrInvalidJump` | 失败 | **GAP-010**：`evmcStatusToTransactionStatus` 未映射 |
| `EVMC_INVALID_MEMORY_ACCESS` | `StackUnderflow` | evmone 专有 | 失败 | **GAP** 命名不一致 |
| `EVMC_STATIC_MODE_VIOLATION` | `Unknown` | `ErrWriteProtection` | 失败 | **GAP** 应为 WriteProtection 语义 |
| `EVMC_INTERNAL_ERROR` | `Unknown` | `VMErrorCodeUnknown` | 未知 | 一致 |
| 未映射 status | 主路径 → `Unknown`；`evmcStatusToTransactionStatus` → 异常 | `VMErrorCodeUnknown` | 失败 | **GAP-005/011** 次要路径 |

---

## §3 分路径差异详表

### 3.1 ETH Reference (`ethReferenceExecute`)

| 步骤 | 我方 | GETH | 结论 |
|------|------|------|------|
| debitIntrinsicGas | `TxPipeline.cpp:68-80` | `state_transition.go:565-567` | **PASS** 计算 |
| intrinsic failure 出口 | `EthOrchestrationErrorPolicy.h:14-21` | `return nil, ErrIntrinsicGas` | **GAP-001** inclusion |
| normalizeIncludedTxVmerr | `EthOrchestrationErrorPolicy.h:50-57` | top-level vmerr included | **PASS** |
| TE applyStateDiff | `EthTransactionExecutorImpl.h:179-188` | N/A | **PASS**（ADR-015）；见 GAP-TE-003 |
| TE buyGas 失败 | `EthTransactionExecutorImpl.h:169-171` → Finalize 仍 `makeReceipt` | reject | **GAP-001** |
| TE gas 结算 | `settleGasUsedFromEvmResult` 未用 `topLevelIncludedTxVmError` | N/A | **GAP-TE-002** |

**GAP-001 详述（v2 修正）：**

- intrinsic 失败：`gas_left=0` → `gasUsed = gasLimit - 0 = gasLimit`（**全额扣费**，非负值）
- `applyStateDiff` 跳过（门控正确，不会多写 state）
- **仍产 receipt**，`status=OutOfGasLimit(2)` — 与 geth reject 不符

### 3.2 OpStack (`opStackExecute`)

| 路径 | 我方 | OPGETH | Settlement | Inclusion |
|------|------|--------|------------|-----------|
| Normal intrinsic/afford reject | `abortNormalAfterBuyGas` | `innerExecute` err | **PASS** | **GAP-002** |
| Normal buyGas fail | 同上 abort | `buyGas` err | **PASS** | **GAP-002** |
| Deposit entry fail | `finalizeDeposit` nonce+1, gasUsed=limit | `execute():486-510` | **PASS** | **PASS** |
| Deposit gasPool reject | 直接 OOG，无 `settleDeposit` | `ErrGasLimitReached` reject | **PARTIAL** | **GAP** |
| Phantom l1Fee/operatorFee | ADR-025 abort | N/A | **PASS（已解决）** | — |
| Isthmus operator refund | `OpStackPostSettlementCharacterizationTest` | `refundIsthmusOperatorCost:836-846` | 数学 **PASS**；lifecycle E2E 缺口 | — |

**OpStack entry failure 时序（v2 修正调用链）：**

```mermaid
sequenceDiagram
    participant Lifecycle as OpStackTxLifecycle
    participant Fee as OpStackNormalFeeSettlement
    participant Pipeline as TxPipeline
    participant Settle as OpStackSettlement
    participant TE as TE Layer

    Lifecycle->>Fee: buyGas → runTxPipeline
    Pipeline-->>Lifecycle: IntrinsicRejected / GasAffordRejected
    Lifecycle->>Fee: completeAfterPipeline
    Fee->>Settle: abortNormalAfterBuyGas
    Note over Settle: revert buyGas; gasUsed=0; no l1Fee meta
    Lifecycle-->>TE: evmcResult OutOfGasLimit + gasUsed=0
    TE->>TE: skip applyStateDiff; makeReceipt status=2
```

### 3.3 FISCO 附录（有意偏离）

同 v1 Task 9，不与 geth 判 FAIL。

---

## §4 错误路径时序

（ETH included-vmerr 时序同 v1 §4，无变更。）

---

## §5 测试覆盖 gap + 建议 characterization 用例

### 已有测试覆盖（v2 补充）

| 测试文件 | 覆盖范围 |
|----------|----------|
| `OpStackTxLifecycleCharacterizationTest.cpp` | **8 用例**，ADR-023 矩阵 #1–#8；#2/#3/#8 覆盖 ADR-025 abort |
| `OpStackNormalFeeSettlementTest.cpp` | entry reject abort 单元测试 |
| `OpStackPostSettlementCharacterizationTest.cpp` | Isthmus operator refund 数学 |
| `EthIncludedTxVmerrTest.cpp` | top-level invalid normalize；7623 settlement |
| `OrchestrationErrorPolicyTest.cpp` | intrinsic/OOG/exception；INSUFFICIENT_BALANCE 排除（E-PEN-06） |

### 建议新增 / 仍缺

| 优先级 | 用例 | GAP | 状态 |
|--------|------|-----|------|
| P0/P1 | `EthIntrinsicGasFailureRejectBehavior` | GAP-001 | **待新增** — 断言 receipt + gasUsed=gasLimit |
| — | `OpStackEntryFailureAbortNormal` | GAP-002 settlement | **已有**（lifecycle #2/#8/#3） |
| P1 | `NestedCallInsufficientBalanceGasLeft` | GAP-004 | **待新增** |
| P1 | `EvmcStatusMappingCompleteness` | GAP-005/010 | **待新增** |
| P1 | `TopLevelInsufficientBalanceStateDiff` | GAP-TE-003 | **待新增** |
| P2 | `Eip3529RefundCapBoundary` | GAP-008 | **待新增**（实现已有） |
| P2 | `DepositGasPoolRejectVsOpGeth` | §3.2 | **待新增** |

---

## §6 与已有 ADR/Plan 的交叉引用（v2 修正）

| 项 | ADR/Plan | v2 状态 |
|----|----------|---------|
| included-tx vmerr | ADR-015 | **已解决** — normalize 正确；TE gas 结算见 GAP-TE-002 |
| OpStack entry abort / phantom fee | ADR-025 | **已解决（ADR 范围）** — `abortNormalAfterBuyGas` + 测试绿灯 |
| OpStack tx reject vs include | GAP-002 | **仍 open** — inclusion 语义，**非** ADR-025 未交付 |
| ETH error handling plan | `2026-06-23-eth-evm-error-handling-parity.md` | **仍 open** — Task 1–5 大多未落地；**ADR-025 子集已先行落地** |
| 全量 parity | `eth-opstack-geth-parity-review-tasks.md` | 交叉引用 |

### 2026-06-23 plan Task 状态

| Task | 内容 | 状态 |
|------|------|------|
| Task 1 | PrecompileRouter gas 保留 | **未做** |
| Task 2 | nested insufficient balance gas | **未做**（= GAP-004） |
| Task 3 | `finalizeEthTxGasUsed` / TE 消费 flag | **未做**（= GAP-TE-002） |
| Task 5 | `EthTxOutcome` + 无 throw | **未做**（= GAP-005/011） |

---

## §7 逐 Task 补充（v2 增量）

### Task 8: TE 层（v2 修正）

**PASS：**
- `applyStateDiff` 仅 `EVMC_SUCCESS || EVMC_REVERT` — 符合 ADR-015
- `makeReceipt` 用 `evmcResult.status`（非 `status_code`）— 正确

**GAP（v1 Task 8 偏乐观处）：**

| ID | 描述 | 证据 |
|----|------|------|
| GAP-TE-001 | `buyGas` `co_return {}` 只结束 Execute；Finalize 仍 `makeReceipt` | `EthTransactionExecutorImpl.h:169-171,285` |
| GAP-TE-002 | `topLevelIncludedTxVmError` 未参与 gas 结算 | `EthTransactionExecutorImpl.h:204-221,252` |
| GAP-TE-003 | top-level `INSUFFICIENT_BALANCE` 可能丢 stateDiff | `normalizeIncludedTxVmerr.h:19` |
| GAP-TE-004 | ETH entry failure 仍 included + receipt | 同 GAP-001 |
| GAP-TE-005 | OpStack entry failure 仍 failed receipt | ADR-025 修了 settlement，未修 inclusion |
| GAP-TE-006 | `OutOfGasLimit = 2`（v1 误写为 0） | `TransactionStatus` 枚举 |

---

## §8 完整 GAP 索引（v2）

| ID | 严重度 | 简述 | v1→v2 |
|----|--------|------|-------|
| GAP-001 | P0/P1 | ETH entry failure 仍 receipt + gasUsed=gasLimit | CONFIRM，修正 gasUsed 描述 |
| GAP-002 | P1 | inclusion：include failed tx vs reject | DOWNGRADE from P0；ADR-025 已解决 settlement |
| GAP-003 | P1 | 异常路径 vs geth reject | DOWNGRADE from P0 |
| GAP-004 | P1 | nested InsufficientBalance gas_left=0 | CONFIRM |
| GAP-005 | P1 | 双映射表 / 次要路径异常 | DOWNGRADE 崩溃风险 |
| GAP-006 | P2 | exitKind 命名误导 | CONFIRM |
| GAP-007 | P2 | OpStack precheck | DOWNGRADE |
| GAP-008 | P2 | EIP-3529 测试缺口 | CONFIRM（实现已有） |
| GAP-009 | P1 | 双 TransactionStatus 枚举 | **新增** |
| GAP-010 | P1 | 双映射表不一致 | **新增** |
| GAP-011 | P2 | VMInstance 次要路径 | **新增** |
| GAP-TE-001~006 | P1/P2 | TE 层契约 | **新增** |

---

## Appendix：文件索引

同 v1，补充：

| 文件 | 用途 |
|------|------|
| `bcos-evm/opstack/OpStackNormalFeeSettlement.cpp` | ADR-025 `completeAfterPipeline` abort |
| `bcos-evm/opstack/OpStackSettlement.cpp` | `abortNormalAfterBuyGas` |
| `bcos-evm/eth/pipeline/AdoptEvmcResult.h` | 运行时 status 映射入口 |
| `bcos-evm/eth/reference/EthTxFeeLedger.h` | buyGas / makeReceipt |
| `bcos-evm/test/opstack/OpStackPostSettlementCharacterizationTest.cpp` | Isthmus refund |

---

*报告结束。v2 基于 4 路子代理交叉复核。审查员：AI Parity Agent | 状态：只读审查*

# 错误处理 Parity 测试构造报告

**日期：** 2026-06-26  
**来源：** 4 路子 agent 并行执行 `error-handling-geth-parity-test-prompt.md` Task 0–11  
**原则：** characterization only，未改生产代码

---

## Task 0 — 参考索引

| 仓库 | HEAD |
|------|------|
| **GETH** | `117e067f0f0bae1a17082321f224dedb6765b10f` |
| **OPGETH** | `e8800cffe53d459cde8a07c8e8f1de9d86e79e07` |

| 错误域 | GETH 测试 | OPGETH 增量 |
|--------|-----------|-------------|
| Entry reject | `core/state_processor_test.go:136-240` | 同 fork |
| Txpool precheck | `core/txpool/legacypool/legacypool_test.go:423+` | `:435` |
| Nested balance gas | **无单测** → `core/vm/evm.go:262-264` | 同 fork |
| Refund cap | **无单测** → `state_transition.go:778` | 同 fork |
| Deposit gasPool | **无 execute 单测** → `state_transition.go:484-510` | 同段 |

---

## 测试索引

| GAP | 测试文件 | CASE 名 | ctest -R | 状态 |
|-----|----------|---------|----------|------|
| GAP-001/TE-001/004 | `EthIntrinsicGasFailureCharacterizationTest.cpp` | `ethReferenceExecute_intrinsic_gas_too_low_*` 等 4 cases | `EthIntrinsicGasFailure` | **pass** |
| GAP-002/TE-005 | `OpStackTxLifecycleCharacterizationTest.cpp` | `lifecycle_normal_intrinsic_reject_inclusion_failed_receipt_oracle` 等 2 cases | `OpStackTxLifecycleCharacterization` | **pass** (10/10) |
| GAP-004 | `InsufficientBalanceGasLeftTest.cpp` | `NestedCallInsufficientBalanceGasLeft` | `InsufficientBalanceGasLeft` | **pass** |
| Plan-Task1 | `PrecompileRouterEnvelopeTest.cpp` | `precompile_router_insufficient_balance_gas_preservation_characterization` | `PrecompileRouterEnvelope` | **pass** |
| GAP-TE-002 | `EthIncludedTxVmerrTest.cpp` | `TopLevelIncludedTxVmErrorGasSettlement_*` | `EthIncludedTxVmerr` | **fail** (setup: rules_rejected) |
| GAP-TE-003 | `TopLevelInsufficientBalanceStateDiffTest.cpp` | (7702 auth + balance fail) | `TopLevelInsufficientBalanceStateDiff` | **pass** |
| GAP-005/009/010/011 | `EvmcStatusMappingTest.cpp` | table-driven + `vm_instance_single_arg_*` | `EvmcStatusMapping` | **fail** (1/ N: VMInstance adopt output) |
| GAP-003 | `OrchestrationErrorPolicyTest.cpp` | `eth_pipeline_exception_*` 扩展 | `OrchestrationErrorPolicy` | **pass** |
| GAP-008 | `EthTxGasSettlementTest.cpp` | `Eip3529RefundCapBoundary_*` | `EthTxGasSettlementTest` | **需 rebuild TE 目标** |
| Deposit-gasPool | `OpStackDepositGasPoolCharacterizationTest.cpp` | `deposit_gas_pool_reject_*` | `OpStackDepositGasPoolCharacterization` | **fail** (balance oracle) |
| GAP-006/007 | `OrchestrationErrorPolicyTest.cpp` / `OpStackPrecheckPolicyTest.cpp` | 注释/annotation | — | **partial** |

---

## 新建/修改文件

| 操作 | 路径 |
|------|------|
| 新建 | `bcos-evm/test/eth/EthIntrinsicGasFailureCharacterizationTest.cpp` |
| 新建 | `bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp` |
| 新建 | `bcos-evm/test/eth/TopLevelInsufficientBalanceStateDiffTest.cpp` |
| 新建 | `bcos-evm/test/eth/EvmcStatusMappingTest.cpp` |
| 新建 | `bcos-evm/test/opstack/OpStackDepositGasPoolCharacterizationTest.cpp` |
| 修改 | `bcos-evm/test/opstack/OpStackTxLifecycleCharacterizationTest.cpp` |
| 修改 | `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp` |
| 修改 | `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp` |
| 修改 | `bcos-evm/test/eth/OrchestrationErrorPolicyTest.cpp` |
| 修改 | `transaction-executor/tests/EthTxGasSettlementTest.cpp` |
| 修改 | `bcos-evm/test/cmake/EthTests.cmake` |
| 修改 | `bcos-evm/test/cmake/OpStackTests.cmake` |

---

## geth/op-geth 参考缺口（无直接单测）

| 场景 | 主 oracle | 建议 |
|------|-----------|------|
| Nested CALL insufficient balance gas 保留 | `geth/core/vm/evm.go:262-264` | 可选 Go runtime snippet 验证 |
| EIP-3529 refund cap 边界 | `state_transition.go:807-816` | pure function 已测，pipeline 级待 TE rebuild |
| Deposit gasPool reject | `op-geth/state_transition.go:484-510` | 修正 lifecycle setup（sender balance oracle） |
| 7702 + top-level reject | `state_processor_test.go:165-170` | 无 7702 组合单测 |

---

## 运行结果摘要

```bash
# PASS
rtk ctest -R 'EthIntrinsicGasFailure|InsufficientBalanceGasLeft|PrecompileRouterEnvelope|TopLevelInsufficientBalanceStateDiff|OrchestrationErrorPolicy|OpStackTxLifecycleCharacterization'

# FAIL / 待修
rtk ctest -R 'EthIncludedTxVmerr|EvmcStatusMapping|OpStackDepositGasPoolCharacterization'
```

### 失败根因（待 follow-up）

~~1. **EthIncludedTxVmerr** — 新 case 未填 legacy tx fee caps~~ **已修复**（移除 type-2 without caps）
~~2. **EvmcStatusMapping** — VMInstance adopt output 断言~~ **已修复**
~~3. **OpStackDepositGasPool** — balance oracle~~ **已修复**（empty diff 语义）
~~4. **EthTxGasSettlementTest**~~ **已修复**（独立 target + EIP-3529 cap 数学修正）

**2026-06-26 follow-up：** 全部 characterization ctest 绿灯。

---

*报告由 4 路子 agent 产出合并；主 agent 复核 ctest 2026-06-26*

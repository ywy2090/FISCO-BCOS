# bcos-evm vs geth/op-geth 语义对齐审查报告

**日期：** 2026-06-26  
**最后更新：** 2026-06-30 — **15/15 PASS**（Warning-1 核实关闭 + 跑测确认）  
**审查范围：** `bcos-evm/eth/` + `bcos-evm/opstack/`  
**参考版本：** `blockchain-impl/go-ethereum` @ `117e067f0` (v1.17.3 stable)；`blockchain-impl/op-geth` @ `e8800cffe` (feat: core/types post-exec tx encoding)

---

## Executive Summary

- 总 Task 数：15
- **Blocker：0** | **Warning：0** | **Nit：0** | **一致：15**
- 总体结论：**PASS** — Eth 与 OpStack 参考路径执行语义与 geth/op-geth 对齐

Eth 路径与 go-ethereum 高度一致（EIP-2929/3529/7623/7702、DELEGATECALL→预编译、嵌套 CREATE nonce、CanTransfer、SELFDESTRUCT 均已对齐）。OpStack 路径与 op-geth 完全一致。

**关闭历程：**
| 项 | 状态 | 验证 |
|----|------|------|
| Warning-2 嵌套 CREATE nonce bump | ✅ 2026-06-26 修复 | `EvmCallFrameTest` nested_create×3 |
| Task 6 Eth EIP-7623 | ✅ 2026-06-26 核实 | `Eip7623PrecheckTest` / `EthIncludedTxVmerrTest` |
| Warning-1 7702 DELEGATECALL→预编译 | ✅ 2026-06-30 核实 | `EthDelegateCallPrecompileTest`（7 cases）+ `EvmCallFrameTest` |

**2026-06-30 跑测记录（A1）：**
```text
./EthDelegateCallPrecompileTest          → 7/7 PASS
./ExecutionFrameTest --run_test="*nested_create*"  → 4/4 PASS
```
（`ExecutionFrameTest` 已重命名为 `EvmCallFrameTest`；上述二进制为既有 build 产物。）

**不在本报告范围的残余项：** 错误处理轨道 GAP-001（intrinsic/floor 失败 reject vs included-tx）— 见 [error-handling-geth-parity-report-2026-06-26.md](error-handling-geth-parity-report-2026-06-26.md)。

---

## 汇总表

| Task | Concern | 结论 | Blocker | Warning | Nit | 测试覆盖 |
|------|---------|------|---------|---------|-----|----------|
| 1 | EthHost call 递归 | ✅ 一致 | 0 | 0 | 0 | EvmCallFrameTest ✓ |
| 2 | executeMessage 顶层编排 | ✅ 一致 | 0 | 0 | 0 | TxPipeline / innerExecute ✓ |
| 3 | EIP-2929 Access List / Warm Set | ✅ 一致 | 0 | 0 | 0 | WarmTransactionEntry ✓ |
| 4 | Gas Refund (EIP-3529) | ✅ 一致 | 0 | 0 | 0 | SstoreRefund ✓ |
| 5 | EIP-7702 Set Code Auth | ✅ 一致 | 0 | 0 | 0 | EEST / Eip7702 tests ✓ |
| 6 | EIP-7623 FloorDataGas (Eth) | ✅ 一致 | 0 | 0 | 0 | Eip7623Precheck / EthIncludedTxVmerr ✓ |
| 7 | Value Transfer / CanTransfer | ✅ 一致 | 0 | 0 | 0 | FrameValueTransfer ✓ |
| 8 | BLOCKHASH / Precompile / SELFDESTRUCT | ✅ 一致 | 0 | 0 | 0 | BlockHashHost / EthDelegateCallPrecompile ✓ |
| 9 | RollupCost / Fjord L1 Fee | ✅ 一致 | 0 | 0 | 0 | OpStackFeeTest ✓ |
| 10 | Isthmus Operator Fee + Refund | ✅ 一致 | 0 | 0 | 0 | OpStackPostSettlement ✓ |
| 11 | buyGas / Fee Routing / Settlement | ✅ 一致 | 0 | 0 | 0 | OpStackSettlement ✓ |
| 12 | EIP-7623 FloorDataGas (OpStack) | ✅ 一致 | 0 | 0 | 0 | OpStackFloorGas ✓ |
| 13 | Deposit Tx (0x7E) | ✅ 一致 | 0 | 0 | 0 | DepositTxPreCheck ✓ |
| 14 | L1Block Storage + Getter | ✅ 一致 | 0 | 0 | 0 | L1BlockPredeployTest ✓ |
| 15 | OpStack 叠加层 | ✅ 一致 | 0 | 0 | 0 | Eip7702/EvmoneRefund ✓ |

---

## Blocker 详情

**无。**

---

## Warning 详情

**无开放 Warning。** 历史项均已关闭，见下。

### ~~[Warning-1] EIP-7702 委托 + DELEGATECALL→预编译门控偏差~~ — ✅ 已关闭

- **Task：** 1 (#2) / 8 (#4)
- **状态：** **已关闭**（2026-06-30 跑测核实）
- **原偏差：** `EVMC_DELEGATED` + `DELEGATECALL` 直接指向预编译地址时跳过预编译分发。
- **当前实现：** `EvmCallFrame.cpp` 的 `tryCallTargetDispatch` **不再**对 `EVMC_DELEGATED` 做 early-return；经 `resolveCallTarget` 正常 dispatch 预编译 envelope。
- **语义矩阵（均已测）：**

| 场景 | 期望（geth） | 测试 |
|------|-------------|------|
| 普通 `DELEGATECALL`→0x04 | dispatch 预编译 | `EthDelegateCallPrecompileTest::nested_delegatecall_identity_hits_precompile_envelope` |
| `EVMC_DELEGATED` + `DELEGATECALL`→0x04 | dispatch 预编译 | `EthDelegateCallPrecompileTest::delegated7702_delegatecall_direct_precompile_with_evmc_delegated_flag` |
| `DELEGATECALL`→7702 authority（指向预编译） | 空代码，不 dispatch | `EthDelegateCallPrecompileTest::delegated7702_delegatecall_to_authority_runs_empty_code_not_precompile` |
| FISCO `allowDelegateCallToPrecompile=false` | `PolicyRejected` | `EthDelegateCallPrecompileTest::fisco_policy_rejects_*` |

- **有意偏离：** FISCO 路径拒绝 DELEGATECALL→预编译（产品策略，非 geth parity 问题）。

### ~~[Warning-2] CREATE sender nonce 递增时机偏差~~ — ✅ 已关闭

- **状态：** **已修复**（2026-06-26）
- **修复：** `bumpNestedCreateSenderNonce` 在 `bindCreateForInit` 之后、`checkpointFrame` 之前（`EvmCallFrame.cpp`）
- **测试：** `EvmCallFrameTest::{nested_create_failed_still_increments_sender_nonce,nested_create_sequential_assigns_distinct_addresses,nested_create_reentrant_address_derivation_sees_pre_checkpoint_bump}`

---

## Task 6 核实结论 — ✅ 已关闭

（内容同 2026-06-26 核实，略。）

Eth EIP-7623 公式、准入 `max(intrinsic, floor)`、post-refund floor uplift 与 geth Prague 一致。实现位于 `TxPipeline` → `IntrinsicGasDebit.h` / `TxIntrinsicGas.h`。

残余 **GAP-001**（失败时 reject vs included）属错误处理轨道，非 7623 数学 parity。

---

## 测试缺口清单

| geth/op-geth fixture | bcos-evm 对应 test | 状态 |
|----------------------|-------------------|------|
| Fjord empty tx 3,203,000 | `OpStackFeeTest::FjordL1_emptyTx_matches3203000` | ✅ |
| Isthmus L1 setter→getter | `L1BlockPredeployTest::getters_return_slot_values_after_setter` | ✅ |
| Eth DELEGATECALL→预编译 | `EthDelegateCallPrecompileTest`（7 cases） | ✅ |
| 7702 DELEGATECALL 语义矩阵 | 同上 + `EvmCallFrameTest::nested_7702_delegatecall_direct_precompile_hits_envelope` | ✅ |
| Nested CREATE nonce | `EvmCallFrameTest` nested_create×3 | ✅ |
| DELEGATECALL 0x01–0x11 全矩阵 | 部分（0x04 已覆盖） | △ P2 可选 |
| `stCall_emptyAccount.json` | EEST/fixture 间接 | △ P3 可选 |

---

## 已确认一致的关键语义域

### Eth 路径
1. EIP-2929 warm set
2. EIP-3529 gas refund
3. EIP-7623 floor data gas
4. EIP-7702 authorization + delegation code resolution
5. **DELEGATECALL→预编译**（含 `EVMC_DELEGATED` 直接调预编译地址）
6. CanTransfer / Value Transfer
7. 嵌套 CREATE nonce（checkpoint 前 bump）
8. BLOCKHASH / SELFDESTRUCT (EIP-6780)
9. TxPipeline / `innerExecute` 编排

### OpStack 路径
（Fjord / Isthmus / fee routing / deposit / L1Block / overlay — 全部对齐，见初版 §OpStack。）

---

## 建议后续动作

1. ~~**[P1] 7702 DELEGATECALL→预编译**~~ — ✅ 已关闭
2. ~~**嵌套 CREATE / Fjord / L1Block / ExecuteViaEth**~~ — ✅ 已完成
3. **[P0] 错误处理 GAP-001/002/003** — intrinsic/floor 失败 reject vs included-tx（[error-handling 报告](error-handling-geth-parity-report-2026-06-26.md)）
4. **[P0] EvmcStatusMap 统一** — vm-domain-evmc-only-design P0
5. **[P2] DELEGATECALL 0x01–0x11 全矩阵** — 可选
6. **[持续] CI** — `check-opstack-no-prague-post-execution.sh` 已接入 `capability-gate.yml`

---

## 审查元数据

- **初始审查：** 2026-06-26，双 agent 并行（Eth 1–8 + OpStack 9–15）
- **2026-06-26 更新：** Task 6 核实、Warning-2 修复
- **2026-06-30 更新：** A1 跑测、`EthDelegateCallPrecompileTest` 7/7 + nested_create 4/4 → **15/15 PASS**
- **实现命名（refactor）：** `ExecutionFrame`→`EvmCallFrame`；`EvmHostHooks`→`EvmHostHooks`；`executeMessage`→`innerExecute` / `TxExecutionRunner`

# OPStack Isthmus 复审计报告 — Wave 3（严格 op-geth 对照）

**日期：** 2026-06-21  
**FB commit：** `52dda0921` — `feat(opstack): close Isthmus Wave 2 TE baseline`  
**op-geth：** v1.101702.2 @ `e8800cffe`（`/Users/octopus/octo/code/blockchain-impl/op-geth`）  
**optimism/specs：** `689a96f6d3aad7cf7b26525e2d7e0b5d581ae057`  
**方法：** 逐域读取 op-geth 源文件 + FB `bcos-evm/opstack/` + `transaction-executor/` 对照；Wave 2 结论作为基线，仅记录**新增**或**状态变更**  
**前置报告：** `2026-06-20-opstack-isthmus-audit.md`（Wave 2 ✅）

---

## Part 0 — 执行摘要

### 主判定

| 阶段 | Commit | 主判定 |
|------|--------|--------|
| Wave 2 sign-off | `54e17a62c` | ✅ 通过 |
| **Wave 3 严格 op-geth 复审计** | **`52dda0921`** | **⚠️ 有条件通过**（Wave 3 P0 **CLOSED** @ P0 Task 9） |

**理由：** Wave 2 闭合的 P0 项（operator wiring、signed RLP L1 cost、deposit nonce/gas、7702 intrinsic、2537/6780 内核）经独立源码对照仍成立；CTest 23/23 + TE fixture 13/13 PASS。**Wave 3 P0（R3-4844-1/2/3、R3-POOL-1）已于 P0 Task 9 闭合**。**R3-DEP-1（CREATE deposit nonce）已闭合** @ `DepositCreateNonceTest`。剩余 🟡 为 orchestration 精度（baseFee 来源、entry 失败 receipt 语义）。无新增 P0 公式/路由回归。

### 可裁决行统计（Wave 3 @ `52dda0921`）

| 指标 | Wave 2 | Wave 3 |
|------|--------|--------|
| 审计行数（可裁决） | ~33 | **~36**（+3 4844 preCheck 子行） |
| ✅ 一致 | ~30 | **~34** |
| 🟡 警告 | ~3 | **~8** |
| 🔴 阻断 | 0 | **0**（4844 preCheck 已闭合；见 P0 Task 9） |
| **主判定** | ✅ | **⚠️ 有条件通过**（P0 closed；P1 🟡 开放） |

### Wave 3 新增发现汇总

| ID | 域 | 严重度 | 描述 | op-geth 锚点 | FB 锚点 |
|----|-----|--------|------|--------------|---------|
| **R3-4844-1** | Task 7 | ~~🔴~~ **CLOSED** | Blob tx 拒绝 CREATE | `state_transition.go:421-422` | `OpStackPreCheck.cpp:82-83`; `OpStackPreCheck4844Test::rejects_blob_create` |
| **R3-4844-2** | Task 7 | ~~🔴~~ **CLOSED** | type 0x03 / blob intent 拒绝空 hash 列表 | `state_transition.go:424-425` | `OpStackPreCheck.cpp:84-85`; `rejects_type03_with_empty_hashes` |
| **R3-4844-3** | Task 7 | ~~🔴~~ **CLOSED** | versioned hash 版本字节 `0x01` | `state_transition.go:430-433` | `Eip4844.h` + `:86-89`; `rejects_invalid_versioned_hash_prefix` |
| **R3-POOL-1** | Task 1/7 | ~~🔴~~ **CLOSED** | 块级 `BlockGasPool`；普通 L2 hook 占/还 pool | `buyGas` `gp.SubGas` | TE `beginBlock`/`endBlock`; `BlockGasPoolTest`, TE `second_transaction_rejected_when_block_gas_exhausted` |
| **R3-DEP-1** | Task 4 | ~~🟡~~ **CLOSED** | CREATE deposit 成功路径 sender nonce：编排层单次 +1（内核 CREATE 不 bump sender）；`DepositCreateNonceTest` 3→4 | `evm.go:530` vs `innerExecute:598-599` | `OpStackExecuteViaHost.cpp:205-206`; `DepositCreateNonceTest` |
| **R3-ORCH-1** | Task 1 | ~~🟡~~ **CLOSED** | `resolveOpStackBaseFee(header)` 读 OPF1 extraData；无 Legacy fallback | `evm.Context.BaseFee` | `OpStackBlockHeaderExtension.h`; `OpStackTxInputBuilder.h` |
| **R3-ORCH-2** | Task 1 | ~~🟡~~ **CLOSED** | `blobBaseFee` 来自 `calcOpStackBlobBaseFee(excess=0)=1`；**不再读 L1Block slot 7** | `evm.Context.BlobBaseFee` | `OpStackBlockHeaderExtension.h`; `BlobGasBalanceTest` 解耦 |
| **R3-ORCH-3** | Task 1/2 | ~~🟡~~ **CLOSED** | `OpStackForkSchedule` + per-block cache factories；Bedrock/Ecotone intentional unsupported（ADR-014） | `rollup_cost.go:157-192` | `OpStackFee.*`, ADR-014 |
| **R3-7623-1** | Task 6 | ~~🟡~~ **CLOSED** | 非 deposit entry 失败 abort（revert buyGas，无 settle/refundGas）；ADR-025 | geth entry 早返回 | `OpStackTxLifecycle.cpp`; characterization #2/#8 |
| **R3-T2-1** | Task 2 | ~~🟡~~ **CLOSED** | deposit `buildRollupCostData` 返回空 struct（对齐 op-geth） | `transaction.go:399-400` | `OpStackTxInputBuilder.h`; `OpStackTxInputBuilderTest` |
| **R3-T3-1** | Task 3 | 🟡 | 无 Jovian `operator-fee-fix`（×100）；Isthmus 范围外 | `rollup_cost.go:271-286` | 未实现 |

### Wave 2 闭合项 — Wave 3 再验证

| Wave 2 项 | Wave 3 状态 | 证据 |
|-----------|------------|------|
| OP-01 `m_isIsthmus` | ✅ 仍闭合 | `OpStackTransactionExecutorImpl.h:210-211`；`libinitializer/Initializer.cpp:339-342` 生产接入 |
| OP-02 signed RLP | ✅ 仍闭合 | `OpStackTxInputBuilder.h:110-128`；`FIX05_signed_rlp_rollup_execute_e2e` |
| OP-03～05 deposit | ✅ CALL + CREATE 路径闭合 | `DepositMintTest` / `DepositCreateNonceTest` nonce 3→4 |
| OP-06 blob buyGas | ✅ 仍闭合 | `OpStackTxExecutor.cpp:67-75` |
| OP-07 7702 intrinsic | ✅ 仍闭合 | `calcAuthTupleIntrinsicGas` = 25000×n |
| FIX-01～07/09～12 | ✅ 仍闭合 | CTest + TE fixture PASS |

---

## Part 1 — 分域对照（严格 op-geth）

### Task 1 — Executor Wiring

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | Isthmus profile + TE→host 链路 | `IsOptimismIsthmus` | `makeIsthmusRevisionConfig` + `opStackExecuteViaHostTx` | ✅ |
| 2 | Operator fee wiring | `OperatorCostFunc` @ buy/refund | `m_isIsthmus` + `operatorCostIsthmus` | ✅ |
| 3 | L1 cost wiring | `L1CostFunc` fork 分支 | `wireL1CostFuncWithState` + Isthmus+ preset | ✅ **CLOSED R3-ORCH-3** |
| 4 | baseFee 来源 | header / EVM context | OPF1 extraData | ✅ **CLOSED R3-ORCH-1** |
| 5 | blobBaseFee 来源 | `evm.Context.BlobBaseFee` | OPF1 + calcOpStackBlobBaseFee | ✅ **CLOSED R3-ORCH-2** |
| 6 | 生产 scheduler 接入 | miner/worker | `libinitializer` `ExecutionPath::OpStack` | ✅ **Wave 3 确认** |

### Task 2 — Fjord L1 Cost

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | 常量 intercept/coef/minTx | `rollup_cost.go:92-96` | `OpStackConstants.h:43-46` | ✅ |
| 2 | `flzCompressLen` | `rollup_cost.go:667-743` | `RollupCost.cpp:12-105` | ✅ |
| 3 | `l1CostFjord` 公式 | `rollup_cost.go:607-627` | `OpStackFee.cpp:36-56` | ✅ FIX-04 105484/2463 |
| 4 | signed RLP 字节源 | `MarshalBinary()` | `encodeWeb3SignedMarshalBinary` | ✅ |
| 5 | deposit rollup 字节 | 空 struct | 空 struct | ✅ **CLOSED R3-T2-1** |
| 6 | `calldataGasUsed` / L1GasUsed | 第二返回值 | 未暴露 | 🟡 已知 intentional |

### Task 3 — Operator Fee (Isthmus)

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | 公式 | `newOperatorCostFuncIsthmus` | `operatorCostIsthmus` | ✅ |
| 2 | buyGas limit / balanceCheck | `state_transition.go:294-308` | `OpStackTxExecutor.cpp:59-82` | ✅ |
| 3 | refund limit−used | `refundIsthmusOperatorCost` | `OpStackTxExecutor.cpp:103-117` | ✅ |
| 4 | vault 路由 | fee recipient | `OP_OPERATOR_FEE_RECIPIENT` | ✅ |
| 5 | receipt scalar/constant | `receipt_opstack.go` | `makeReceipt` + sidecar | ✅ |
| 6 | Jovian fix | `×100` | 无 | 🟡 R3-T3-1 |

### Task 4 — Deposit Tx

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | mint 在 snapshot 前 | `execute:474-480` | `:127-134` | ✅ |
| 2 | 跳过 fee 路由 | `!IsDepositTx` | deposit 分支 | ✅ |
| 3 | Regolith+ actual gas | `innerExecute:681-688` | `postExecuteGasSettlement` | ✅ |
| 4 | REVERT nonce+1 + actual gas | Call 前 bump | revert 后 bump | ✅ CALL |
| 5 | entry 失败 gasLimit + nonce+1 | `execute:486-510` | `:135-144` | ✅ |
| 6 | depositNonce 捕获 | EVM 前 | mint 前 `:126` | ✅ |
| 7 | CREATE 成功 nonce | `evm.Create` 内 +1 | 编排层成功后 +1（无双重 bump） | ✅ **CLOSED R3-DEP-1** |

### Task 5 — L1Block + L1 Attributes

| # | 能力 | op-geth / Solidity | FB | 状态 |
|---|------|-------------------|-----|------|
| 1 | 地址 `0x420…0015` | `rollup_cost.go:68` | `OpStackConstants.h:22-24` | ✅ |
| 2 | Isthmus selector `0x098999be` | `rollup_cost.go:63` | `L1BlockSelectors.h:7` | ✅ |
| 3 | 176B calldata 布局 | `extractL1GasParamsPostIsthmus` | `L1BlockStorage.cpp:47-66` | ✅ |
| 4 | setter/getter | `L1Block.sol` | `L1BlockPredeploy.cpp` | ✅ |
| 5 | native dispatch | EVM Solidity | `OpHostExtension` deviation | 🟡 已知 |
| 6 | L1 attributes TE E2E | deposit → slots | `l1_attributes_deposit_via_te` | ✅ |
| 7 | 失败 deposit nonce/gas | Regolith+ | `L1AttributesDepositFailureTest` | ✅ **Wave 3 更正 Wave 2 笔记** |

### Task 6 — EIP-7623 Floor + Receipt

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | `FloorDataGas` 公式 | `state_transition.go:120-133` | `OpStackFloorGas.cpp:32-41` | ✅ |
| 2 | entry floor vs gasLimit | `IsPrague` + `:552` | `executeEntryFloorDataGasCheck` | ✅ |
| 3 | post-exec floor bump | `:650-661` | `OpStackPostExecuteGas.h:35-40` | ✅ |
| 4 | operator 基于 post-floor gasUsed | `:731` | settlement 后 operator | ✅ |
| 5 | entry 失败路径 | 早返回无 refund | abort + revert buyGas（ADR-025） | ✅ **CLOSED R3-7623-1** |
| 6 | floor E2E receipt | — | 仍缺 | 🟡 |

### Task 7 — 7702 + 4844

| # | 能力 | op-geth | FB | 状态 |
|---|------|---------|-----|------|
| 1 | 7702 EOA/delegation precheck | `:385-388` | `OpStackPreCheck.cpp:64-70` | ✅ |
| 2 | 7702 intrinsic 25000×n | `:114-116` | `calcAuthTupleIntrinsicGas` | ✅ |
| 3 | auth+CREATE 拒绝 | `:453-454` | `:89-92` | ✅ |
| 4 | existence refund 12500 | `:787-788` | `Eip7702.cpp:79-81` | ✅ |
| 5 | 4844 eip4844 门控 | `IsCancun` | `revisionConfig.eip4844` | ✅ |
| 6 | blobGasFeeCap >= blobBaseFee | `:444-446` | `:83-86` | ✅ |
| 7 | blob tx CREATE 拒绝 | `:421-422` | `hasBlobTxIntent` + `isCreateKind` | ✅ **CLOSED R3-4844-1** |
| 8 | 空 blob hashes 拒绝 | `:424-425` | empty list on blob intent | ✅ **CLOSED R3-4844-2** |
| 9 | versioned hash 版本 | `:430-433` | `isValidVersionedHash` | ✅ **CLOSED R3-4844-3** |
| 10 | blob buyGas 公式 | `:854-856` | `OpStackTxExecutor.cpp:71` | ✅ |

### Task 8 — Inherited Smoke

| EIP | 状态 | 备注 |
|-----|------|------|
| 2537 MSM | ✅ | `BlsGas.h` + smoke |
| 6780 SELFDESTRUCT | ✅ | `EthHost.cpp` |
| 2929 warm | ✅ | `eip2929=true` |
| 7212 / 7823 on Isthmus | ⚪ | PRAGUE profile，预期 unsupported |

### Task 9 — Unsupported

无 Wave 3 状态变更；BCOS 钩子仍隔离于 OP 路径 ✅。

---

## Part 2 — 偏离项（Wave 3 开放）

| ID | 严重度 | 描述 | 建议 |
|----|--------|------|------|
| ~~R3-4844-1/2/3~~ | ~~🔴~~ **CLOSED** | 4844 preCheck 三项 op-geth 形状校验 | `OpStackPreCheck4844Test` |
| ~~R3-POOL-1~~ | ~~🔴~~ **CLOSED** | 块级 gas pool 占/还 | `BlockGasPoolTest`, TE fixture |
| ~~R3-DEP-1~~ | ~~🟡~~ **CLOSED** | CREATE deposit nonce 时序 | `DepositCreateNonceTest`（零 prod 改动） |
| ~~R3-ORCH-1/2~~ | ~~🟡~~ **CLOSED** | header OPF1 fee 扩展 | `OpStackBlockHeaderExtensionTest`; TE fixture OPF1 header |
| ~~R3-ORCH-3~~ | ~~🟡~~ **CLOSED** | fork schedule + ADR-014 | `OpStackForkScheduleTest`, `OpStackFeeTest`, smoke E2E |
| R3-7623-1 | ~~🟡~~ **CLOSED** | entry 失败 abort（ADR-025）；无 phantom refundGas / receipt fee | `OpStackTxLifecycle.cpp`; characterization #2/#8 |
| R3-T2-1 | ~~🟡~~ **CLOSED** | deposit rollup 字节 | `buildRollupCostData` → `RollupCostData{}` |

**Wave 2 CLOSED 项：** 无 reopen。

---

## Part 3 — CTest 验证（Wave 3 @ `52dda0921`）

| 项 | 命令 | 结果 |
|----|------|------|
| bcos-evm opstack | `ctest -R 'OpStack\|L1Block\|Deposit\|Blob\|7702\|RefundIsthmus\|L1Attributes'` | **23/23 PASS** |
| TE fixture | `ctest -R OpStackTransactionExecutorFixture` | **1/1 PASS**（13 用例） |
| capability matrix | `check-capability-matrix.sh` | 未重跑（Wave 2 OK） |

---

## Part 4 — 后续动作（Wave 3 → Wave 4）

### P0（~~🔴~~ — **CLOSED @ P0 Task 9**）

1. ~~`OpStackPreCheck`：blob 字段非空时拒绝 CREATE~~ ✅ `OpStackPreCheck4844Test::rejects_blob_create`
2. ~~type 0x03 / blob 字段 present 时拒绝 `blobVersionedHashes.empty()`~~ ✅ `rejects_type03_with_empty_hashes`
3. ~~逐 hash 校验 version byte `0x01`（KZG-4844）~~ ✅ `rejects_invalid_versioned_hash_prefix`
4. ~~块级 `BlockGasPool`；普通 L2 占 pool~~ ✅ `BlockGasPoolTest`, `second_transaction_rejected_when_block_gas_exhausted`

### P1（🟡）

1. CREATE deposit nonce E2E 测试 + 必要时修复 bump 时序。
2. `resolveOpStackBaseFee` → block header baseFee。
3. operator fee / L1 fee literal E2E 向量（非 `>0` smoke）。
4. entry 失败路径 receipt vs state 语义文档或对齐。

### 放行条件（Wave 3 → ✅ 全量通过）

- ~~P0 4844 三项补全 + CTest~~ ✅ CLOSED
- ~~P0 block gas pool~~ ✅ CLOSED
- CREATE deposit nonce 用例 PASS
- （可选）baseFee 来源修复

---

**审计状态：** Wave 3 严格 op-geth 复审计完成 @ `52dda0921`；**P0 Task 9 闭合 R3-4844-1/2/3、R3-POOL-1**。主判定 **⚠️ 有条件通过**（P0 closed；P1 🟡 仍开放）。详细域笔记见各 `_work/task*.md` Wave 3 附录。

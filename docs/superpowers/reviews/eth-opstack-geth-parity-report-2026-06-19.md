# bcos-evm vs geth/op-geth 语义对齐审查报告

**日期：** 2026-06-19  
**审查范围：** bcos-evm `eth/` + `opstack/`  
**参考版本：** `blockchain-impl/go-ethereum` @ `117e067f0`；`blockchain-impl/op-geth` @ `e8800cffe`  
**模式：** 只读审查（未改代码、未跑 ctest）

---

## Executive Summary

- 总 Task 数：**15**（Task 0 基线 + Task 1–15 语义审查）
- **Blocker：7** | **Warning：18** | **Nit：4** | **一致：6 Task 全绿**
- **总体结论：FAIL**（Eth 纯路径与 OpStack 语义层存在共识级偏差；OpStack fee 公式层基本对齐）

**一句话：** EIP-2929 warm set、OpStack Fjord/Isthmus 费用公式、Branch A refund 与 geth/op-geth **高度一致**；主要 blocker 集中在 **Eth 顶层 gas/transfer 编排**、**EIP-7702 执行闭环**、**deposit 成功 nonce**、**L1Block Isthmus setter 缺 slot**。

---

## Task 0：环境确认

| 项 | 状态 |
|----|------|
| WORKSPACE `bcos-evm/{eth,bcos,opstack}` | ✅ OK |
| REF_ROOT `blockchain-impl/go-ethereum` | ✅ @ `117e067f0` |
| REF_ROOT `blockchain-impl/op-geth` | ✅ @ `e8800cffe` |
| 设计 spec（WORKSPACE） | ✅ 两份可读 |

**审查基线（来自 spec）：** Eth 8 项 + OpStack 7 项 success criteria  
**全局假设：** post-Prague / post-Isthmus / post-Regolith；不含 Bedrock/Ecotone/Jovian 分叉

**建议 ctest 命令：**
```bash
ctest --test-dir build/bcos-evm/test -R 'NestedCallHost|WarmTransactionEntry|ExecuteViaEthFixture|RollupCost|OpStackSettlement|DepositMint|L1AttributesDeposit|Eip7702|EvmoneRefund'
```

---

## 汇总表

| Task | Concern | 结论 | Blocker | Warning | Nit | 测试覆盖 |
|------|---------|------|---------|---------|-----|----------|
| 1 | EthHost call 递归 | ⚠️ 部分偏差 | 0 | 2 | 0 | NestedCallHost ✓ |
| 2 | executeMessage 编排 | ⚠️ 部分偏差 | **1** | 2 | 1 | ExecuteViaEthFixture △ |
| 3 | EIP-2929 warm | ✅ 一致 | 0 | 0 | 0 | WarmTransactionEntry ✓ |
| 4 | EIP-3529 refund | ⚠️ 部分偏差 | **1** | 1 | 0 | SstoreRefund △ |
| 5 | EIP-7702 | ⚠️ 部分偏差 | **3** | 3 | 1 | Eip7702Apply △ |
| 6 | EIP-7623 eth | ⚠️ 部分偏差 | **1** | 1 | 0 | ✗ 无 eth floor test |
| 7 | CanTransfer/transfer | ❌ 严重偏差 | **1** | 0 | 0 | ✗ 缺 eth 顶层 value test |
| 8 | BLOCKHASH/预编译/SD | ⚠️ 部分偏差 | 0 | 2 | 0 | BlockHashHost ✓ |
| 9 | Fjord L1 fee | ✅ 一致 | 0 | 0 | 1 | RollupCostTest ✓ |
| 10 | Isthmus operator | ✅ 一致 | 0 | 1 | 0 | RefundIsthmus ✓ |
| 11 | buyGas/settlement | ⚠️ 部分偏差 | 0* | 3 | 0 | OpStackSettlement ✓ |
| 12 | FloorDataGas OpStack | ✅ 一致 | 0 | 0 | 1 | OpStackFloorGas ✓ |
| 13 | Deposit tx | ⚠️ 部分偏差 | **1** | 1 | 0 | DepositMint ✓ |
| 14 | L1Block | ⚠️ 部分偏差 | **1** | 1 | 0 | L1Attributes △ |
| 15 | OpStack 7702/refund/CanTransfer | ⚠️ 部分偏差 | 0 | 2 | 0 | CanTransfer/EvmoneRefund ✓ |

\* Task 11：非 blob 主路径无 blocker；若支持 blob tx 则 buyGas 缺 blob 扣费为条件 blocker。

---

## Blocker 详情（必须修复）

### [Blocker-1] Eth 路径未完整扣除 intrinsic gas

- **Task：** 2
- **偏差：** `executeViaEth` 在 eip7623 路径仅扣 `normalCost`（4/16 calldata），未扣 21000 + accessList + CREATE + auth tuple gas；OpStack 已在 `OpStackExecuteViaHost.cpp:25-36` 实现完整 debit，Eth 路径缺失。
- **geth 侧：** `go-ethereum/core/state_transition.go:561-571`
- **bcos-evm 侧：** `bcos-evm/eth/ExecuteViaEth.cpp:68-82`
- **建议修复：** 抽取与 OpStack `computeIntrinsicGasDebit` 等价的 eth 入口 helper，在 `executeViaEth` / `executeMessage` 前统一扣减。
- **建议补测：** 带 access list / CREATE 的 fixture，断言 `gas_before_evm` 与 geth 一致。

### [Blocker-2] transition / 非 Web3 路径未应用 calcRefund

- **Task：** 4
- **偏差：** `calcRefund = min(refundCounter, usedGas/5)` 仅在 `EthTransactionExecutorImpl` + eip7623 finalize 路径应用；`transition()` 直接取 `gas_limit - gas_left`，无 /5 cap。
- **geth 侧：** `go-ethereum/core/state_transition.go:647-648,771-782`
- **bcos-evm 侧：** `bcos-evm/eth/state/Transition.cpp:83-84`；`EthTxGasSettlement.h:101-126`
- **建议修复：** 所有 eth 出口统一走 `finalizeEthereumGasUsed` / `postExecuteGasSettlement`。
- **建议补测：** SSTORE clear/refill GeneralStateTest，断言最终 gas_used 含 refund cap。

### [Blocker-3] Eth execute entry 用 normalCost 而非 FloorDataGas

- **Task：** 6
- **偏差：** entry 检查 `message.gas < normalCost`，geth 要求 `msg.GasLimit >= FloorDataGas`（21000 + tokens×10）；post-refund bump 在 executor 层已实现，但 entry 门槛偏低。
- **geth 侧：** `go-ethereum/core/state_transition.go:574-580`
- **bcos-evm 侧：** `bcos-evm/eth/ExecuteViaEth.cpp:72-73` vs `OpStackExecuteViaHost.cpp:61-73`（OpStack 正确）
- **建议修复：** Eth 路径复用 `OpStackFloorGas` / `executeEntryFloorDataGasCheck` 逻辑。
- **建议补测：** 复制 geth `TestFloorDataGas` 向量到 `bcos-evm/test/eth/`。

### [Blocker-4] Eth 顶层 tx value 只 CanTransfer 不 transfer

- **Task：** 7
- **偏差：** `executeViaEth` 检查余额但未调用 `transfer()`；geth 在 EVM.Call/Create 前 Transfer；FISCO 路径有 `maybeTransferValue`，Eth 路径缺失。
- **geth 侧：** `go-ethereum/core/vm/evm.go:294-296`；`core/state_transition.go:596-640`
- **bcos-evm 侧：** `bcos-evm/eth/ExecuteViaEth.cpp:96-120`（缺 transfer）；对比 `bcos-evm/bcos/ExecuteViaHost.cpp:79-110`
- **建议修复：** 在 `executeMessage` 顶层 CALL 前或 `executeViaEth` 内调用 `Transfer.h:13-22`。
- **建议补测：** 顶层 `msg.value > 0` 的 GeneralStateTest + 余额 diff 断言。

### [Blocker-5] EIP-7702 delegation resolve 缺失

- **Task：** 5
- **偏差：** `executeMessage` / `EthHost` 直接 `get_code`，未实现 geth 一层 `0xef0100||addr` code 跟随；CALL 到 delegated EOA 可能执行 designator 而非 target code。
- **geth 侧：** `go-ethereum/core/vm/evm.go:641-650`
- **bcos-evm 侧：** `bcos-evm/eth/executeMessage.cpp:164`；`EthHost.cpp:388-399`
- **建议修复：** 在 `resolveExecutionCode` 中实现 geth `resolveCode` 等价逻辑。
- **建议补测：** pre-state delegated EOA + CALL 应返回 delegatee 结果的 fixture。

### [Blocker-6] Eth 路径未扣 auth tuple intrinsic gas + fixture 未启用 eip7702

- **Task：** 5
- **偏差：** geth 每条 auth 预扣 25000 gas；Eth 路径无对应逻辑。`EthFixtureAdapter` 默认 `eip7702=false`，`stEIP7702_delegation.json` 无 auth list，名称误导。
- **geth 侧：** `go-ethereum/core/state_transition.go:137-138`
- **bcos-evm 侧：** `EthTxGasSettlement.h:83-98`；`test/fixtures/EthFixtureAdapter.h:30-42`
- **建议修复：** Eth 入口扣 auth gas；fixture 启用 `eip7702=true` 并导入真实 7702 向量。
- **建议补测：** type-0x04 tx end-to-end gas 断言。

### [Blocker-7] Deposit 成功未递增 sender nonce

- **Task：** 13
- **偏差：** 成功路径仅 `state.commit()`，无 `set_nonce+1`；failure 路径有 nonce++。geth CALL deposit 必 increment nonce。
- **op-geth 侧：** `op-geth/core/state_transition.go:601-602`
- **bcos-evm 侧：** `bcos-evm/opstack/OpStackExecuteViaHost.cpp:159-169`（成功）vs `:174-175`（失败有 nonce++）
- **建议修复：** 成功 deposit 在 commit 前 `set_nonce(sender, nonce+1)`，或依赖 executeMessage CALL 分支 bump（需与 geth 时机一致）。
- **建议补测：** deposit 成功 nonce N→N+1；连续 deposit depositNonce 递增。

### [Blocker-8] L1Block Isthmus setter 未写 epoch 元数据 slot

- **Task：** 14
- **偏差：** `applySetterIsthmus` 仅写 fee 相关 4 slot；已解析的 sequenceNumber/timestamp/l1BlockNumber/hash/batcherHash 未持久化。
- **op-geth 侧：** `optimism/.../L1Block.sol:164-170`（via EVM）；测试 `L1Block.t.sol:302-312`
- **bcos-evm 侧：** `bcos-evm/opstack/l1/L1BlockPredeploy.cpp:70-76` vs `L1BlockStorage.cpp:46-52`（已解析未写）
- **建议修复：** 补写 L1Block 全量 Isthmus slot 映射。
- **建议补测：** setter 后逐 slot 断言，对齐 `L1Block.t.sol` Isthmus fuzz。

---

## Warning 详情（建议修复）

| ID | Task | 摘要 |
|----|------|------|
| W1 | 1 | 嵌套 CALL 余额不足时 `gas_left=0`，geth 不变 gas |
| W2 | 1 | 缺 EIP-158 空账户 zero-value CALL 早退 |
| W3 | 2 | 顶层空 code CALL 直接 SUCCESS 返全部 gas |
| W4 | 4 | finalize 用 `evmcResult.gas_refund` 而非 `state.get_refund()`，双轨风险 |
| W5 | 5 | 无效 auth 的 authority 未预热（geth 校验前 AddAddressToAccessList） |
| W6 | 5 | ExecuteViaEth 缺 tx 级 preCheck（CREATE+auth、sender EOA/delegation） |
| W7 | 5 | apply auth 前 sender nonce 未 bump |
| W8 | 8 | SELFDESTRUCT 依赖 evmone，Host 层无 Prague 6780 显式逻辑 |
| W9 | 10 | `m_isIsthmus` 需调用方显式设置，未自动推导 |
| W10 | 11 | blob tx preCheck 通过但 buyGas 未扣 blob 费 |
| W11 | 11 | skipTransactionChecks 仿真仍扣 L1/operator |
| W12 | 11 | NoBaseFee 跳过整段 refundGas，geth 仅跳过 coinbase 段 |
| W13 | 13 | system tx 无条件拒绝（pre-Regolith 历史块差异） |
| W14 | 14 | 无 Bedrock/Ecotone/Jovian setter 覆盖（Isthmus-only 可接受） |
| W15 | 15 | `prague_post_execution=false` 仅 RevisionConfig，块处理器未消费 |
| W16 | 15 | deposit 路径仍传入 authorizationList |
| W17 | 6 | transition 路径无 floor check/bump |
| W18 | 9 | 缺 Fjord minimum bounds / Solidity parity 测试 |

---

## 测试缺口清单

| geth/op-geth fixture / 语义 | bcos-evm 对应 test | 状态 |
|------------------------------|-------------------|------|
| Nested CALL/REVERT warm | NestedCallHost / NestedRevertWarm | ✓ |
| EIP-2929 cold/warm | WarmTransactionEntry / Eip2929AccessHost | ✓ |
| Fjord empty tx fee | RollupCostTest / OpStackFeeTest | ✓ |
| Isthmus operator refund | RefundIsthmusTest | ✓ |
| OpStack fee routing 四路 | OpStackSettlementTest | ✓ |
| FloorDataGas formula | OpStackFloorGasTest / CalcRefundTest | ✓ |
| evmone refund Branch A | EvmoneRefundSpikeTest | ✓ |
| CanTransfer nested | CanTransferTest | ✓（OpStack） |
| SSTORE refund → tx gas_used | SstoreRefundTest | △ 仅 counter |
| EIP-3529 calcRefund /5 cap | 无 | ✗ |
| Eth FloorDataGas entry | 无 | ✗ |
| Eth 顶层 value transfer | stCall_emptyAccount (value=0) | ✗ |
| EIP-7702 delegation e2e | stEIP7702_delegation（非 7702） | ✗ 误导 |
| Deposit success nonce++ | DepositMintTest | ✗ |
| L1Block Isthmus 全 slot | L1BlockPredeployTest | △ 仅 fee slot |
| blob buyGas | 无 | ✗ |
| postExecution Isthmus 禁用 | IsthmusPostExecutionPolicyTest | △ 仅读 config |

---

## 各 Task 简要结论

### Task 1 — EthHost::call()：⚠️ 主流程对齐，边界 gas 有差
### Task 2 — executeMessage：⚠️ warm/单帧 execute 正确，intrinsic gas 缺失
### Task 3 — EIP-2929：✅ 与 geth 一致
### Task 4 — EIP-3529：⚠️ SSTORE counter 对齐，tx 级 calcRefund 不完整
### Task 5 — EIP-7702：⚠️ applyAuthorizations 接近 geth，执行闭环未完成
### Task 6 — EIP-7623 eth：⚠️ post-refund bump 有，entry 检查错误
### Task 7 — CanTransfer：❌ 嵌套 OK，顶层 transfer 缺失
### Task 8 — BLOCKHASH/预编译：⚠️ 预编译/BLOCKHASH OK，SELFDESTRUCT 待验证
### Task 9 — Fjord L1：✅ 公式与 golden fixture 一致
### Task 10 — Isthmus operator：✅ 公式与 refund 一致
### Task 11 — Settlement：⚠️ 主路径对齐，blob/skip/NoBaseFee 有差
### Task 12 — OpStack FloorDataGas：✅ 公式与 settlement 对齐
### Task 13 — Deposit：⚠️ 主路径对齐，成功 nonce 缺失
### Task 14 — L1Block：⚠️ fee getter/setter 可用，epoch slot 未写全
### Task 15 — OpStack 叠加：⚠️ 7702/CanTransfer/refund 对齐，postExecution 未接线

---

## 建议后续动作（优先级）

1. **P0 — Eth 入口对齐 OpStack：** 在 `executeViaEth` 复用 `executeEntryChecks` 模式（intrinsic + floor + transfer + auth gas）。
2. **P0 — 顶层 value transfer：** 补 `transfer()` 与 geth 一致。
3. **P0 — EIP-7702 resolve + flag + 真实 fixture。**
4. **P0 — Deposit 成功 nonce++。**
5. **P1 — L1Block Isthmus setter 全 slot 写入。**
6. **P1 — 统一 gas settlement：** transition / 全路径 calcRefund + floor bump。
7. **P2 — OpStack blob buyGas、skip 标志、NoBaseFee 细分。**
8. **P2 — 块处理器读取 `prague_post_execution` 禁用 6110/7002/7251。**

---

## 审查方法说明

- 参考源码：`/Users/octopus/octo/code/blockchain-impl/go-ethereum` @ `117e067f0`，`op-geth` @ `e8800cffe`
- 我方源码：`feat-evm-refactor` worktree `bcos-evm/`
- 未实际运行 ctest；测试覆盖状态基于测试文件静态分析
- 排除：`bcos/` FISCO 扩展语义、`bcos-executor` DAG 路径

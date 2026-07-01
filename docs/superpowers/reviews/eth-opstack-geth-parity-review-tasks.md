# bcos-evm vs geth/op-geth 语义对齐审查 — Master Task List

**用途：** 将本文档整体 `@` 引用给 AI，要求按 Task 0→15 顺序一次性完成全部审查，输出单一汇总报告。

**状态：** Ready  
**日期：** 2026-06-19  
**范围：** `bcos-evm/eth/` + `bcos-evm/opstack/` vs go-ethereum / op-geth  
**排除：** `bcos/` FISCO 扩展语义、`bcos-executor` DAG/旧 executive 路径

---

## 路径约定

| 变量 | 路径 | 说明 |
|------|------|------|
| **WORKSPACE** | `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor` | 我方实现（bcos-evm） |
| **REF_ROOT** | `/Users/octopus/octo/code/blockchain-impl` | 参考实现与文档根目录 |
| **GETH** | `REF_ROOT/go-ethereum` | go-ethereum 源码 |
| **OPGETH** | `REF_ROOT/op-geth` | op-geth 源码 |
| **REF_DOCS** | `REF_ROOT/docs` | 参考文档（gas/deposit/执行流分析等） |

> 下文 Task 中 `GETH/...`、`OPGETH/...` 为简写，实际读取路径为  
> `/Users/octopus/octo/code/blockchain-impl/go-ethereum/...` 与  
> `/Users/octopus/octo/code/blockchain-impl/op-geth/...`

**我方 spec（对齐标准，在 WORKSPACE 内）：**
- `docs/superpowers/specs/2026-06-18-bcos-evm-layer-refactor-design.md`
- `docs/superpowers/specs/2026-06-18-opstack-isthmus-design.md`

**参考文档（在 REF_ROOT 内，辅助理解 geth 语义）：**
- `docs/superpowers/specs/2026-04-27-gas-semantics-comparison.md`
- `docs/superpowers/specs/2026-04-27-deposit-tx-implementation.md`
- `docs/superpowers/specs/2026-04-23-fisco-bcos-op-compatible-execution-path-unified-design.md`
- `docs/evm-initialization.md`

---

## 给 AI 的执行指令（复制此段作为 user message 开头）

```
请阅读并完整执行 @docs/superpowers/reviews/eth-opstack-geth-parity-review-tasks.md 中的 Task 0 到 Task 15。

要求：
1. 严格按 Task 0→15 顺序执行，不得跳过
2. 每个 Task 必须读取「我方文件」和「参考文件」，给出逐步对照表
3. 只读审查，不改代码
4. 每个结论必须引用 `文件:行号`（两侧各至少一处，不确定则标注「需跑测试验证」）
5. 全部 Task 完成后，输出 §Final Report 格式的单一汇总文档
6. 优先读取 REF_ROOT 本地参考仓库；仅当文件不存在时才 fallback GitHub raw，并标注「未本地验证」

路径：
- WORKSPACE（我方）: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
- REF_ROOT（参考）: /Users/octopus/octo/code/blockchain-impl
- GETH: REF_ROOT/go-ethereum
- OPGETH: REF_ROOT/op-geth

设计 spec（WORKSPACE）：
- @docs/superpowers/specs/2026-06-18-bcos-evm-layer-refactor-design.md
- @docs/superpowers/specs/2026-06-18-opstack-isthmus-design.md

参考文档（REF_ROOT，可选辅助）：
- @/Users/octopus/octo/code/blockchain-impl/docs/superpowers/specs/2026-04-27-gas-semantics-comparison.md
- @/Users/octopus/octo/code/blockchain-impl/docs/superpowers/specs/2026-04-27-deposit-tx-implementation.md
```

---

## Task 0：环境确认与审查基线

**目标：** 确认材料可达，建立审查基线，避免后续 Task 因路径缺失而空转。

**步骤：**
1. 确认 WORKSPACE 内 `bcos-evm/` 三轨目录存在：`eth/`、`bcos/`、`opstack/`
2. 确认 REF_ROOT 可达，且存在 `go-ethereum/`、`op-geth/` 子目录
3. 记录 GETH / OPGETH 的 git commit（`git -C REF_ROOT/go-ethereum rev-parse HEAD` 等）
4. 读取 WORKSPACE 两份 design spec 的 success criteria，提取为审查 checklist
5. 列出将运行的 ctest 命令（不需实际运行，仅列出）

**输出：**
- 环境 OK / 部分缺失（列出缺失项）
- Eth 审查基线 8 项 + OpStack 审查基线 7 项（来自 spec）
- 全局假设：post-Prague / post-Isthmus / post-Regolith；OpStack 不含 Bedrock/Ecotone/Jovian 分支

---

## Task 1（Eth-1）：EthHost::call() 递归语义

**Concern：** 嵌套 CALL/CREATE 是否与 geth EVM.Call/Create 一致

**我方文件：**
- `bcos-evm/eth/state/EthHost.cpp`
- `bcos-evm/eth/state/EthHost.hpp`
- `bcos-evm/eth/policy/HostExtension.h`
- `bcos-evm/test/state/NestedCallHostTest.cpp`
- `bcos-evm/test/state/PrecompileInCallTest.cpp`
- `docs/superpowers/specs/2026-06-18-bcos-evm-layer-refactor-design.md` §14

**参考 (GETH = REF_ROOT/go-ethereum)：**
- `GETH/core/vm/evm.go` — `EVM.Call()` (~L239)
- `GETH/core/vm/evm.go` — `EVM.Create()` / Create2
- WORKSPACE design spec §4.3 call() 流程表

**对照要点：**
1. 钩子顺序：routeCall → tryChainPrecompile → precompile → prepareMessage → transferValue → checkpoint → vm.execute → commit/revert
2. DELEGATECALL 预编译门控
3. CREATE/CREATE2：nonce、auth 表、pin_warm_create 时机
4. REVERT：checkpoint rollback + warm set 保留规则

**Oracle 测试：** `ctest -R 'NestedCallHost|PrecompileInCall'`

**输出格式：**
```
### Task 1 结论
| 步骤 | geth 行为 | bcos-evm 行为 | 一致? | severity | 备注 |
```

---

## Task 2（Eth-2）：executeMessage 顶层编排

**Concern：** tx 级编排是否与 StateTransition 执行前准备一致

**我方文件：**
- `bcos-evm/eth/executeMessage.cpp`
- `bcos-evm/eth/executeMessage.h`
- `bcos-evm/eth/ExecuteViaEth.cpp`
- `bcos-evm/eth/ExecuteViaEth.h`
- `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp`

**参考 (GETH)：**
- `GETH/core/state_transition.go` — preCheck → intrinsic gas → Prepare → Create/Call (~L433–509)
- `GETH/core/state_transition.go:493` — `state.Prepare(...)`

**对照要点：**
1. warmTransactionEntry 仅在 tx 级，不在 call() 重复
2. intrinsic gas 扣除顺序
3. access list / transient storage reset
4. 顶层只 vm.execute 一次；嵌套由 EthHost::call() 递归（策略 A）
5. executeViaEth 与 transition() 共享 executeMessage 内核

**Oracle 测试：** `ctest -R 'ExecuteViaEthFixture|PragueStateTest|WarmTransactionEntry'`

---

## Task 3（Eth-3）：EIP-2929 Access List / Warm Set

**Concern：** warm/cold 访问语义

**我方文件：**
- `bcos-evm/eth/execution/warmTransactionEntry.h`
- `bcos-evm/eth/state/State.cpp`
- `bcos-evm/eth/state/State.hpp`
- `bcos-evm/eth/execution/Eip2929PrecompileWarm.h`
- `bcos-evm/test/state/WarmTransactionEntryTest.cpp`
- `bcos-evm/test/state/Eip2929AccessHostTest.cpp`
- `bcos-evm/test/state/NestedRevertWarmTest.cpp`

**参考 (GETH)：**
- `GETH/core/vm/operations_acl.go` — SLOAD/SSTORE/CALL access list
- `GETH/core/state/statedb.go` — Prepare / AddAddressToAccessList / journal revert

**对照要点：**
1. tx 入口 warm：origin、callee、precompile、access list
2. 运行时 cold→warm gas
3. 子帧 REVERT 的 warm journal 行为
4. pinned CREATE 地址 revert 后仍 warm（spec §14 rule 4）

**Oracle 测试：** `ctest -R 'WarmTransactionEntry|Eip2929AccessHost|NestedRevertWarm'`

---

## Task 4（Eth-4）：Gas Refund（EIP-3529）

**Concern：** SSTORE refund 与 calcRefund

**我方文件：**
- `bcos-evm/eth/state/State.cpp`
- `bcos-evm/eth/eip/EthTxGasSettlement.h`
- `bcos-evm/test/state/SstoreRefundTest.cpp`
- `bcos-evm/test/state/StateRefundTest.cpp`
- `bcos-evm/test/state/SstoreStatusTest.cpp`

**参考 (GETH)：**
- `GETH/core/state_transition.go:634` — `calcRefund()`（quotient 5）
- `GETH/core/vm/operations_acl.go` — SSTORE gas/refund
- `GETH/core/state/statedb.go` — refund counter
- `REF_DOCS/superpowers/specs/2026-04-27-gas-semantics-comparison.md`（辅助）

**对照要点：**
1. refund counter 累加/清零时机
2. `calcRefund = min(counter, usedGas/5)`
3. calcRefund 与 returnGas 顺序
4. 确认 eth 路径无 OpStack 叠加干扰

**Oracle 测试：** `ctest -R 'SstoreRefund|StateRefund|SstoreStatus'`

---

## Task 5（Eth-5）：EIP-7702 Set Code Authorization

**Concern：** auth list 处理（纯 eth 路径）

**我方文件：**
- `bcos-evm/eth/Eip7702.cpp`
- `bcos-evm/eth/eip/Eip7702.h`
- `bcos-evm/eth/executeMessage.cpp`
- `bcos-evm/test/fixtures/state/imported/stEIP7702_delegation.json`
- `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp`

**参考 (GETH)：**
- `GETH/core/state_transition.go:505–509` — applyAuthorization 循环
- `GETH/core/state_transition.go` — applyAuthorization / validateAuthorization
- `GETH/core/types/tx_setcode.go:71` — SetCodeAuthorization

**对照要点：**
1. auth list 在 CALL 前 apply 时机
2. 无效 auth skip（geth 忽略 error）
3. delegation code 格式 `0xef0100 || address`
4. existence refund
5. DELEGATECALL 到 delegation EOA

**Oracle 测试：** `ctest -R ExecuteViaEthFixture`（含 stEIP7702_delegation）

---

## Task 6（Eth-6）：EIP-7623 FloorDataGas（Eth 路径）

**Concern：** 纯 eth 层 floor data gas（非 OpStack fee routing）

**我方文件：**
- `bcos-evm/eth/RevisionConfig.h`
- `bcos-evm/eth/executeMessage.cpp`
- `bcos-evm/eth/vm/EthPolicy.h`

**参考 (GETH)：**
- `GETH/core/state_transition.go:119` — `FloorDataGas()`
- `GETH/core/state_transition.go:453–461` — execute entry 检查
- `GETH/core/state_transition.go:637–648` — post-refund floor bump

**对照要点：**
1. Prague 下 eip7623 启用
2. execute entry：`gasLimit >= floorDataGas`
3. post-calcRefund floor bump
4. eth 路径实现位置（executeMessage vs txpool only）

---

## Task 7（Eth-7）：Value Transfer / CanTransfer

**Concern：** 顶层与嵌套 CALL value transfer

**我方文件：**
- `bcos-evm/eth/Transfer.h`
- `bcos-evm/eth/state/EthHost.cpp`
- `bcos-evm/eth/executeMessage.cpp`

**参考 (GETH)：**
- `GETH/core/state_transition.go:476–482` — 顶层 CanTransfer
- `GETH/core/vm/evm.go:271` — CALL 内 CanTransfer
- `GETH/core/evm.go` — CanTransfer 默认实现

**对照要点：**
1. execute entry：balance >= msg.value
2. 嵌套 CALL 每帧 CanTransfer
3. zero value 跳过检查
4. 空账户 / SELFDESTRUCT 边界

**Oracle：** `stCall_emptyAccount.json` fixture（若测试存在）

---

## Task 8（Eth-8）：BLOCKHASH / 预编译 / SELFDESTRUCT

**Concern：** Host 层 opcode 与预编译语义

**我方文件：**
- `bcos-evm/eth/state/EthHost.cpp`
- `bcos-evm/eth/state/EthPrecompiles.cpp`
- `bcos-evm/eth/state/EthPrecompiles.hpp`
- `bcos-evm/eth/precompiled/PrecompileTraits.h`
- `bcos-evm/test/state/BlockHashHostTest.cpp`
- `bcos-evm/test/fixtures/state/imported/stPrecompile_ecrecover.json`
- `bcos-evm/test/fixtures/state/imported/stSelfDestruct_basic.json`

**参考 (GETH)：**
- `GETH/core/vm/evm.go` — GetHash
- `GETH/core/vm/contracts.go` — 预编译 0x01–0x11
- `GETH/core/vm/instructions.go` — SELFDESTRUCT Prague 语义

**对照要点：**
1. get_block_hash 接线与 block 范围
2. 预编译在 CALL 帧内 dispatch
3. DELEGATECALL 预编译门控
4. Prague SELFDESTRUCT 限制

**Oracle 测试：** `ctest -R 'BlockHashHost|PrecompileInCall|ExecuteViaEthFixture'`

---

## Task 9（OP-1）：RollupCost / Fjord L1 Fee 公式

**Concern：** Fjord L1 data fee 逐常量对齐

**我方文件：**
- `bcos-evm/opstack/fee/RollupCost.cpp`
- `bcos-evm/opstack/fee/RollupCost.h`
- `bcos-evm/test/opstack/RollupCostTest.cpp`

**参考 (OPGETH = REF_ROOT/op-geth)：**
- `OPGETH/core/types/rollup_cost.go:607` — `NewL1CostFuncFjord`
- `OPGETH/core/types/rollup_cost_test.go:68` — canonical empty-tx fixture
- WORKSPACE `docs/superpowers/specs/2026-06-18-opstack-isthmus-design.md` §1 criteria 1

**对照要点：**
1. newRollupCostData：zeroes/ones/fastLzSize
2. FastLz 线性回归系数逐项对比
3. empty tx fixture expected wei
4. rounding / overflow

**Oracle 测试：** `ctest -R RollupCost`

---

## Task 10（OP-2）：Isthmus Operator Fee + Refund

**Concern：** operator fee 公式与 refundIsthmusOperatorCost

**我方文件：**
- `bcos-evm/opstack/fee/OpStackFee.cpp`
- `bcos-evm/opstack/fee/OpStackFee.h`
- `bcos-evm/opstack/OpStackTxExecutor.cpp`
- `bcos-evm/test/opstack/RefundIsthmusTest.cpp`
- `bcos-evm/test/opstack/OpStackFeeTest.cpp`

**参考 (OPGETH)：**
- `OPGETH/core/types/rollup_cost.go:253` — `newOperatorCostFuncIsthmus`
- `OPGETH/core/state_transition.go:812` — `refundIsthmusOperatorCost()`
- `OPGETH/core/state_transition.go:703` — settlement 调用点
- WORKSPACE opstack-isthmus spec §1 criteria 2

**对照要点：**
1. 公式 `gas × scalar / 1e6 + constant`
2. refund = operatorCost(gasLimit) - operatorCost(gasUsed)
3. refund 时机相对 returnGas
4. 确认未误用 Jovian OperatorFeeFix

**Oracle 测试：** `ctest -R 'RefundIsthmus|OpStackFee'`

---

## Task 11（OP-3）：buyGas / Fee Routing / Settlement 全链路

**Concern：** 非 deposit tx 完整 fee 链路

**我方文件：**
- `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- `bcos-evm/opstack/OpStackTxExecutor.cpp`
- `bcos-evm/opstack/fee/OpStackGasSettlement.h`
- `bcos-evm/test/opstack/OpStackSettlementTest.cpp`
- `bcos-evm/test/opstack/GasFeeCapBalanceTest.cpp`
- `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp`
- `bcos-evm/opstack/OpStackConstants.h`

**参考 (OPGETH)：**
- `OPGETH/core/state_transition.go:277` — buyGas
- `OPGETH/core/state_transition.go:665–699` — fee recipients
- `OPGETH/core/state_transition.go:778` — calcRefund
- `OPGETH/core/state_transition.go:798` — returnGas
- `REF_DOCS/superpowers/specs/2026-04-27-gas-semantics-comparison.md`（辅助）
- WORKSPACE opstack-isthmus spec Q2/Q5

**对照要点：**
1. effectiveGasPrice = min(tipCap+baseFee, feeCap)
2. balance check 用 gasFeeCap
3. routing：0x4200…0019/001A/001B + coinbase
4. SUCCESS/REVERT/hard failure 均 return unused gas
5. fee routing 用的 gasUsed 是 post-refund 值

**Oracle 测试：** `ctest -R 'OpStackSettlement|GasFeeCapBalance|OpStackExecuteViaHost'`

---

## Task 12（OP-4）：EIP-7623 FloorDataGas（OpStack 路径）

**Concern：** OpStack execute entry + post-refund floor

**我方文件：**
- `bcos-evm/opstack/fee/OpStackFloorGas.cpp`
- `bcos-evm/opstack/fee/OpStackFloorGas.h`
- `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- `bcos-evm/test/opstack/OpStackFloorGasTest.cpp`

**参考 (OPGETH)：**
- `OPGETH/core/state_transition.go:455–461` — execute entry
- `OPGETH/core/state_transition.go:637–648` — post-calcRefund bump
- WORKSPACE opstack-isthmus spec §1 criteria 6、Q6

**对照要点：**
1. deposit tx 跳过 floor check
2. non-deposit：executeEntryChecks 含 intrinsic + FloorDataGas
3. postExecuteGasSettlement 顺序
4. peakGasUsed vs gasUsed

**Oracle 测试：** `ctest -R OpStackFloorGas`

---

## Task 13（OP-5）：Deposit Tx（0x7E）全流程

**Concern：** Regolith+ deposit 语义

**我方文件：**
- `bcos-evm/opstack/OpStackDepositTx.h`
- `bcos-evm/opstack/OpStackPreCheck.cpp`
- `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- `bcos-evm/test/opstack/DepositTxPreCheckTest.cpp`
- `bcos-evm/test/opstack/DepositMintTest.cpp`
- `bcos-evm/test/opstack/DepositNoFeeRoutingTest.cpp`

**参考 (OPGETH)：**
- `OPGETH/core/types/deposit_tx.go`
- `OPGETH/core/state_transition.go:341` — preCheck deposit 分支
- `OPGETH/core/state_transition.go:652–662` — Regolith 跳过 fee
- `REF_DOCS/superpowers/specs/2026-04-27-deposit-tx-implementation.md`（辅助）
- WORKSPACE opstack-isthmus spec §1 criteria 8–9、architecture deposit path

**对照要点：**
1. type 0x7E；无 fee/nonce 检查
2. system tx 拒绝
3. mint 在 checkpoint 之前
4. 无 buyGas；post-execution 跳过 fee routing
5. failure：revert + nonce++

**Oracle 测试：** `ctest -R 'DepositTxPreCheck|DepositMint|DepositNoFeeRouting'`

---

## Task 14（OP-6）：L1Block Storage + Getter Dispatch

**Concern：** L1 attributes 写 slot、fee params 读、getter dispatch

**我方文件：**
- `bcos-evm/opstack/l1/L1BlockStorage.cpp`
- `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- `bcos-evm/opstack/l1/L1BlockPredeploy.h`
- `bcos-evm/opstack/OpHostExtension.h`
- `bcos-evm/test/opstack/L1AttributesDepositTest.cpp`
- `bcos-evm/test/opstack/L1BlockGetterTest.cpp`
- `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`
- `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin`

**参考 (OPGETH)：**
- `OPGETH/core/` — L1Block predeploy `0x4200…0015` getter/setter 实现
- Isthmus selector `0x098999be`，176-byte payload
- WORKSPACE opstack-isthmus spec §4 block ordering、§8.4 Q4 decision D

**对照要点：**
1. L1 attributes 写 slot 映射
2. loadOpStackFeeParams 读取时机
3. empty-code top-level CALL → OpHostExtension → getter
4. L1 attrs 失败 rollback（mint before checkpoint）
5. getter 只读

**Oracle 测试：** `ctest -R 'L1AttributesDeposit|L1BlockGetter|L1BlockPredeploy'`

---

## Task 15（OP-7）：OpStack 叠加层 — 7702 / evmone Refund / CanTransfer / PostExecution

**Concern：** Isthmus 叠加语义

**我方文件：**
- `bcos-evm/opstack/OpStackPreCheck.cpp`
- `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- `bcos-evm/eth/Eip7702.cpp`
- `bcos-evm/test/opstack/Eip7702PreCheckTest.cpp`
- `bcos-evm/test/opstack/Eip7702ApplyAuthorizationTest.cpp`
- `bcos-evm/test/opstack/Eip7702DelegationSenderTest.cpp`
- `bcos-evm/test/opstack/CanTransferTest.cpp`
- `bcos-evm/test/opstack/EvmoneRefundSpikeTest.cpp`
- `bcos-evm/test/opstack/IsthmusPostExecutionPolicyTest.cpp`
- `bcos-evm/test/opstack/CalcRefundTest.cpp`

**参考 (OPGETH)：**
- `OPGETH/core/state_transition.go:570` — CanTransfer execute entry
- `OPGETH/core/state_transition.go:598` — applyAuthorization
- `OPGETH/core/state_transition.go:778–802` — calcRefund / returnGas
- `OPGETH/core/vm/evm.go:271` — nested CanTransfer
- `OPGETH/core/state_processor.go:141` — 6110/7002/7251 disabled at Isthmus
- WORKSPACE opstack-isthmus spec §1 criteria 7、13–15；§5.9 Branch A

**对照要点：**
1. preCheck auth list vs execute-time apply
2. delegation EOA sender 允许
3. evmone Branch A：gas_left pre-refund；无 double-count
4. CanTransfer execute entry + nested 同一规则
5. praguePostExecution=false at Isthmus — **tx-level scope + source/CI gate**（`prague_post_execution` RevisionConfig 字段已删除）

**Oracle 测试：** `ctest -R 'Eip7702|CanTransfer|EvmoneRefund|IsthmusPostExecution|CalcRefund'`

---

## §Final Report（全部 Task 完成后必须输出）

AI 完成 Task 0–15 后，输出以下结构的**单一汇总报告**（可写入 `docs/superpowers/reviews/eth-opstack-geth-parity-report-YYYY-MM-DD.md`）：

```markdown
# bcos-evm vs geth/op-geth 语义对齐审查报告

**日期：** YYYY-MM-DD  
**审查范围：** bcos-evm eth + opstack  
**参考版本：** `blockchain-impl/go-ethereum` @ \<commit\>；`blockchain-impl/op-geth` @ \<commit\>

## Executive Summary
- 总 Task 数：15
- Blocker：N | Warning：N | Nit：N | 一致：N
- 总体结论：PASS / PASS with warnings / FAIL

## 汇总表

| Task | Concern | 结论 | Blocker | Warning | Nit | 测试覆盖 |
|------|---------|------|---------|---------|-----|----------|
| 1 | EthHost call 递归 | ... | 0 | 1 | 0 | NestedCallHost ✓ |
| ... | ... | ... | ... | ... | ... | ... |
| 15 | OpStack 叠加层 | ... | 0 | 0 | 1 | EvmoneRefund ✓ |

## Blocker 详情（必须修复）
### [Blocker-1] <标题>
- **Task：** N
- **偏差：** ...
- **geth 侧：** `file:line` — ...
- **bcos-evm 侧：** `file:line` — ...
- **建议修复：** ...
- **建议补测：** ...

## Warning 详情（建议修复）
...

## 测试缺口清单
| geth/op-geth fixture | bcos-evm 对应 test | 状态 |
|----------------------|-------------------|------|
| rollup_cost_test empty tx | RollupCostTest | ✓/✗/缺失 |

## 建议后续动作
1. ...
2. ...
```

---

## 单 Task 输出模板（每个 Task 1–15 必须使用）

```markdown
### Task N：<名称>

**结论：** ✅ 一致 / ⚠️ 部分偏差 / ❌ 严重偏差

| # | 检查项 | geth/op-geth | bcos-evm | 一致? | severity | 引用 |
|---|--------|--------------|----------|-------|----------|------|
| 1 | ... | ... | ... | Y/N/? | -/warning/blocker | `file:line` |

**测试 oracle：** ✓已覆盖 / △部分 / ✗缺失 — <test name>

**本 Task blocker 数：** 0  
**本 Task 建议补测：** ...
```

---

## 执行顺序与依赖

```
Task 0 (baseline)
  → Task 1–3 (Eth Host + warm — 内核，优先)
  → Task 4–8 (Eth EIP 语义)
  → Task 9–12 (OpStack fee engine)
  → Task 13–15 (OpStack 语义叠加)
  → §Final Report
```

**原则：** 发现 Eth 内核 blocker 时，在 Final Report 中标注对 OpStack Task 的「连带影响」，但 OpStack Task 仍须独立完成后给出结论。

---

## 快速启动（Cursor 一键）

**User message：**

```
@docs/superpowers/reviews/eth-opstack-geth-parity-review-tasks.md
@/Users/octopus/octo/code/blockchain-impl/go-ethereum
@/Users/octopus/octo/code/blockchain-impl/op-geth

请完整执行 Task 0 到 Task 15，输出 §Final Report。
模式：只读，不改代码。中文输出。

参考根目录：/Users/octopus/octo/code/blockchain-impl
我方工作区：/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
```

**可选：** 并行 subagent 时，Eth Task 1–8 与 Op Task 9–15 可分给两个 agent，但 Final Report 须由主 agent 合并去重。

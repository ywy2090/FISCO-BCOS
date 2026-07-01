# bcos-evm 错误处理 Parity — 测试用例构造提示词

**用途：** 将本文档整体 `@` 引用给 AI，按 v2 问题列表为 open GAP 构造 characterization 测试，并对照 go-ethereum / op-geth 参考用例。  
**状态：** Ready  
**日期：** 2026-06-26  
**问题来源：** `docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26-v2.md` §8  
**范围：** `bcos-evm/test/` + `transaction-executor/tests/` 错误路径 characterization  
**排除：** 已关闭项（ADR-025 settlement、OpStackEntryFailureAbortNormal 等）

**相关文档：**

- v2 审查报告：`docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26-v2.md`
- 审查提示词（只读）：`docs/superpowers/reviews/error-handling-geth-parity-prompt.md`
- 实现计划：`docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md`
- ADR-015 included-tx vmerr：`bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md`
- ADR-025 OpStack entry abort：`bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md`

---

## 给 AI 的执行指令（复制此段作为 user message 开头）

```
请阅读并完整执行 @docs/superpowers/reviews/error-handling-geth-parity-test-prompt.md。

要求：
1. 严格按 Task 0→11 顺序：先查 geth/op-geth 参考用例，再写 characterization 测试（红灯），不先改生产代码
2. 每个用例必须填写「参考锚点表」：我方断言 + GETH/OPGETH file:line 或 test 名；无直接单测则标注「源码推导 + 需手工验证」
3. 测试风格对齐现有 characterization（OpStackTxLifecycleCharacterizationTest、EthIncludedTxVmerrTest、OrchestrationErrorPolicyTest）
4. 注册到对应 CMakeLists（EthTests.cmake / OpStackTests.cmake / CrossTests.cmake / transaction-executor tests）
5. Shell 命令使用 `rtk` 前缀；每完成一个 GAP 跑对应 ctest 并记录结果
6. 已关闭项（ADR-025 settlement、OpStackEntryFailureAbortNormal）跳过，仅补「仍 open」项

路径：
- WORKSPACE: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
- REF_ROOT: /Users/octopus/octo/code/blockchain-impl
- GETH: REF_ROOT/go-ethereum
- OPGETH: REF_ROOT/op-geth
- 问题来源: @docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26-v2.md §8
```

---

## 角色与目标

你是 **bcos-evm 错误处理 parity 测试工程师**。目标是为 v2 报告中的 open GAP 编写 **characterization 测试**：先锁定**当前行为**与 **geth/op-geth 期望行为**的对照 oracle，为后续修复提供回归基线。

### 原则

| 原则 | 说明 |
|------|------|
| **Characterization first** | 测试先描述「现在是什么 + geth 是什么」，修复 PR 单独做 |
| **Deep module 优先** | 优先测 `runOpStackTxLifecycle` / `ethReferenceExecute` / `runDepth1` 等公开接口，少测 private helper |
| **双轨断言** | 每条用例含 `CURRENT_ORACLE`（我方现状）与 `GETH_ORACLE`（注释或 `#if 0` 期望），修复后翻转 |
| **不重复已有** | ADR-025 settlement、lifecycle #2/#3/#8 已有 — 不新建 `OpStackEntryFailureAbortNormal` |

### 必读

- v2 报告：`docs/superpowers/reviews/error-handling-geth-parity-report-2026-06-26-v2.md`
- 实现计划：`docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md`
- 现有 helper：`bcos-evm/test/fixtures/EthFrameParityHelpers.h`、`bcos-evm/test/opstack/helpers/OpStackLifecycleTestHelpers.h`

---

## 路径约定

| 变量 | 路径 | 说明 |
|------|------|------|
| **WORKSPACE** | `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor` | 我方实现 |
| **REF_ROOT** | `/Users/octopus/octo/code/blockchain-impl` | 参考实现根目录 |
| **GETH** | `REF_ROOT/go-ethereum` | go-ethereum 源码 |
| **OPGETH** | `REF_ROOT/op-geth` | op-geth 源码 |

开始时记录 GETH / OPGETH 的 `git rev-parse HEAD`。

---

## v2 修正后 open GAP 速查

### P0 / P1 — 需构造测试

| ID | 严重度 | 问题 |
|----|--------|------|
| GAP-001 | P0/P1 | ETH entry failure 仍 receipt + `OutOfGasLimit(2)`，`gasUsed=gasLimit` |
| GAP-002 | P1 | Inclusion：include failed tx vs geth reject（ADR-025 settlement 已解决） |
| GAP-004 | P1 | 嵌套 CALL InsufficientBalance 硬编码 `gas_left=0` |
| GAP-009 | P1 | 同 `EVMC_INSUFFICIENT_BALANCE` → `InsufficientFunds(10015)` vs `NotEnoughCash(7)` |
| GAP-010 | P1 | `evmcStatusToTransactionStatus` 与 `evmcStatusToErrorMessage` 双表不一致 |
| GAP-TE-002 | P1 | `m_topLevelIncludedTxVmError` 未参与 TE gas 结算 |
| GAP-TE-003 | P1 | top-level INSUFFICIENT_BALANCE 不 normalize → 可能丢 partial state |
| GAP-003 | P1 | 异常路径产 evmcResult + receipt vs geth reject |
| GAP-005 | P1 | API 层双映射不一致；次要路径 `VMInstance` 可抛异常 |
| GAP-TE-001 | P1 | buyGas 失败 Finalize 仍 makeReceipt |
| GAP-TE-004 | P1 | ETH entry failure 仍 included + receipt |
| GAP-TE-005 | P1 | OpStack entry failure 仍 failed receipt |
| §2-映射 | P1 | `INVALID_MEMORY_ACCESS→StackUnderflow`；`STATIC_MODE_VIOLATION→Unknown` |
| Deposit-gasPool | P1 | Deposit gasPool reject 路径 vs op-geth `ErrGasLimitReached` |
| Plan-Task1 | P1 | PrecompileRouter 失败时 `gas_left=0` |

### P2 — 可选

| ID | 问题 |
|----|------|
| GAP-006 | `checkBalanceAndValue` 失败强制 `GasAffordRejected`，命名误导 |
| GAP-007 | OpStack system tx `Malformed` vs `ErrSystemTxNotSupported` |
| GAP-008 | EIP-3529 cap 已实现，缺 characterization 边界断言 |
| GAP-011 | `VMInstance` 单参构造可抛 `UnknownEVMCStatus` |
| Isthmus-E2E | operator refund lifecycle 端到端仍缺 |

### 已关闭 — 跳过

| 项 | 说明 |
|----|------|
| ADR-025 | phantom fee + `abortNormalAfterBuyGas` + lifecycle #2/#3/#8 |
| ADR-015 | included-tx vmerr normalize |
| OpStack deposit entry failure | 对齐 op-geth execute:486-510 |
| Task 8 applyStateDiff / makeReceipt(status) | PASS |

---

## Task 0：建立 geth/op-geth 测试索引

审查开始时在 REF_ROOT 执行：

```bash
cd REF_ROOT/go-ethereum && rtk git rev-parse HEAD
cd REF_ROOT/op-geth && rtk git rev-parse HEAD
rtk grep -l "ErrIntrinsicGas\|ErrInsufficientFunds\|ErrGasLimitReached\|RefundQuotientEIP3529\|ErrInsufficientBalance\|ErrWriteProtection" REF_ROOT/go-ethereum --glob "*_test.go"
rtk grep -l "DepositTx\|ErrGasLimitReached\|deposit" REF_ROOT/op-geth --glob "*_test.go"
```

输出「参考测试索引表」（见 §参考索引），供后续 Task 引用。

每个后续 Task 必须输出：

1. **测试文件路径** + `BOOST_AUTO_TEST_CASE` 名
2. **场景 setup**（账户、gasLimit、calldata、revision）
3. **断言清单**（status / gas_left / gasUsed / stateDiff / receipt / gasPool）
4. **GETH/OPGETH 参考**（test 名 + file:line，或源码行 + 「无单测」）
5. **CMake 注册** + **ctest 命令**

---

## Task 1 — GAP-001 / GAP-TE-001 / GAP-TE-004

**用例名：** `EthIntrinsicGasFailureRejectBehavior`

| 项 | 内容 |
|----|------|
| **文件** | 新建 `bcos-evm/test/eth/EthIntrinsicGasFailureCharacterizationTest.cpp`；可选 TE 层 `transaction-executor/tests/EthEntryFailureCharacterizationTest.cpp` |
| **场景 A** | intrinsic gas 不足：`gasLimit = params.TxGas - 1000`（21000→20000） |
| **场景 B** | buyGas 余额不足（effective cost > balance，但 intrinsic 够） |
| **CURRENT_ORACLE** | `OutOfGasLimit(2)`；`gas_left=0`；TE 仍 `makeReceipt`；`gasUsed=gasLimit`；state 不变或仅 buyGas 前状态 |
| **GETH_ORACLE** | tx **reject**，**无 receipt**；区块处理 err |

**geth 参考：**

| 参考 | 路径 |
|------|------|
| 区块级 reject 矩阵 | `go-ethereum/core/state_processor_test.go:181-186`（`ErrIntrinsicGas`） |
| insufficient funds | `go-ethereum/core/state_processor_test.go:171-176` |
| txpool precheck | `go-ethereum/core/txpool/legacypool/legacypool_test.go:423` |
| simulated client | `go-ethereum/ethclient/simulated/options_test.go:71` |
| 源码 | `go-ethereum/core/state_transition.go:565-567`；`core/state_processor.go:176-178` |

**op-geth：** 同路径 fork（`op-geth/core/state_processor_test.go` 结构相同）

**我方现有：** `OrchestrationErrorPolicyTest.cpp` E-IGF-01/02（仅 ErrorPolicy 层，缺 TE/lifecycle）

**ctest：** `rtk ctest -R EthIntrinsicGasFailure --output-on-failure`

---

## Task 2 — GAP-002 / GAP-TE-005

**Inclusion 语义（ETH + OpStack entry failure receipt）**

| 项 | 内容 |
|----|------|
| **文件** | 扩展 `EthIntrinsicGasFailureCharacterizationTest.cpp`；扩展 `OpStackTxLifecycleCharacterizationTest.cpp` |
| **断言** | entry failure 后：**是否产 receipt**、receipt.status、区块是否「包含该 tx」 |
| **CURRENT_ORACLE** | 我方 **include** + failed receipt |
| **GETH_ORACLE** | **reject**，后续 tx 索引不变 |

**geth 参考：** `core/state_processor_test.go:136-240` — 每个 case 的 `want` 含 `could not apply tx`

**op-geth 参考：** 同文件；另读 `op-geth/core/state_transition.go:527-529`

**已有（不测 settlement）：** `lifecycle_normal_intrinsic_reject_gas_used_zero_and_returns_full_gas_pool` — ADR-025 settlement ✅；本 Task 只补 **receipt 存在性 / inclusion 决策** 断言

---

## Task 3 — GAP-004 / Plan Task 2

**用例名：** `NestedCallInsufficientBalanceGasLeft`

| 项 | 内容 |
|----|------|
| **文件** | 新建 `bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp`；复用 `EthFrameParityHelpers.h` |
| **场景** | depth-1 CALL，带 value，余额不足触发 `EVMC_INSUFFICIENT_BALANCE` |
| **CURRENT_ORACLE** | `gas_left=0`（`ExecutionFrame.cpp:147-154`） |
| **GETH_ORACLE** | nested vmerr，**parent 保留 gas**（`evm.go:262-264` 返回 `(nil, gas, ErrInsufficientBalance)`） |

**geth 参考：**

| 类型 | 路径 |
|------|------|
| **源码（主 oracle）** | `go-ethereum/core/vm/evm.go:262-264,356,496` |
| **间接 trace** | `go-ethereum/eth/tracers/internal/tracetest/calltrace_test.go:274-285`（nested call trace 格式参考，非 balance 场景） |
| **无直接单测** | geth **没有** named `TestInsufficientBalanceGasLeft` — 标注「源码推导」；可选写 Go snippet 在 REF_ROOT 临时验证 |

**我方现有（修复后需更新期望）：** `PrecompileRouterEnvelopeTest.cpp:c5_insufficient_balance_both_depths` 当前断言 `gasLeft=0`

**ctest：** `rtk ctest -R InsufficientBalanceGasLeft`

---

## Task 4 — Plan Task 1

**PrecompileRouter gas 保留**

| 项 | 内容 |
|----|------|
| **文件** | 扩展 `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp` |
| **场景** | identity precompile + value transfer + insufficient balance（depth 0 与 depth 1） |
| **GETH_ORACLE** | 同 Task 3（router 在 geth 等价于 `evm.Call` 前置 balance check） |

**参考：** `go-ethereum/core/vm/evm.go` + `PrecompileRouter.cpp:19-24`

---

## Task 5 — GAP-TE-002 / Plan Task 3

**用例名：** `TopLevelIncludedTxVmErrorGasSettlement`

| 项 | 内容 |
|----|------|
| **文件** | 扩展 `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp` + `transaction-executor/tests/EthTxGasSettlementTest.cpp` |
| **场景** | top-level invalid opcode / OOG（非 REVERT），触发 `m_topLevelIncludedTxVmError` |
| **CURRENT_ORACLE** | TE `settleGasUsedFromEvmResult` **忽略** flag，gasUsed 可能按 `gas_left` 算 |
| **GETH_ORACLE** | included tx vmerr：`gasUsed = gasLimit - gas_left`（peak 语义见 ADR-015） |

**geth 参考：**

| 参考 | 路径 |
|------|------|
| 源码 | `go-ethereum/core/state_transition.go` refund/finalize 段 |
| 我方 | `EthIncludedTxVmerrTest.cpp` 已有 pure helper 向量（EEST invalid tx） |

**ctest：** `rtk ctest -R 'EthIncludedTxVmerr|EthTxGasSettlement'`

---

## Task 6 — GAP-TE-003

**用例名：** `TopLevelInsufficientBalanceStateDiff`

| 项 | 内容 |
|----|------|
| **文件** | 新建 `bcos-evm/test/eth/TopLevelInsufficientBalanceStateDiffTest.cpp` |
| **场景** | EIP-7702 SetCode tx：authorization 已 apply，随后 top-level transfer 因 balance 失败 |
| **CURRENT_ORACLE** | `normalizeIncludedTxVmerr` **不** normalize INSUFFICIENT_BALANCE → TE **skip** `applyStateDiff` |
| **GETH_ORACLE** | top-level insufficient → **reject tx**（无 partial state 上链） |

**geth 参考：**

| 参考 | 路径 |
|------|------|
| 区块 reject | `go-ethereum/core/state_processor_test.go:165-170`（`ErrInsufficientFundsForTransfer`） |
| 7702 API | `go-ethereum/internal/ethapi/api_test.go:748+`（SetCode 构造，非 balance 场景） |
| **无直接 7702+reject 单测** | 标注「需 EEST / 手工 state test」 |

**我方 helper：** `bcos-evm/test/helpers/SetCodeAuthorizationTestHelper.h`

---

## Task 7 — GAP-005 / GAP-009 / GAP-010 / GAP-011 / Plan Task 5

**用例名：** `EvmcStatusMappingCompleteness`

| 项 | 内容 |
|----|------|
| **文件** | 新建 `bcos-evm/test/eth/EvmcStatusMappingTest.cpp` |
| **场景** | 遍历 `evmc_status_code` 枚举；对每条断言：`evmcStatusToTransactionStatus` vs `evmcStatusToErrorMessage` vs `adoptEvmcResult` **一致** |
| **重点 case** | `EVMC_INSUFFICIENT_BALANCE`（7 vs 10015）；`EVMC_INVALID_MEMORY_ACCESS`；`EVMC_STATIC_MODE_VIOLATION` |
| **GETH_ORACLE** | `go-ethereum/core/vm/errors.go:30-205`（`vm.Error` ↔ RPC 语义）；无 EVMC 层 — 映射表为我方责任 |

**geth 参考：**

| evmc / 场景 | geth |
|-------------|------|
| `STATIC_MODE_VIOLATION` | `errors.go:36` `ErrWriteProtection`；runtime staticcall 测例 `core/vm/runtime/runtime_test.go:617+` |
| `OUT_OF_GAS` vs intrinsic | `state_processor_test.go:181` vs execution OOG（trace `calltrace_test.go:288` stack underflow 耗尽 gas） |
| unknown status | `errors.go:205` `VMErrorCodeUnknown` fallback |

**次要路径：** `VMInstance.cpp:22-23` 单参构造 — 单独 case 断言 throw vs adopt 不 throw

**ctest：** `rtk ctest -R EvmcStatusMapping`

---

## Task 8 — GAP-003

**Pipeline exception → INTERNAL_ERROR**

| 项 | 内容 |
|----|------|
| **文件** | 扩展 `OrchestrationErrorPolicyTest.cpp`（已有 E-PEX-02）；补 TE 集成 case |
| **CURRENT_ORACLE** | `EVMC_INTERNAL_ERROR` + `Unknown`；**不抛** |
| **GETH_ORACLE** | 未预期异常 → 区块 reject（`state_transition.go:550-552`） |

**已有：** `OrchestrationErrorPolicyTest.cpp:72+`

---

## Task 9 — GAP-008

**用例名：** `Eip3529RefundCapBoundary`

| 项 | 内容 |
|----|------|
| **文件** | 扩展 `transaction-executor/tests/EthTxGasSettlementTest.cpp`（已有 `EffectiveRefundEip3529_cap`） |
| **场景** | 边界：`refund = gasUsed/5`、`refund > stateRefund`、`gasUsed=0` |
| **GETH_ORACLE** | `go-ethereum/core/state_transition.go:778` `RefundQuotientEIP3529`（=5） |

**geth 参考：** **无 named refund cap 单测** — 源码 `state_transition.go:807-816` + 我方 `TxIntrinsicGas.h:113-119` 数学对齐即可

**已有：** `EthTxGasSettlementTest.cpp:220-224` — 本 Task 补 **pipeline 级** 边界而非仅 pure function

---

## Task 10 — Deposit-gasPool / §3.2

**用例名：** `DepositGasPoolRejectVsOpGeth`

| 项 | 内容 |
|----|------|
| **文件** | 新建 `bcos-evm/test/opstack/OpStackDepositGasPoolCharacterizationTest.cpp` |
| **场景** | deposit tx `gas > block gas pool remaining` |
| **CURRENT_ORACLE** | 我方 OOG 短路，可能不走 `finalizeDeposit` |
| **OPGETH_ORACLE** | 整笔 reject `ErrGasLimitReached`（`state_transition.go:486-510`） |

**op-geth 参考：**

| 参考 | 路径 |
|------|------|
| 源码 | `op-geth/core/state_transition.go:484-510,628` |
| Err 定义 | `op-geth/core/error.go:57-59` |
| gas pool | `op-geth/core/gaspool.go:44` |
| 区块测试 | `op-geth/core/state_processor_test.go:159,187`（generic `ErrGasLimitReached`，非 deposit 专用） |
| **无 deposit+gasPool 专用单测** | 标注「源码推导」；可读 `REF_ROOT/docs/superpowers/specs/2026-04-27-deposit-tx-implementation.md` |

---

## Task 11 — GAP-006 / GAP-007（P2，可选）

| GAP | 测试 | 参考 |
|-----|------|------|
| GAP-006 | `TxPipeline` exitKind 文档化测试或 comment-only characterization | 我方 `TxPipeline.cpp:89-94` |
| GAP-007 | 已有 `OpStackPrecheckPolicyTest.cpp` — 补 system tx `Malformed` case 注释对照 op-geth `ErrSystemTxNotSupported` | `OpStackPrecheckPolicy.cpp:67` |

---

## §参考索引（geth / op-geth 速查）

| 错误域 | 最有用 geth 测试 | op-geth 增量 |
|--------|------------------|--------------|
| Entry reject（intrinsic/funds） | `core/state_processor_test.go:136-240` | 同 fork |
| Txpool precheck | `core/txpool/legacypool/legacypool_test.go:423+` | `op-geth/core/txpool/legacypool/legacypool_test.go:435` |
| Nested CALL gas / trace | `eth/tracers/internal/tracetest/calltrace_test.go` | 同 fork |
| vm.Error 映射 | `core/vm/errors.go`（非 test） | 同 fork |
| Refund cap | **无单测** → `state_transition.go:778` | 同 fork |
| Deposit 执行 | **无 execute 级单测** → `state_transition.go:484-510` | 同段 + `types/receipt_opstack_test.go`（receipt 编码，非 gasPool） |
| Insufficient balance nested | **无单测** → `core/vm/evm.go:262-264` | 同 fork |
| ADR-025 settlement | — | `OpStackTxLifecycleCharacterizationTest` #2/#3/#8 ✅ |

### 已有我方测试（勿重复）

| 测试文件 | 覆盖 |
|----------|------|
| `OpStackTxLifecycleCharacterizationTest.cpp` | ADR-023 矩阵 #1–#8；#2/#3/#8 覆盖 ADR-025 abort |
| `OpStackNormalFeeSettlementTest.cpp` | entry reject abort 单元测试 |
| `EthIncludedTxVmerrTest.cpp` | top-level invalid normalize；7623 settlement |
| `OrchestrationErrorPolicyTest.cpp` | intrinsic/OOG/exception；INSUFFICIENT_BALANCE 排除 |
| `EthTxGasSettlementTest.cpp:220-224` | EIP-3529 pure function cap |
| `PrecompileRouterEnvelopeTest.cpp` | precompile insufficient balance（期望待 Task 3/4 更新） |

---

## §输出格式

全部 Task 完成后输出单一 Markdown：

```markdown
# 错误处理 Parity 测试构造报告

## 测试索引
| GAP | 测试文件 | CASE 名 | ctest -R | 状态 |
|-----|----------|---------|----------|------|
| ... | ... | ... | ... | pass/fail/skip |

## geth/op-geth 参考缺口
（列出「无单测、仅源码」项及建议 Go 临时验证脚本）

## 运行结果
rtk ctest -R '...' 摘要
```

---

## 实施顺序建议

```
P0/P1: Task 1 → 3 → 4 → 5 → 6 → 7 → 2 → 10
P2:    Task 8 → 9 → 11
跳过:  ADR-025 已有 lifecycle 用例
```

---

## 2026-06-23 plan 对照

| Plan Task | 内容 | 本提示词 Task |
|-----------|------|---------------|
| Task 1 | PrecompileRouter gas 保留 | Task 4 |
| Task 2 | nested insufficient balance gas | Task 3 |
| Task 3 | `finalizeEthTxGasUsed` / TE 消费 flag | Task 5 |
| Task 5 | `EthTxOutcome` + 统一无 throw 映射 | Task 7 |

---

*文档结束。基于 error-handling-geth-parity-report-2026-06-26-v2.md*

# bcos-evm 错误处理 / 错误码 — geth·op-geth 差异审查提示词

**用途：** 将本文档整体 `@` 引用给 AI，专项排查 `bcos-evm` 错误处理、错误码与 go-ethereum / op-geth 的差异。  
**状态：** Ready  
**日期：** 2026-06-26  
**范围：** `bcos-evm/eth/` + `bcos-evm/opstack/` + `transaction-executor/` 错误链路 vs geth / op-geth  
**排除：** `bcos/` FISCO 扩展语义（仅附录标注有意偏离）、legacy `bcos-executor` / DAG

**相关文档：**

- 全量 parity 审查：`docs/superpowers/reviews/eth-opstack-geth-parity-review-tasks.md`
- ETH 错误处理实现计划：`docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md`
- ADR-015 included-tx vmerr：`bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md`
- ADR-025 OpStack entry reject：`bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md`

---

## 给 AI 的执行指令（复制此段作为 user message 开头）

```
请阅读并完整执行 @docs/superpowers/reviews/error-handling-geth-parity-prompt.md 中的 Task 0 到 Task 9。

要求：
1. 严格按 Task 0→9 顺序执行，不得跳过
2. 只读审查，不改代码
3. 每个 FAIL/GAP 必须引用 `我方 file:line` + `GETH/OPGETH file:line`（不确定则标注「需 characterization 测试验证」）
4. 优先读取 REF_ROOT 本地参考仓库；仅当文件不存在时才 fallback GitHub raw，并标注「未本地验证」
5. 全部 Task 完成后，输出本文 §输出格式 的单一 Markdown 报告
6. Shell 命令使用 `rtk` 前缀

路径：
- WORKSPACE（我方）: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
- REF_ROOT（参考）: /Users/octopus/octo/code/blockchain-impl
- GETH: REF_ROOT/go-ethereum
- OPGETH: REF_ROOT/op-geth
```

---

## 角色与目标

你是区块链执行层 parity 审查员。**只读审查，不改代码**。结论必须可追溯到源码行号；不确定的标注「需 characterization 测试验证」。

### 目标

系统对比 **bcos-evm** 与 **go-ethereum（geth）**、**op-geth** 在以下维度的差异：

1. 错误分类 taxonomy（共识拒绝 vs 包含在区块中的失败 vs 嵌套 frame 失败）
2. EVMC / 执行结果 status 映射（`evmc_status_code` ↔ receipt / RPC 语义）
3. 框架层 `TransactionStatus` 与 geth `vm.Error` / `TransitionDb err` 的对应关系
4. `gas_left` / `gasUsed` / refund 在各类错误路径上的行为
5. precheck / intrinsic gas / buyGas 失败时的 early-return 与 state 回滚
6. OpStack 特有：deposit、L1 fee、operator fee、block gas pool、entry reject（ADR-025）

---

## 路径约定

| 变量 | 路径 | 说明 |
|------|------|------|
| **WORKSPACE** | `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor` | 我方实现 |
| **REF_ROOT** | `/Users/octopus/octo/code/blockchain-impl` | 参考实现根目录 |
| **GETH** | `REF_ROOT/go-ethereum` | go-ethereum 源码 |
| **OPGETH** | `REF_ROOT/op-geth` | op-geth 源码 |

审查开始时记录 GETH / OPGETH 的 `git rev-parse HEAD`。

---

## 范围

### In scope

- `bcos-evm/eth/` — 共享内核 + ETH 参考编排
- `bcos-evm/opstack/` — OP Stack 编排
- `transaction-executor/` 中与错误映射相关的 TE 层（`makeReceipt`、`buyGas` 失败路径）
- 三 Bridge 入口：`fiscoExecute` / `ethReferenceExecute` / `opStackExecute`

### Out of scope

- `bcos-evm/bcos/` 中 FISCO 专有语义：`BALANCE_TRANSFER_GAS=21000`、auth table、`fix_error_handling`、`NotFoundCodeError` 等 — **仅附录标注「FISCO 有意偏离」，不做 geth FAIL**
- legacy `bcos-executor` / DAG / `HostContext`

### 分层基准

| 路径 | 对齐基准 |
|------|----------|
| ETH 参考（`ethReferenceExecute`） | **go-ethereum** |
| OpStack（`opStackExecute`） | **op-geth**（geth 仅作 fallback） |
| FISCO（`fiscoExecute`） | 单独附录，不与 geth 判 FAIL |

---

## 必读我方锚点文件

### 入口与错误出口

- `bcos-evm/bcos/FiscoExecutionBridge.{h,cpp}`
- `bcos-evm/eth/reference/EthReferenceBridge.{h,cpp}`
- `bcos-evm/opstack/OpStackExecutionBridge.{h,cpp}`
- `bcos-evm/opstack/OpStackTxLifecycle.cpp`

### 管线与 ErrorPolicy

- `bcos-evm/eth/pipeline/TxPipeline.cpp` — try/catch + `TxPipelineExitKind`
- `bcos-evm/eth/pipeline/StateTransitionContext.h` — `earlyExit` / `exitKind`
- `bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h`
- `bcos-evm/bcos/FiscoOrchestrationErrorPolicy.h`
- `bcos-evm/eth/reference/EthOrchestrationErrorPolicy.h`
- `bcos-evm/opstack/OpStackOrchestrationErrorPolicy.h`

### Precheck 错误注入

- `bcos-evm/eth/reference/EthPrecheckPolicy.cpp` / `EthTxPrecheck.cpp`
- `bcos-evm/bcos/FiscoPrecheckPolicy.cpp`
- `bcos-evm/opstack/OpStackPrecheckPolicy.cpp`

### 状态码映射与归一化

- `bcos-evm/eth/EVMCResult.{h,cpp}` — `evmcStatusToTransactionStatus` / `makeErrorEVMCResult` / `fillErrorOutputInPlace`
- `bcos-evm/eth/pipeline/normalizeIncludedTxVmerr.h` — included-tx vmerr（ADR-015）
- `bcos-evm/opstack/OpStackPipelineInternals.h` — `makeOutOfGasLimitResult` / `makeInternalErrorResult`

### OpStack 费用与 entry failure

- `bcos-evm/opstack/OpStackNormalFeeSettlement.cpp`
- `bcos-evm/opstack/OpStackSettlement.cpp`
- `bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md`
- `bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md`
- `bcos-evm/docs/adr/023-opstack-tx-lifecycle.md`

### TE 调用方如何处理 Result

- `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`
- `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`
- `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`
- `bcos-evm/bcos/FiscoTxFeeLedger.h` / `eth/reference/EthTxFeeLedger.h`

### 已有测试（characterization 基准）

- `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp`
- `bcos-evm/test/eth/OrchestrationErrorPolicyTest.cpp`
- `bcos-evm/test/opstack/OpStackOrchestrationErrorPolicyTest.cpp`
- `bcos-evm/test/opstack/OpStackTxLifecycleCharacterizationTest.cpp`
- `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp`

### 历史计划（避免重复劳动）

- `docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md`
- `docs/superpowers/specs/2026-06-23-eth-evm-error-handling-parity-design.md`

---

## 必读 geth / op-geth 锚点

### go-ethereum

- `core/state_transition.go` — `TransitionDb` / `execute` / intrinsic gas / preCheck 错误
- `core/vm/evm.go` — `Call` / `Create` 错误返回
- `core/vm/errors.go` — `vm.Error` 定义
- `core/error.go` 或等价 — 交易层错误类型
- `internal/ethapi/api.go` — receipt status / error 对外暴露（如有）

### op-geth（在 geth 基础上增量）

- `core/state_transition.go` — deposit tx、`buyGas`、L1 cost、operator fee
- `core/types/deposit_tx.go`
- Regolith/Canyon/Isthmus 等与 deposit gas / mint 相关的错误路径
- block gas pool / `GasPool` 相关（对比我方 `GasPoolHooks`）

---

## 审查任务

### Task 0：建立错误 taxonomy 对照表

列出三方共用的分类维度，作为后续 Task 的列头：

| 维度 | geth | op-geth | bcos-evm |
|------|------|---------|----------|
| 交易未进入 EVM（precheck/intrinsic/buyGas） | | | |
| Top-level EVM vmerr 但 tx 仍 included | | | |
| Top-level REVERT | | | |
| Nested CALL/CREATE 失败 | | | |
| INSUFFICIENT_BALANCE（value transfer） | | | |
| OUT_OF_GAS（intrinsic vs execution） | | | |
| INTERNAL_ERROR / 未映射异常 | | | |

**输出：** 填好的 taxonomy 表 + GETH/OPGETH commit hash。

---

### Task 1：EVMC status ↔ 框架 TransactionStatus 映射

- 完整枚举 `evmc_status_code` 在我方的映射（`EVMCResult.cpp`）
- 对比 geth 如何将 `vm.Error` 转为 receipt status / `ExecutionResult`
- 标注：`UnknownEVMCStatus` 抛出 vs geth 行为
- 标注：ETH 路径 `OutOfGasLimit` vs FISCO `OutOfGas` 命名差异

**输出：** Status 映射矩阵（见 §输出格式 §2）。

---

### Task 2：runTxPipeline 错误流（三链 ErrorPolicy）

对每种 `TxPipelineExitKind` 填表：

| exitKind | 触发条件 | 最终 evmcResult | gas_left | state checkpoint | 三链 ErrorPolicy 差异 |
|----------|----------|-----------------|----------|------------------|----------------------|

重点：`RulesRejected` / `IntrinsicRejected` / `GasAffordRejected` / `ExceptionHandled` / `Completed`。

区分三条错误通道：

1. 编排层 `earlyExit` + 预填 `ctx.evmcResult`
2. EVM 内 vmerr（`executeMessage` 返回）
3. C++ 异常 → `OrchestrationErrorPolicy::onPipelineException`

---

### Task 3：ETH 参考 — included-tx vmerr（ADR-015）

- 我方：`normalizeIncludedTxVmerr` / `normalizeSetCodeTransactionVmerr` / `topLevelIncludedTxVmError`
- geth：`TransitionDb` 对 top-level vm err 的处理（`err==nil` 但 receipt failed 语义）
- 对比：gasUsed 结算（EIP-3529 refund cap、7623 floor）
- 列出当前测试覆盖与缺口

**geth 锚点：** `core/state_transition.go` — `TransitionDb` vmerr handling、`applyAuthorization`

---

### Task 4：ETH 参考 — precheck 错误族

逐条对照 `EthTxPrecheck` / `EthPrecheckPolicy` 与 geth `StateTransition` preCheck：

- nonce / balance / gas limit / EIP-1559 fee cap / 4844 blob / 7702 auth intrinsic
- 每条：我方 `status_code` + `TransactionStatus` + 是否 `earlyExit` + geth 是否 reject tx

---

### Task 5：嵌套 frame 错误（内核）

- `EthHost::call` / `ExecutionFrame` / `PrecompileRouter`
- 对照 geth `evm.Call`：REVERT vs OOG vs insufficient balance 的 `gas_left` 保留规则
- 引用 `docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md` 中已知 gap，标注是否已修复

---

### Task 6：OpStack — entry failure 与 op-geth（ADR-025）

- 我方：`IntrinsicRejected` / `GasAffordRejected` → `abortNormalAfterBuyGas`
- op-geth：`innerExecute` 在 intrinsic/floor/transfer 失败时是否调用 `returnGas` / 是否写 receipt
- 对比：`buyGas` 失败、gas pool OOG、deposit vs normal 分支
- 检查 phantom `l1Fee` / `operatorFee` 是否仍有路径泄漏

**op-geth 锚点：** `state_transition.go` — `innerExecute` early return before settlement

---

### Task 7：OpStack — 执行后 settlement 错误

- `settleNormal` / `settleDeposit` / `refundGas`
- op-geth 对应 receipt meta（L1 fee、deposit nonce、Regolith gasUsed）
- 各 evmc status 下 `gasUsed`、`stateDiff` 是否 apply

---

### Task 8：TE 层错误契约（调用方）

- 三 TE 对 `EVMC_SUCCESS` / `EVMC_REVERT` / 其他 status 的 `applyStateDiff` 条件
- `buyGas` 失败返回空 receipt 的路径
- `makeReceipt` 如何用 `evmcResult.status` vs `status_code`
- 判断：TE 行为是否放大或掩盖 bridge 层错误语义

---

### Task 9：FISCO 有意偏离附录（非 FAIL）

简要列出 FISCO 相对 geth 的已知产品差异，避免误报：

- auth check / auth table
- `BALANCE_TRANSFER_GAS` / 21000 语义
- `fix_revert_logs` / `fix_error_handling`
- `NotFoundCodeError` → REVERT vs geth 行为
- `FiscoOrchestrationErrorPolicy` 特有异常映射

---

## 输出格式（Final Report 模板）

审查完成后，AI 应输出以下结构的单一 Markdown 文档：

```markdown
# bcos-evm 错误处理 / 错误码 geth·op-geth Parity 报告

## Executive Summary
- P0 共识/费用错误（必须修）
- P1 参考路径 parity gap
- P2 文档/测试缺口
- Accepted 有意偏离

## 环境
- GETH commit: ...
- OPGETH commit: ...
- WORKSPACE branch: ...

## §1 Taxonomy 对照总表

## §2 Status 映射矩阵

| evmc_status_code | bcos TransactionStatus | geth vm.Error / TransitionDb | receipt 语义 | 备注 |
|------------------|------------------------|------------------------------|--------------|------|

## §3 分路径差异详表

### 3.1 ETH Reference (`ethReferenceExecute`)
### 3.2 OpStack (`opStackExecute`)
### 3.3 FISCO 附录（有意偏离）

## §4 错误路径时序

（mermaid 至少 2 张：ETH included-vmerr / OpStack entry-reject）

## §5 测试覆盖 gap + 建议 characterization 用例

## §6 与已有 ADR/Plan 的交叉引用

| 项 | ADR/Plan | 状态（已解决 / 仍 open） |
|----|----------|-------------------------|

## Appendix：文件索引
```

---

## 证据规则

1. 每个 **FAIL / GAP** 必须引用：`我方 file:line` + `GETH/OPGETH file:line`
2. 优先读本地 REF_ROOT；不存在才 GitHub raw，并标注「未本地验证」
3. 不猜测 evmone 行为 — 引用现有测试或标注需补 test
4. 区分「编排层 earlyExit」与「EVM 内 vmerr」与「C++ 异常映射」三条通道
5. 可选验证测试：

```bash
rtk ctest -R 'EthIncludedTxVmerr|OrchestrationError|OpStackOrchestration|OpStackTxLifecycle|OpStackSettle'
```

---

## 禁止事项

- 不修改源码
- 不把 FISCO 专有语义判为 geth FAIL（放附录）
- 不泛泛而谈「可能不一致」— 必须落到具体分支或测试名
- 不重复全量 opcode/EIP 审查（仅限错误处理链路）

---

## 使用变体

### 仅 ETH 参考路径

删除 Task 6–7；OpStack 改为 Out of scope；基准仅 GETH。

### 审查 + 测试互证

在 user message 末尾追加：

```
完成审查后运行：
rtk ctest -R 'EthIncludedTxVmerr|OrchestrationError|OpStackOrchestration|OpStackTxLifecycle|OpStackSettle'
将失败用例与报告 GAP 互证。
```

### 与全量 parity 审查的关系

| 文档 | 关系 |
|------|------|
| `eth-opstack-geth-parity-review-tasks.md` | 全量 parity（Task 0–15）；**本文档是其错误处理子集** |
| `2026-06-23-eth-evm-error-handling-parity.md` | 已实现/待实现 plan；审查时交叉引用，避免重复立项 |

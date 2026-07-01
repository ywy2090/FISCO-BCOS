# bcos-evm EVM 执行逻辑 / 状态一致性 — geth·op-geth 差异审查提示词

**用途：** 将本文档整体 `@` 引用给 AI，专项排查 `bcos-evm` EVM 执行逻辑与 go-ethereum / op-geth 的差异，**聚焦可能导致链上状态不一致（state root / receipt / balance / nonce 漂移）的行为**。  
**状态：** Ready  
**日期：** 2026-06-26  
**范围：** `bcos-evm/eth/` + `bcos-evm/opstack/` + `transaction-executor/` 状态写回路径 vs geth / op-geth  
**排除：** `bcos/` FISCO 扩展语义（单独附录）、legacy `bcos-executor` / DAG

**相关文档（交叉引用，避免重复立项）：**

| 文档 | 关系 |
|------|------|
| `docs/superpowers/reviews/eth-opstack-geth-parity-review-tasks.md` | 全量 parity Task 0–15 |
| `docs/superpowers/reviews/error-handling-geth-parity-prompt.md` | 错误码 / taxonomy 子集 |
| `docs/superpowers/plans/2026-06-23-eth-evm-error-handling-parity.md` | ETH 错误处理已规划项 |
| `bcos-evm/capability-matrix.md` | 能力契约 |
| `bcos-evm/docs/adr/015` / `025` / `026` | included-vmerr / entry reject / fee settlement |

---

## 给 AI 的执行指令（复制此段作为 user message 开头）

```
请阅读并完整执行 @docs/superpowers/reviews/evm-execution-state-parity-prompt.md 中的 Task 0 到 Task 12。

要求：
1. 严格按 Task 0→12 顺序执行，不得跳过
2. 只读审查，不改代码
3. 每个 STATE-GAP 必须引用 `我方 file:line` + `GETH/OPGETH file:line`；不确定标注「需 characterization / state test 验证」
4. 优先读取 REF_ROOT 本地参考仓库；文件不存在才 GitHub raw，并标注「未本地验证」
5. 全部 Task 完成后，输出本文 §Final Report 模板的单一 Markdown 报告
6. 每个状态维度必须回答：**成功路径写什么、失败路径是否回滚、TE 是否 apply StateDiff、receipt 反映什么**
7. Shell 命令使用 `rtk` 前缀

路径：
- WORKSPACE: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
- REF_ROOT: /Users/octopus/octo/code/blockchain-impl
- GETH: REF_ROOT/go-ethereum
- OPGETH: REF_ROOT/op-geth
```

---

## 角色与目标

你是 EVM **状态一致性** parity 审查员。核心问题不是「代码风格」，而是：

> **在同一输入（tx + pre-state + block context）下，bcos-evm 产生的 world state delta、receipt 字段、gas 计量是否与 geth/op-geth 等价？**

### 必查状态面（State Surfaces）

| # | 状态面 | 典型不一致症状 |
|---|--------|------------------|
| S1 | **Gas** | gasUsed 偏差、refund 多/少扣、7623 floor 未 bump、OpStack L1/operator 泄漏 |
| S2 | **Balance** | buyGas 预扣/退还错误、value transfer 双扣/漏扣、deposit mint 错账户 |
| S3 | **Nonce** | sender nonce 未 bump、CREATE 合约 nonce 初始值错、7702 auth nonce 校验 |
| S4 | **合约地址** | CREATE/CREATE2 地址公式、top-level vs nested、空地址 pin |
| S5 | **Code / storage** | code deposit OOG、storage slot 脏标记、transient storage 未清 |
| S6 | **Logs** | REVERT 后 log 残留、included-vmerr 是否清 log、OpStack fix_revert_logs |
| S7 | **SELFDESTRUCT** | EIP-6780 同 tx CREATE 跟踪、beneficiary 转账、destroy 列表 |
| S8 | **返回状态** | status_code vs 实际 state commit、included-vmerr 归一化 |
| S9 | **Access / warm** | 2929 cold/warm 影响 gas 进而影响 state；revert 后 warm 集 |
| S10 | **StateDiff 写回** | 何种 status 下 TE `applyStateDiff`；与 geth journal commit 对齐 |

### 分层基准

| 路径 | 对齐基准 | 状态写回 |
|------|----------|----------|
| ETH 参考 `ethReferenceExecute` | **geth** `StateTransition` + `StateDB` | TE: SUCCESS/REVERT apply diff |
| OpStack `opStackExecute` | **op-geth** | lifecycle 内 commit + TE apply |
| FISCO `fiscoExecute` | 附录（有意偏离） | 仅 SUCCESS apply |

---

## 路径约定

| 变量 | 路径 |
|------|------|
| WORKSPACE | `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor` |
| REF_ROOT | `/Users/octopus/octo/code/blockchain-impl` |
| GETH | `REF_ROOT/go-ethereum` |
| OPGETH | `REF_ROOT/op-geth` |

审查开始时：`git -C GETH rev-parse HEAD`、`git -C OPGETH rev-parse HEAD`。

---

## 我方执行链路（审查时必画）

```text
Bridge (fisco/eth/opStack Execute)
  → runTxPipeline (precheck / intrinsic / earlyExit)
  → executeMessage → TxExecutionAdapter
  → runExecutionFrame(TopLevel)
  → evmone → EthHost::call → runExecutionFrame(Nested)
  → State 内存突变 → build_diff() → StateDiff
  → TE applyStateDiff(storage) [条件因链而异]
  → FeeLedger refund / OpStack settlement
  → makeReceipt
```

**状态一致性审查必须沿此链路逐段标注 commit/revert 边界。**

---

## 必读我方锚点

### 内核执行与 State

- `bcos-evm/eth/ExecuteMessage.{h,cpp}` / `execution/TxExecutionAdapter.cpp`
- `bcos-evm/eth/execution/ExecutionFrame.{h,cpp}`
- `bcos-evm/eth/state/EthHost.{hpp,cpp}`
- `bcos-evm/eth/state/State.{hpp,cpp}` — balance/nonce/code/storage/refund/selfdestruct
- `bcos-evm/eth/state/StateDiff.hpp` / `bcos/StateDiffApplier.h`
- `bcos-evm/eth/execution/CreateContract.h` / `FrameValueTransfer.h`
- `bcos-evm/eth/execution/WarmTransactionEntry.h` / `Eip2929Access.h`
- `bcos-evm/eth/Eip7702.cpp`
- `bcos-evm/eth/precompiled/PrecompileRouter.cpp`

### 管线 / 费用 / 状态写回条件

- `bcos-evm/eth/pipeline/TxPipeline.cpp`
- `bcos-evm/eth/eip/TxFeeSettlement.h` / `TxIntrinsicGas.h` / `Eip1559.h`
- `bcos-evm/eth/reference/EthTxFeeLedger.h`
- `bcos-evm/opstack/OpStackTxLifecycle.cpp`
- `bcos-evm/opstack/OpStackNormalFeeSettlement.cpp` / `OpStackSettlement.cpp`
- `bcos-evm/opstack/OpStackTxFeeLedger.cpp`
- `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md`

### Bridge 与 TE

- `bcos-evm/*/ *ExecutionBridge.{h,cpp}`
- `transaction-executor/bcos-transaction-executor/*TransactionExecutorImpl.h`

### 表征测试（优先作 oracle）

- `bcos-evm/test/state/` — refund、2929、revert warm、selfdestruct
- `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp`
- `bcos-evm/test/eth/Eip7702*.cpp`
- `bcos-evm/test/eth/CreateErrorParityTest.cpp`（若存在）
- `bcos-evm/test/opstack/DepositCreateNonceTest.cpp` / `DepositMintTest.cpp`
- `bcos-evm/test/opstack/OpStackSettlement*.cpp`
- `bcos-evm/test/fixtures/state/imported/st*.json`
- `bcos-evm/test/cross/FeeSettlementCharacterizationTest.cpp`

---

## 必读 geth / op-geth 锚点

### go-ethereum（状态突变主路径）

- `core/state_transition.go` — `TransitionDb` / `execute` / `buyGas` / `refundGas` / `calcRefund`
- `core/state/statedb.go` — `Prepare` / `Snapshot` / `RevertToSnapshot` / `Commit` / `AddLog`
- `core/vm/evm.go` — `Call` / `Create` / `Create2` value transfer + checkpoint
- `core/vm/interface.go` — `CanTransfer` / `Transfer`
- `core/vm/operations_acl.go` — SLOAD/SSTORE gas + refund
- `core/vm/contract.go` — code deposit gas
- `core/types/receipt.go` — status / logs / contract address

### op-geth 增量

- `core/state_transition.go` — deposit、`mint`、`buyGas`、L1/operator cost、`GasPool`
- `core/types/deposit_tx.go`
- Isthmus/Regolith/Canyon 相关 gasUsed / receipt meta

### 参考文档（REF_ROOT，辅助）

- `docs/superpowers/specs/2026-04-27-gas-semantics-comparison.md`
- `docs/superpowers/specs/2026-04-27-deposit-tx-implementation.md`

---

## 审查任务

### Task 0：状态面 × 路径 矩阵（基线）

建立三维表格，后续 Task 往表中填 cell：

| 状态面 S1–S10 | ETH 参考 | OpStack | FISCO 附录 | geth 锚点 | 我方主文件 |
|---------------|----------|---------|------------|-----------|------------|

并列出 **commit/revert 边界**：

| 边界 | geth | bcos-evm |
|------|------|----------|
| Tx 开始 | StateDB Prepare | warmTransactionEntry / checkpoint? |
| buyGas 后 | balance 预扣 | FiscoTxFeeLedger / OpStack buyGas |
| 帧失败 REVERT | Snapshot revert | State::revert / checkpoint |
| Tx 成功结束 | Finalise + Commit | build_diff + applyStateDiff |
| included-vmerr | state 保留、receipt failed? | normalizeIncludedTxVmerr |

---

### Task 1（S1 Gas）：计量与 settlement

**对照要点：**

1. **Intrinsic gas** — 扣除时机、`message.gas` 是否为唯一 owner（ADR-019）
2. **Execution gas** — `gas_left`、nested call 返还规则
3. **Refund（EIP-3529）** — `State` refund counter、`min(refund, used/5)` 时机
4. **EIP-7623 floor** — entry 检查 vs post-refund bump；ETH vs OpStack entry reject（ADR-025）
5. **Top-level gasUsed** — `settleTopLevelTransactionGas` vs geth `gasUsed = gasPool - leftover`
6. **Included-vmerr** — gasUsed 是否按 peakGasUsed 模型（ADR-015）
7. **OpStack** — L1 fee / operator fee 是否改变 balance；entry reject 是否 zero gasUsed

**我方：** `State.cpp`、`EthTxGasSettlement.h`、`TxFeeSettlement.h`、`OpStackGasSettlement.h`  
**geth：** `state_transition.go` calcRefund / gas pool  
**op-geth：** deposit gasUsed = gasLimit 等 fork 规则

**Oracle：** `ctest -R 'SstoreRefund|StateRefund|EthIncludedTxVmerr|OpStackSettlement|FeeSettlement'`

---

### Task 2（S2 Balance）：余额突变全路径

逐路径填表：`路径 | 何时 mutate | 失败是否回滚 | geth 等价 | 风险`

必查路径：

- TE `buyGas` 预扣 / `refundGas` 退还
- 顶层 / 嵌套 `transferValue`（`FrameValueTransfer` / `EthHost`）
- OpStack deposit `mint`
- Precompile 调用前后 value transfer（`PrecompileRouter` envelope）
- insufficient balance：state 是否变化、gas_left 是否保留
- EIP-7702 / delegation 是否影响 balance 路径

**geth：** `CanTransfer` / `Transfer` / `buyGas` / `refundGas`  
**我方：** `Transfer.h`、`EthHost.cpp`、`EthTxFeeLedger`、`OpStackTxFeeLedger`

---

### Task 3（S3 Nonce）：账户 nonce 语义

必查：

1. **Tx sender nonce** — TE `updateNonce` vs geth `useNonce` / state transition bump
2. **Contract CREATE nonce** — 新账户 nonce=1；FISCO `bumpContractCreateNonce` / persist 回调
3. **In-account nonce for CALL** — `fix_nonce_init` 等行为
4. **EIP-7702** — authority nonce 校验与应用后 nonce 变化
5. **OpStack deposit** — `receiptMeta.depositNonce` vs state nonce
6. **失败 tx** — nonce 是否仍递增（consensus reject vs included failure）

**我方：** `TxExecutionAdapter.cpp`、`FiscoEvmHostHooks.cpp`、`Eip7702.cpp`、`DepositCreateNonceTest`  
**geth：** `state_transition.go` nonce check / `SetNonce`

---

### Task 4（S4 合约地址）：CREATE / CREATE2

必查：

1. **地址公式** — CREATE `keccak(rlp([sender, nonce]))`、CREATE2 salt+code hash
2. **Top-level 派生** — `deriveMessage` / `FiscoAddressDerivation` vs geth `createAddress`
3. **Nested CREATE** — `EthHost` / `ExecutionFrame` 内 recipient 绑定
4. **空地址 / pin warm** — CREATE 失败/revert 后 `create_address` 字段
5. **Collision** — 目标已有 code 时的 REVERT/OOG 与 state 影响
6. **Receipt contractAddress** — 仅 SUCCESS 时写入？

**我方：** `CreateContract.h`、`FrameTargetResolver`、`FiscoOrchestrationErrorPolicy::onPostExecuteNormalize`  
**geth：** `evm.create` / `CREATE` opcode handler / receipt 字段

**Oracle：** `stCreate_initCode.json`、`stCreate2_basic.json`、`DepositCreateNonceTest`

---

### Task 5（S5 Code / Storage / Transient）

必查：

1. **Code deploy gas** — max code size、initcode size（EIP-3860/7951）
2. **Storage SSTORE** — dirty tracking、original value、EIP-2200/2929 gas
3. **Transient storage（EIP-1153）** — tx 结束清零
4. **Empty account clearing（EIP-161/6780）**
5. **StateDiff 字段** — `codeDirty` / `nonceDirty` / `balanceDirty` 是否漏标

**我方：** `State.cpp`、`StateDiff.hpp`、`RevisionConfig.h`  
**geth：** `statedb.go` / `operations_acl.go`

**Oracle：** `stSstore*` fixtures、`SstoreStatusTest`

---

### Task 6（S6 Logs）：事件日志

必查：

1. **emit_log** — topic/data 累积位置（`State` vs `kernelOutput.logs`）
2. **REVERT** — 帧 revert 是否清 log；top-level REVERT log 是否上 receipt
3. **Included-vmerr** — geth 是否保留 log；我方 `normalizeIncludedTxVmerr` 后 log 行为
4. **EIP-7702 setcode tx REVERT** — auth 已 apply 时 log 语义（ADR-015）
5. **FISCO fix_revert_logs** — 非 SUCCESS 清 log
6. **OpStack / TE convertLogs** — 地址编码 hex 是否影响 hash

**我方：** `EthHost::emit_log`、`TxExecutionAdapter`、`FiscoExecutionBridge` convertLogs  
**geth：** `StateDB.AddLog` / receipt logs

**Oracle：** `NestedRevertWarmTest`、`EthIncludedTxVmerrTest`

---

### Task 7（S7 SELFDESTRUCT）

必查：

1. **EIP-6780** — 同 tx CREATE 的 account 是否可 destruct
2. **Beneficiary transfer** — balance 转移 vs FISCO 禁用
3. **destroy list finalize** — `finalize_self_destructs` 时机
4. **Revert 后** — destruct 标记是否回滚

**我方：** `EthHost::selfdestruct`、`State.cpp`、`EvmHostHooks::allowSelfdestruct`  
**geth：** `Selfdestruct6780` / `stSelfDestruct_*`

---

### Task 8（S8 返回状态 vs 实际 State）

**核心：status 与 state commit 是否一致**

必查：

1. **Included top-level vmerr** — status 归一化为 SUCCESS 但 gasUsed>0 / state 已变
2. **REVERT** — status=REVERT 且 state diff 部分保留（7702 auth）
3. **Consensus reject** — earlyExit 时 stateDiff 必须为空；OpStack ADR-025 abort
4. **EVMC_INSUFFICIENT_BALANCE** — 顶层 reject vs nested 保留 gas
5. **TE apply 条件** — 三路径 SUCCESS/REVERT/其他 对照表
6. **Receipt status** — `evmcResult.status` vs `status_code` 双字段映射

**我方：** `normalizeIncludedTxVmerr.h`、三 TE `executeStep<1>`、`EVMCResult.cpp`  
**geth：** `TransitionDb` err nil 但 `ExecutionResult.Failed()`

**交叉引用：** `error-handling-geth-parity-prompt.md` Task 3/8（避免重复展开 taxonomy，聚焦 state 后果）

---

### Task 9（S9 Access List / Warm — 间接状态）

Warm/cold 影响 gas → 影响 execution 能否完成 → 影响最终 state。

必查：

1. `warmTransactionEntry` 入口集 vs geth `Prepare`
2. Nested revert 后 access set（`NestedRevertWarmTest`）
3. Precompile / chain port static warm（`CallTargetResolver`）
4. 2929 预编译地址

**Oracle：** `WarmTransactionEntryTest`、`Eip2929AccessHostTest`

---

### Task 10（S10 StateDiff 写回与 Journal 对齐）

必查：

1. **`State::build_diff()`** — 相对 cold reader 的 delta 是否完整
2. **`applyStateDiff`** — 字段应用顺序；nonce/code/storage 条件
3. **RollbackableStorage** — TE savepoint 与 execution checkpoint 关系
4. **失败 tx** — 是否可能 apply 部分 diff（双写 bug）
5. **OpStack** — lifecycle 内 `state.commit()` vs `abortNormalAfterBuyGas` revert
6. **FISCO persistContractCreateNonce** — 绕过 StateDiff 直接写 storage 的路径

**我方：** `StateDiffApplier.h`、`OpStackTxLifecycle.cpp`、`TransactionExecutorImpl.h`

---

### Task 11：OpStack 专有状态（op-geth 基准）

必查（每项：`op-geth 行为 | 我方 | state 风险`）：

1. **Deposit tx** — mint、gasUsed、receipt、nonce、system tx 拒绝
2. **L1 attributes deposit** — 固定 calldata、失败 state
3. **L1Block / GasPriceOracle predeploy** — storage 读写影响 fee state
4. **Block gas pool** — 与 state 无关但影响 tx 是否执行 → 间接 state
5. **Regolith/Canyon/Isthmus** — deposit gas、operator fee recipient balance
6. **Normal entry reject（ADR-025）** — buyGas revert 后 balance 是否恢复

**Oracle：** `DepositMintTest`、`L1AttributesDepositTest`、`OpStackPreDebitCharacterizationTest`

---

### Task 12：FISCO 有意偏离附录 + 汇总

不在此判 FAIL，但列出若误用到 ETH/Op 路径会致不一致的 FISCO 行为：

- `BALANCE_TRANSFER_GAS` / 21000 预扣
- auth table CREATE 副作用
- `skipHostValueTransfer` / 禁 selfdestruct
- CREATE 地址 FISCO 派生（`FiscoAddressDerivation`）
- `fix_error_handling` 对 gas_left clamp

---

## 每个 STATE-GAP 必填字段

```markdown
### GAP-XXX: <简短标题>
- **状态面:** S1–S10
- **路径:** ETH / OpStack / Both
- **Severity:** P0 共识 / P1 参考路径 / P2 测试缺口 / Accepted
- **症状:** 何种输入下 state root / receipt / balance 会漂移
- **我方:** `file:line` — 实际行为
- **geth/op-geth:** `file:line` — 期望行为
- **Commit/Revert:** 失败时 state 是否回滚；TE 是否 apply
- **Oracle:** 已有测试名 或 建议新增 characterization 用例
- **Related ADR/Plan:** ...
```

---

## Final Report 模板

```markdown
# bcos-evm EVM 执行状态 geth·op-geth Parity 报告

## Executive Summary
- P0：可直接导致 state root / 资金不一致
- P1：ETH/Op 参考路径偏差
- P2：测试/文档缺口
- Accepted：FISCO 有意偏离

## 环境
- GETH / OPGETH commit、WORKSPACE branch

## §1 状态面 × 路径 总览矩阵（Task 0）

## §2 Gas / Balance / Nonce 差异详表（Task 1–3）

## §3 地址 / Code / Storage / Logs / SELFDESTRUCT（Task 4–7）

## §4 返回状态 vs State Commit 对照（Task 8）
（含三 TE applyStateDiff 条件表）

## §5 Access / StateDiff 写回（Task 9–10）

## §6 OpStack 专有（Task 11）

## §7 FISCO 附录（Task 12）

## §8 状态不一致风险 Top-N（按 severity 排序）

## §9 建议 characterization 测试矩阵

| 用例 ID | 状态面 | 输入要点 | 断言（state + receipt） |
|---------|--------|----------|-------------------------|

## §10 与已有审查文档交叉引用

## Appendix：mermaid 状态生命周期图（ETH included-vmerr + OpStack entry-reject 各一张）
```

---

## 证据规则

1. **STATE-GAP 必须双端行号**；纯推断标「需 state test」
2. 区分 **内存 State 突变** vs **StateDiff** vs **storage apply** 三层
3. 引用已有 JSON state test fixture 时注明路径与覆盖 EIP
4. 可选跑测试互证：

```bash
rtk ctest -R 'WarmTransactionEntry|SstoreRefund|StateRefund|NestedRevert|Eip7702|IncludedTxVmerr|DepositCreate|OpStackSettlement|FeeSettlement|CreateError'
```

---

## 禁止事项

- 不修改源码
- 不把 FISCO 专有行为标为 geth FAIL（放 Task 12）
- 不重复全量 opcode 审计 — 只追 **state 后果**
- 不脱离 commit/revert 边界空谈「逻辑不一致」
- 错误码-only 差异若不影响 state，引到 `error-handling-geth-parity-prompt.md` 即可

---

## 使用变体

### 仅 ETH 参考 + 内核

Scope：`eth/` + `ethReferenceExecute` + ETH TE；Skip Task 11；OpStack 列标 N/A。

### 仅 OpStack

Scope：`opstack/` + `opStackExecute` + OpStack TE；基准 OPGETH；ETH 内核仅作 shared 依赖说明。

### 审查 + GST/EEST fixture

追加：

```
对 bcos-evm/test/fixtures/state/imported/st*.json 抽样 10 个，
列出我方已覆盖与 geth state test 预期未覆盖项。
```

### 与 error-handling 提示词联用

1. 先跑 `error-handling-geth-parity-prompt.md` Task 1–3（taxonomy + status 映射）
2. 再跑本文 Task 1–12（state 后果）
3. 合并报告时：**error-only 项放附录，state 影响项进 P0/P1**

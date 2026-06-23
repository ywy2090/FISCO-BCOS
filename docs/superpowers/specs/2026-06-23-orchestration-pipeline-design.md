# 三条 executeVia* 编排收敛为深 Module — 设计文档

**日期:** 2026-06-23  
**范围:** `bcos-evm` 编排层收敛(架构候选 1)  
**词汇:** 取自 `codebase-design`(module / interface / implementation / depth / seam / adapter / leverage / locality)  
**关联:** ADR-005 · ADR-015 · ADR-016 · capability-matrix.md · 新增 ADR-019

---

## 1. 问题陈述

三条链编排入口平行实现同一「事务级 EVM 执行」管线,形成浅 module(接口≈实现),且已暴露正确性缺陷:

| # | 位置 | 角色 |
| --- | --- | --- |
| 1 | `eth/ExecuteViaEth.cpp` | Ethereum 参考路径 |
| 2 | `bcos/ExecuteViaHost.cpp` | FISCO 生产路径 |
| 3 | `opstack/OpStackExecuteViaHost.cpp` | OP Stack 生产路径 |

**重复块(locality 失效):**

| 重复 | Eth | Fisco | OpStack |
| --- | --- | --- | --- |
| null 校验 + `State` 构建 + `txProps` | ✅ | ✅ | ✅ |
| `ExecuteMessageInput` 13 字段逐字段填充 | ✅ | ✅ | ✅×2(deposit/normal) |
| `adoptResult` 匿名副本 | ✅ L20 | ✅ L67 | ✅ L16 |
| EIP-7623 intrinsic 预扣 | `message.gas -=` | `message.gas -=` | `txData.m_message.gas -=` |
| 传入 `executeMessage` 的 message | 已扣减 `message` | 已扣减 `message` | **`input.message`(未扣减)** |

**OpStack 缺陷(`OpStackExecuteViaHost.cpp`):**

- `executeEntryChecks` 在 `txData.m_message.gas` 上扣 intrinsic(L61)
- `executeMessage` 传入 `input.message`(L171 deposit / L252 normal),与扣减对象分裂
- Eth/Fisco 均在本地 `message` 上扣减后传入,行为一致

**`executeEntryChecks` 不等于 `debitIntrinsicGas`:** 现有 OpStack 函数还 bundled `canTransfer`、`executeEntryFloorDataGasCheck`,且校验顺序与 Eth/Fisco 不同(balance/floor **先于** debit)。不可整包收进核,须按 §5 拆分。

**后果:** 编排不变量「进内核的 `message.gas` = intrinsic 预扣后的 gas」无单点 enforcement;复制粘贴导致 drift 与 bug。

## 2. 目标与非目标

### 目标

1. 在 `eth/orchestration/` 引入深 module `runOrchestration`,固定管线至 settlement snapshot 采集 + 共享 gas 数学。
2. 三条 `executeVia*` 退化为薄 wrapper:映射输入 → 填 `OrchestrationHooks` → 调 pipeline → 映射输出。
3. **同一 PR** 修复 OpStack `message`/`intrinsic` 不同步(结构性:单一 `OrchestrationContext::message` 所有权)。
4. 合并三份 `adoptResult` 为 `adoptEvmcResult`;`ExecuteMessageInput` 组装单点。
5. 行为等价测试全绿(Eth/Fisco);OpStack 在修 bug 处允许文档化的预期 gas delta。

### 非目标

- 不合并 `ExecuteViaEthInput` / `ExecuteViaHostInput` / `OpStackExecuteViaHostInput` 为单一输入 struct(接口保持链特有)。
- 不把 fee 路由(L1 fee、operator fee、`buyGas`/`refundGas`、FISCO 21000)收进核(守 ADR-005)。
- 不把链状态机(`checkpoint`/`commit`/`revert`、deposit mint、`GasPoolReturnGuard`)收进固定管线(守 ADR-005;见 §5.2)。
- 不重构 `executeMessage` 内核本身。
- 不碰 revision 门控(已在 ADR-018 单独交付)。

### 已确认决策

| # | 议题 | 结论 |
| --- | --- | --- |
| Q1 | 首要目标 | **B:** 收敛 + 同 PR 修 OpStack gas 同步 |
| Q2 | 模块边界 | **B:** 核包到 settlement(adopt + snapshot) |
| Q3 | Settlement 范围 | **A:** 共享 gas 数学进核;fee 路由留 hook/wrapper |
| Q4 | 实现路径 | **方案 1:** `OrchestrationPipeline` + `OrchestrationHooks` |
| Q5 | `debitIntrinsicGas` 边界 | **核只做 portable intrinsic 相位**;OpStack floor/balance 进 `preDebitEntry` |
| Q6 | 状态机 / RAII | **wrapper 外**;核 catch 仅 `has_checkpoint()` 安全网 |
| Q7 | 异步 fee | **`buyGas`/`refundGas` 在 wrapper**;管线体同步;`runOrchestration` 为同步 `void` |
| Q8 | intrinsic 模式 | `IntrinsicDebitMode` 显式区分 `None`/`AuthOnly`/`Eip7623`/`OpStackEntry` |
| Q9 | intrinsic 失败 | `debitIntrinsicGas` 只返回结构化失败;链侧 `mapIntrinsicFailure` 映射最终 `EVMCResult` |
| Q10 | context 构造 | `OrchestrationContext` 构造即有效、不可默认构造,构造时绑定 `StateView` |
| Q11 | 异常映射 | `mapException` 接 `std::exception_ptr`;链侧在本 `.cpp` rethrow/catch 自己的异常类型 |
| Q12 | catch 范围 | `runOrchestration` 的 `try/catch` 覆盖步骤 ②–⑪;① validate 在外 |
| Q13 | early-exit | `OrchestrationExitKind` 标记 early-exit;pipeline 不自动 post-settle |
| Q14 | state/message 所有权 | `ctx.state` 为唯一 `State` owner;OpStack `txData.m_state = &ctx.state`(借用),`txData` 不再持可 debit 的 `message` |
| Q15 | extension 生命周期 | **删 `buildExtension` hook**;wrapper 在 `runOrchestration` 前预构造 `HostExtension`(绑定 `&ctx.state`),存 `ctx.extension`(borrow);`ctx` 不可拷贝/移动 |
| Q16 | gasPrice 归属 | `gasPrice` 进 `ctx` 构造参数;`buildExecuteMessageInput` 读 `ctx.gasPrice`;Eth `preExecute`/OpStack wrapper(`buyGas` 后)可覆写 |
| Q17 | snapshot 条件 | `captureSettlementSnapshot` 仅 `intrinsicPolicy.mode == Eip7623` 填 `ctx.snapshot`;OpStack settlement 数学留 `opstack/`,经 `postSettle` 调用(不进核 ⑨) |
| Q18 | 输出映射 | `stateDiff`/`logs` 终映射归 wrapper;OpStack 必须 `ctx.state.build_diff()` 且在所有 wrapper 外 state 改动**之后** |
| Q19 | settlement/refund 矩阵 | `buyGas` 成功后任何 `exitKind` 都必 settle+refund+`build_diff`;entryChecks 拆分后保持 intrinsic→canTransfer→floor 失败优先级 |
| Q20 | 失败范式 | `preKernel` hook 允许 state-mutating 且失败走 `throw`;异常路径 `state.revert()`+checkpoint 归 `mapException` hook/wrapper,核绝不碰 state 回滚 |

## 3. 架构(方案 1: OrchestrationPipeline)

```text
  executeViaEth ──────► runOrchestration(sync hooks)
  executeViaHost ─────► runOrchestration(sync hooks)

  opStack normal ─────► gasPoolSubGas
                     ─► GasPoolReturnGuard (armed = buyGas ok)
                     ─► co_await buyGas → ctx.gasPrice = effectiveGasPrice
                     ─► runOrchestration(normal hooks)
                     ─► [armed 后任何 exitKind] settlement
                     ─► co_await refundGas + receipt meta
                     ─► ctx.state.build_diff()  // 尾部,捕获 fee/refund

  opStack deposit ────► mint + checkpoint          (wrapper)
                     ─► runOrchestration(deposit hooks)
                     ─► commit/revert + nonce + pool (wrapper)
                     ─► ctx.state.build_diff()  // 尾部

                    ┌─────────────────────────────────────┐
                    │   eth/runOrchestration()            │──► executeMessage
                    │   OrchestrationContext              │     (kernel)
                    │   (single message owner)            │
                    └──────────┬──────────────────────────┘
                               │ OrchestrationHooks (sync)
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
         Eth hooks        Fisco hooks      OpStack hooks
    (1559 caps,         (auth, 21000,     (preDebitEntry:
     included-tx         balance xfer,      balance+floor)
     normalize)          revert logs)      postSettle: gas math only
```text

**seam 纪律(ADR-005):** 管线与共享 gas 数学在 `eth/`;`eth/` 不 `#include bcos/` 或 `opstack/`。链差异经 `OrchestrationHooks` 注入;`HostExtension` 由 wrapper 在 `runOrchestration` 前预构造(绑定 `&ctx.state`),以 borrow 指针经 `ctx.extension` 传入内核(无 `buildExtension` hook)。协程 fee 与状态机边界在 wrapper。

## 4. 组件

| module | interface | implementation |
| --- | --- | --- |
| `OrchestrationContext`(新) | 可变执行帧 | 构造即有效、不可默认构造、**不可拷贝/移动**;**唯一 `State` owner**;独占 `evmc_message message`;持有 `State`, `txProps`, `gasPrice`, `extension`(borrow), `snapshot`, `kernelOutput`, `evmcResult` |
| `OrchestrationHooks`(新) | 链侧 **同步** hook 集 | `prepareMessage`, `preExecute`, `preDebitEntry`, `preKernel`, `tuneKernelInput`, `postAdopt`, `postSettle`, `mapIntrinsicFailure`, `mapException` + `intrinsicPolicy`(无 `buildExtension`) |
| `runOrchestration(ctx, hooks)`(新) | `void` | 固定 12 步同步管线(§5.1);外层 `executeVia*` 保持 coroutine |
| `debitIntrinsicGas(ctx, policy)`(新) | 在 `ctx.message` 上预扣 | 仅 portable intrinsic/OpStack entry gas 数学;返回结构化 outcome,不构造最终链侧错误 |
| `buildExecuteMessageInput(ctx)`(新) | `→ ExecuteMessageInput` | 13 字段单点组装;读 `ctx.gasPrice` 与 `ctx.extension`(wrapper 预构造) |
| `adoptEvmcResult(result, hashImpl)`(新,公开) | `evmc::Result → EVMCResult` | 替代三份匿名 `adoptResult` |
| `captureSettlementSnapshot(ctx, output)`(新) | 填 `TxGasSettlementSnapshot` | **仅 `intrinsicPolicy.mode == Eip7623`** 填 `gasLimit`/`calldata`/`evmGasRefund`;其余 mode no-op |
| `normalizeIncludedTxVmerr(result, depth)`(新) | Eth ADR-015 | 由 Eth hook `postAdopt` 调用 |
| `executeViaEth` / `executeViaHost` / `opStackExecuteViaHost`(改) | 对外 API 不变 | 薄 wrapper + OpStack 异步/fee 外圈 |

### `OrchestrationContext` 字段

`OrchestrationContext` 必须通过构造函数绑定 `state::StateView`、初始 `evmc_message`、`RevisionConfig` 和 `gasPrice`;不提供默认构造;**显式 `= delete` 拷贝/移动**(它是唯一 `State` owner 的 owning frame)。构造时捕获 `originalGasLimit`,并调用 `execution::setWarmDestinationFromKind(txProps, message.kind)`。`extension` 在构造后由 wrapper 写入(绑定 `&ctx.state`)。

| 字段 | 说明 |
| --- | --- |
| `evmc_message message` | **唯一可变 message**;自输入拷贝;intrinsic 扣减与 `executeMessage` 共用 |
| `int64_t originalGasLimit` | 扣减前 gasLimit(snapshot / settlement) |
| `state::State state` | 自 `StateView` 构建;**ctx 为唯一 owner**;OpStack `txData.m_state = &state`(借用),不再有第二份可 debit message |
| `intx::uint256 gasPrice` | 构造注入;`buildExecuteMessageInput` 读取;Eth `preExecute`(1559 normalize)/OpStack wrapper(`buyGas` 后 `effectiveGasPrice`)可覆写 |
| `HostExtension* extension` | wrapper 预构造并绑定 `&state`;**borrow,非 owning**;⑥ `buildExecuteMessageInput` 透传内核 |
| `TransactionProperties txProps` | 核内 `setWarmDestinationFromKind` |
| `RevisionConfig revisionConfig` | 传入 `executeMessage` 的 profile |
| `gas::TxGasSettlementSnapshot snapshot` | settlement 采集;**仅 `Eip7623` mode 填** |
| `uint64_t floorDataGas` | OpStack 专用;`preDebitEntry` 写入,⑨⑪ settlement 读取;Eth/Fisco 忽略 |
| `ExecuteMessageOutput kernelOutput` | 内核原始输出 |
| `EVMCResult evmcResult` | adopt 后结果 |
| `bool earlyExit` | preExecute / preDebitEntry / intrinsic OOG 早返标记 |
| `OrchestrationExitKind exitKind` | `None`/`PreExecuteRejected`/`PreDebitRejected`/`IntrinsicRejected`/`KernelCompleted`/`ExceptionMapped` |

链特有输出(`EthExecutionContext`, `FiscoExecutionContext`, `OpStackReceiptMeta`, `gasUsed`)与各链 `OpStackTxExecutionData` 由各 wrapper 从 `ctx` 拷贝或并行维护,不膨胀核 struct。

### `OrchestrationHooks` 职责矩阵

| Hook | 时机 | Eth | Fisco | OpStack |
| --- | --- | --- | --- | --- |
| `prepareMessage` | ② | no-op | `deriveMessage` | no-op |
| `preExecute` | ③ | `ethPreCheck` + 1559 caps | `authPort::checkAuth` | `opStackPreCheck` only |
| `preDebitEntry` | ③½ | no-op | no-op | `canTransfer` + `executeEntryFloorDataGasCheck` → `ctx.floorDataGas` |
| `intrinsicPolicy.mode` | ④配置 | `None`/`AuthOnly`/`Eip7623` | `None`/`Eip7623` | `OpStackEntry` |
| `mapIntrinsicFailure` | ④失败 | `OutOfGasLimit`, gas_left=0 | `makeErrorEVMCResult(... OutOfGas ..., reason, fixErrorHandling)` | `OutOfGasLimit`, gas_left=0 |
| `preKernel` | ⑤ | `canTransfer`(只读) | **state-mutating:** 21000 debit、empty-code、`maybeTransferValue`;失败走 `throw` | gas pool `subGas`(若未在 wrapper 做) |
| `tuneKernelInput` | ⑥ | `fixStorage=true` | `fixStorage/fixNonceInit`, `revisionConfig.eth()` | 同 Eth |
| `postAdopt` | ⑩ | `normalizeIncludedTxVmerr` | CREATE address 修补 | no-op |
| `postSettle` | ⑪ | no-op | `fix_revert_logs` on failure | `postExecuteGasSettlement` only |
| `mapException` | catch | rethrow `std::exception_ptr` 后简单映射 | 在 Fisco `.cpp` rethrow/catch `OutOfGas`/`NotEnoughCash`/`NotFoundCodeError`/`std::exception` + `if (ctx.state.has_checkpoint()) revert()` | rethrow 后委托 `opTxExecutor` |

> `HostExtension` 无 hook:wrapper 在 `runOrchestration` 前构造(`EthHostExtension`/`FiscoHostExtension`+deps/`OpHostExtension`,均绑定 `&ctx.state`)并写入 `ctx.extension`。`preKernel` 允许改 `ctx.state`(Fisco 转账),失败 `throw` 由 ②–⑪ catch 兜住(见 §7);`mapException` 同时负责链特有 `state.revert()` 与 checkpoint 语义,核 pipeline 绝不直接回滚 state。

### Intrinsic debit mode

| Mode | 语义 | 使用方 |
| --- | --- | --- |
| `None` | 不预扣 intrinsic/auth | Eth/Fisco 非 7623 且无 auth |
| `AuthOnly` | 仅校验并扣 auth tuple cost | Eth 非 7623 但有 EIP-7702 auth |
| `Eip7623` | EIP-7623 gasLimit minimum/calldata 校验 + `preExecutionDebit` + auth | Eth 7623;Fisco web3+7623 |
| `OpStackEntry` | `availableGas >= intrinsicDebit` + subtract;floor/balance 已在 ③½ | OpStack normal/deposit |

`debitIntrinsicGas` 返回 `DebitIntrinsicGasOutcome{ok, failure, gasLeftOnFailure, debitAmount}`。失败枚举至少包含 `GasLimitMinimum`, `CalldataOutOfGas`, `AuthTupleOutOfGas`, `OpStackIntrinsicOutOfGas`;最终 `EVMCResult` 只能由 `mapIntrinsicFailure` 构造。

**校验顺序纪律(不可统一):**

| 链 | balance / floor | intrinsic debit |
| --- | --- | --- |
| Eth | ⑤ `preKernel`(`canTransfer`) **在 debit 之后** | ④ 核 |
| Fisco | ⑤ `preKernel`(21000、xfer) **在 debit 之后** | ④ 核(条件 eip7623) |
| OpStack | ③½ `preDebitEntry` **在 debit 之前** | ④ 核(仅 subtract) |

**OpStack deposit:** wrapper 层 `if (isDepositTx)` 选用 `OrchestrationProfile::OpStackDeposit` hook 集;mint/checkpoint/commit 在 wrapper;与 normal 共享管线 ③½–⑨。

## 5. 数据流

### 5.1 固定管线(核内,12 步)

```text
message₀ (输入拷贝 → ctx.message; originalGasLimit 捕获)
  │
  ├─① validate(stateView/vm/hashImpl) — 缺失则 throw (try/catch 外)
  ├─② hooks.prepareMessage(ctx)                  ┐
  ├─③ hooks.preExecute(ctx) → earlyExit?          │
  ├─③½ hooks.preDebitEntry(ctx) → earlyExit?      │ try/catch 覆盖 ②–⑪
  ├─④ debitIntrinsicGas(ctx, hooks.intrinsicPolicy) → earlyExit?
  ├─⑤ hooks.preKernel(ctx)        // 可改 ctx.state(Fisco xfer);失败 throw
  ├─⑥ input = buildExecuteMessageInput(ctx)   // 读 ctx.gasPrice + ctx.extension
  │     hooks.tuneKernelInput(input)
  ├─⑦ kernelOutput = executeMessage(input)   // input.message == ctx.message
  ├─⑧ ctx.evmcResult = adoptEvmcResult(kernelOutput.result, hashImpl)
  ├─⑨ captureSettlementSnapshot(ctx, kernelOutput)  // 仅 Eip7623 mode
  ├─⑩ hooks.postAdopt(ctx)
  └─⑪ hooks.postSettle(ctx)                      ┘
```text

**核心不变量:** 步骤④修改的 `ctx.message` 即步骤⑥⑦使用的 `message` 引用。OpStack 删除 `txData.m_message` 与 `input.message` 双轨。

**early-exit 纪律:** ③/③½/④ 失败只设置 `ctx.earlyExit=true` 与 `ctx.exitKind`,并填 `ctx.evmcResult`;pipeline **不自动执行** ⑨–⑪。Eth/Fisco wrapper 直接返回;OpStack wrapper 根据 `exitKind` 决定是否执行 settlement/refund/deposit failure 分支。

**`debitIntrinsicGas` 边界(核):**

| 包含 | 不包含(链 hook / wrapper) |
| --- | --- |
| `computeTxIntrinsicGas` + auth tuple | `canTransfer` / `maybeTransferValue` |
| EIP-7623 gasLimit minimum / calldata 校验 | `executeEntryFloorDataGasCheck`(OpStack → ③½) |
| `ctx.message.gas -=` debit | `buyGas` / `refundGas` / L1 fee |
| 结构化失败原因 → `mapIntrinsicFailure` | `checkpoint` / `commit` / `revert` |

### 5.2 Wrapper 外职责(不进固定管线)

| 职责 | 链 | 位置 |
| --- | --- | --- |
| `co_await buyGas` / `co_await refundGas` | OpStack normal | wrapper,管线前后 |
| `GasPoolReturnGuard` RAII | OpStack normal | wrapper 包裹 buyGas→refundGas |
| `gasPoolSubGasHook` | OpStack normal | wrapper,`buyGas` 前 |
| deposit mint + `state.checkpoint()` | OpStack deposit | wrapper,管线前 |
| commit / revert / nonce bump / `returnDepositPoolGas` | OpStack deposit | wrapper,管线后 |
| L1 fee / operator fee / receipt meta | OpStack normal | wrapper,`refundGas` 后 |
| Eth 1559 caps | Eth | ③ `preExecute` |
| Fisco `gas_left < 0` 后置检查 | Fisco | wrapper,管线后 |

**fee 路由(§5.2,不进核):** Eth `normalizeGasCaps`/`resolveEffectiveGasPrice`;Fisco `BALANCE_TRANSFER_GAS`;OpStack `buyGas`/`refundGas`/L1/operator/gas pool。

### 5.3 输出映射契约(归 wrapper,核不产出终值)

核 pipeline **不**生产最终 `stateDiff`/`logs`;`ctx.kernelOutput` 仅持内核原始 `logs`/`stateDiff`/`gasRefund`。终值映射由各 wrapper 负责:

| 产物 | Eth | Fisco | OpStack |
| --- | --- | --- | --- |
| `logs` | `convertLogs(ctx.kernelOutput.logs)` | 同;失败按 `fix_revert_logs` clear | 同 |
| `stateDiff` | `move(ctx.kernelOutput.stateDiff)` | **仅 success** `move(...)` | **`ctx.state.build_diff()`**,在 `buyGas`/`refundGas`/mint/nonce **全部完成后** |

> OpStack 必须 `build_diff()` 重算:`buyGas`/`refundGas`/deposit mint 都在 pipeline **外**改 `ctx.state` 余额,若误用 `kernelOutput.stateDiff` 会**丢失全部 fee 余额变更**(receipt/diff 错账),且简单 case 测试可能仍绿。

### 5.4 OpStack settlement/refund 责任矩阵(硬契约)

**不变量:** `GasPoolReturnGuard.armed`(= `buyGas` 成功)之后的**任何** `exitKind` 都必须 `settlement + refundGas + build_diff`。

**normal-tx:**

| 失败/退出点 | `buyGas` 已执行 | wrapper 必做 |
| --- | --- | --- |
| `opStackPreCheck`(buyGas 前) | 否 | 直接 return,**不** settle/refund |
| `gasPoolSubGasHook`(buyGas 前) | 否 | 直接 return |
| `buyGas` 本身失败 | 失败 | 直接 return |
| ③½ floor / ④ intrinsic / `canTransfer`(buyGas 后) | 是 | settlement + `refundGas` + `build_diff` |
| `KernelCompleted` | 是 | settlement + `refundGas` + `build_diff` |

**deposit-tx(无 buyGas,独立分支):**

| 退出点 | wrapper 必做 |
| --- | --- |
| entryChecks 失败 | `revert` + `nonce++` + `gasUsed=gasLimit` + `build_diff` + `returnDepositPoolGas` |
| Kernel success | settlement + `nonce++` + `commit` + `build_diff` + `returnDepositPoolGas` |
| Kernel fail | settlement + `revert` + `nonce++` + `build_diff` + `returnDepositPoolGas` |

## 6. OpStack bug 修复与等价性

### 修复

| 修复前 | 修复后 |
| --- | --- |
| `executeEntryChecks` 整包 + 扣 `txData.m_message.gas` | ③½ `preDebitEntry` + ④ `debitIntrinsicGas` 扣 `ctx.message.gas` |
| `executeMessage({.message = input.message})` | `executeMessage({.message = ctx.message})` |
| `txData.m_gasLimit = input.message.gas` 与执行 gas 不一致 | `originalGasLimit` 扣减前捕获;settlement 用一致值 |
| `executeEntryChecks` 失败仍走 settlement 的隐式分支 | `earlyExit` 或 wrapper 显式分支;行为与现网对齐 |

### 等价性边界

| 路径 | 预期 |
| --- | --- |
| Eth | 重构前后 `ExecuteViaEthFixture`、1559/7623 测试输出一致 |
| Fisco | `ExecuteViaHostSmoke`、auth/7623/6780 测试一致 |
| OpStack | 非 intrinsic 路径一致;**intrinsic 路径 gas 修正**(此前 `executeMessage` 收到未扣减 gas) |
| OpStack 顺序 | ③½ balance+floor → ④ debit 顺序保持不变 |
| OpStack 失败优先级 | intrinsic→canTransfer→floor 判定顺序保持;included-tx `status_code`(`OutOfGasLimit`/`InsufficientFunds`)不漂移 |
| OpStack stateDiff | 必含 wrapper 外 `buyGas`/`refundGas`/mint 余额变更(经 `build_diff()`);误用 `kernelOutput.stateDiff` 即回归 |

新增 `OpStackIntrinsicGasSyncTest`:通过 test-only `executeMessage` spy seam 直接断言传入 `executeMessage` 的 `message.gas == originalGasLimit - intrinsicDebit`。该测试 target 不链接普通 `bcos-evm-op`,而是带 `BCOS_EVM_TESTING` 单独编译一份 OpStack 源文件,避免污染生产库。

## 7. 错误处理

| 场景 | 核行为 | 链 hook / wrapper |
| --- | --- | --- |
| null `stateView`/`vm`/`hashImpl` | `throw std::invalid_argument` | — |
| intrinsic OOG | `debitIntrinsicGas` 返回结构化失败;`exitKind=IntrinsicRejected` | `mapIntrinsicFailure` |
| `preExecute` / `preDebitEntry` 拒绝 | hook 填 `ctx.evmcResult`, return(earlyExit) | hook |
| `preKernel` state-mutating 失败(Fisco `maybeTransferValue`/21000/empty-code) | `throw` → ②–⑪ catch | `mapException` 映射 + revert |
| 内核 / hook 异常(②–⑪) | 仅置 `exitKind=ExceptionMapped` 并调 hook;**核绝不 revert state** | `mapException(ctx, std::exception_ptr)` 重抛映射 + `if has_checkpoint() revert()` |
| Fisco `NotFoundCodeError` | catch → `mapException` | static/delegatecall 分支(原 status 分流保留) |
| Fisco `gas_left < 0` | — | Fisco wrapper 后置检查 |
| OpStack `buyGas` 失败 | 不进入管线 | wrapper 早返 |
| OpStack deposit 失败 | wrapper `revert` + nonce | wrapper |

核不 include 链特有异常头;Fisco 的 `fixErrorHandling` 语义由 `mapException` 保留。

## 8. 文件布局

```text
bcos-evm/eth/orchestration/
  OrchestrationContext.h
  OrchestrationHooks.h
  OrchestrationPipeline.h
  OrchestrationPipeline.cpp
  adoptEvmcResult.h
  debitIntrinsicGas.h
  buildExecuteMessageInput.h
  captureSettlementSnapshot.h
  normalizeIncludedTxVmerr.h   // Eth ADR-015;Eth hook 调用
```

修改(变薄,不扩 API):

- `eth/ExecuteViaEth.cpp` — Eth hooks + `runOrchestration`
- `bcos/ExecuteViaHost.cpp` — Fisco hooks + wrapper;删本地 `adoptResult`
- `opstack/OpStackExecuteViaHost.cpp` — OpStack hooks + async/fee wrapper;删 `executeEntryChecks` 整包

OpStack 的 `executeEntryFloorDataGasCheck` 留在 `opstack/`;经 `preDebitEntry` hook 调用,不迁入 `eth/`。

## 9. 测试

| 测试 | 目的 |
| --- | --- |
| 既有表征测试全绿 | Eth/Fisco/OpStack smoke + 7623/6780/2537 回归 |
| `OrchestrationPipelineTest`(新) | 核级:空 hook + mock,验证 ③½→④→⑦ debit→execute 不变量 |
| `OpStackIntrinsicGasSyncTest`(新) | OpStack:test-only spy 捕获 `ExecuteMessageInput.message.gas`;测试 target 带 `BCOS_EVM_TESTING` 单独编译 OpStack 源 |
| `OpStackPreDebitOrderTest`(新) | OpStack:balance+floor 在 debit 前执行(顺序回归) |
| 重构前后 fixture diff | Eth/Fisco 零 delta;OpStack intrinsic 路径文档化 delta |

## 10. 文档与治理

- 新增 `bcos-evm/docs/adr/019-orchestration-pipeline.md`
- 更新 `capability-matrix.md`:三条 TE baseline 路径注记 "via `runOrchestration`"
- 更新 ADR-005 §2:orchestration 共享管线位置 + wrapper 外 fee/状态机边界

## 11. 验收标准

- [ ] `runOrchestration` 落 `eth/orchestration/`,为唯一编排管线;返回同步 `void`。
- [ ] 三份 `adoptResult` 删除;`adoptEvmcResult` 单点。
- [ ] `ExecuteMessageInput` 组装单点;三处逐字段拷贝消除。
- [ ] `OrchestrationContext` 不可默认构造;构造时绑定 `StateView` 并捕获 `originalGasLimit`。
- [ ] `IntrinsicDebitMode` 存在;`debitIntrinsicGas` 不直接构造链最终 `EVMCResult`。
- [ ] `mapIntrinsicFailure` 与 `mapException(std::exception_ptr)` 存在;Fisco 在本 `.cpp` rethrow/catch `NotFoundCodeError`。
- [ ] `OrchestrationExitKind` 存在;early-exit 不自动 post-settle。
- [ ] `preDebitEntry` 存在;OpStack floor/balance 不进 `debitIntrinsicGas`。
- [ ] OpStack 传 `ctx.message` 进 `executeMessage`;`OpStackIntrinsicGasSyncTest` 绿。
- [ ] `buyGas`/`refundGas`/`GasPoolReturnGuard`/deposit 状态机在 wrapper,不在管线步骤表。
- [ ] `OrchestrationContext` 显式 `= delete` 拷贝/移动;`gasPrice` 为构造参数;为唯一 `State` owner(OpStack `txData.m_state = &ctx.state`)。
- [ ] 无 `buildExtension` hook;`HostExtension` 由 wrapper 预构造并经 `ctx.extension`(borrow)透传。
- [ ] `captureSettlementSnapshot` 仅 `Eip7623` mode 填 `ctx.snapshot`;OpStack settlement 数学在 `postSettle`/`opstack/`。
- [ ] `logs`/`stateDiff` 终映射归 wrapper;OpStack 经 `ctx.state.build_diff()`(wrapper 外 state 改动后)。
- [ ] OpStack `buyGas` 成功后任何 `exitKind` 均 settle+refund+`build_diff`(§5.4 矩阵)。
- [ ] `mapException` 负责异常路径 `state.revert()`+checkpoint;核 pipeline 不回滚 state。
- [ ] entryChecks 拆分后保持 intrinsic→canTransfer→floor 失败优先级。
- [ ] Eth/Fisco 表征测试零行为变更。
- [ ] `eth/` 无新增 `bcos/`/`opstack/` include。
- [ ] ADR-019 + capability-matrix 同 PR 更新。

## 12. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| `OrchestrationHooks` 膨胀为 God-interface | hook 按管线阶段命名;`preDebitEntry` 隔离 OpStack 顺序;review 门禁 |
| 统一 balance 顺序破坏 OpStack | ③½ 显式阶段;顺序纪律表(§4) |
| OpStack deposit/normal 分支回归 | 分 profile hook 集 + wrapper scope;deposit 既有测试全跑 |
| Fisco 异常语义丢失 | `mapException(std::exception_ptr)` 在 Fisco `.cpp` rethrow/catch 原异常类型 |
| OpStack gas sync 测试假阳性 | test-only `executeMessage` spy 直接捕获 `ExecuteMessageInput.message.gas` |
| 大 PR review 困难 | 实现计划分 5 绿步(见 §13);每步可独立 review |

## 13. 实施顺序(实现计划输入,保持每步全绿)

1. **Task 1A helper:** `adoptEvmcResult` + `IntrinsicDebitMode` + `debitIntrinsicGas`(结构化 outcome,不构造最终错误);测试绿。
2. **Task 1B OpStack bug fix:** `preDebitEntry` 等价逻辑 + test-only spy seam + **改传 debited message**;测试绿。
3. **Task 2 管线 + Eth:** `OrchestrationContext`(不可默认构造、`= delete` 拷贝/移动、`gasPrice` 构造参数、`extension` borrow、无 `buildExtension`) + `runOrchestration`(同步 `void`,②–⑪ catch,⑨ 仅 `Eip7623`) + Eth wrapper(预构造 `EthHostExtension`);测试绿。
4. **Task 3 Fisco + OpStack 迁移:** 填各自 hooks;`mapIntrinsicFailure`/`mapException(std::exception_ptr)`(含 `state.revert()`+checkpoint);`preKernel` state-mutating throw 范式;OpStack wrapper 外圈 buyGas/refundGas/deposit、§5.4 settlement/refund 矩阵、`ctx.state.build_diff()` 尾部映射;全量表征测试绿。
5. **Task 4 文档:** ADR-019 + matrix + ADR-005。

> 硬依赖:Task 1B 的 OpStack message 修复必须先于 Task 2,否则管线测试会固化错误行为。Task 1B 须同时落地 ③½ 顺序,不可只挪 debit 而遗留 `executeEntryChecks` 整包。

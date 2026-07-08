# bcos-evm 模块设计文档

**版本：** 2026-07-08 · **分支：** `feat-evm-refactor`

**一句话：** 将 FISCO-BCOS 的 EVM 执行从 executor 耦合代码中抽离为独立模块；**共享内核**（`bcos-evm-eth`）统一提供 `stateTransitionExecute` 8 步交易管线、`innerExecute`/`runCallFrame` 帧执行、evmone Host、`RevisionConfig` EIP 开关与 `PrecompileRouter` 标准预编译路由，三链（FISCO / ETH 参考 / OP Stack）仅在外壳层注入 Hooks 与 Port。

**不在范围内：** 区块调度、共识、TxPool、legacy `bcos-executor`/DAG/`HostContext`、JSON-RPC receipt 暴露；这些由 TE/scheduler 负责，`bcos-evm` 只处理单笔交易的 EVM 执行与链编排。

---

## 1. 为何构建这个模块

重构前，EVM 执行逻辑散落在 `bcos-executor`、`transaction-executor` 和各类 Host 适配里，带来四个根本问题：

1. **标准语义与链定制纠缠** — FISCO 权限表、精编译、21000 gas 等与标准 EVM opcode 语义混在同一调用栈。
2. **编译期强耦合** — 执行层直接 `#include` executor 精编译，无法独立测试与合规回归。
3. **多链需求缺乏边界** — 同一仓库要跑 FISCO、ETH 参考、OP Stack，但没有清晰的「共享什么、定制什么」契约。
4. **EIP 开关难以审计** — revision 和 feature flag 在多处重复推导，旧链向前兼容无法系统化证明。

`bcos-evm` 的解法：**抽出一个与链无关的标准 EVM 内核，把三条链的差异放到可注入的外壳里**。

---

## 2. 核心设计原理

**硬规则：** `eth/` 永远不能 `#include` `bcos/` 或 `opstack/`。链差异只能**注入**，不能**渗透**进内核。

### 2.1 一个内核，三套外壳

内核（`eth/` 中 L1–L3 部分）负责 evmc 语义下的状态变更；链外壳（L4：`eth/apply/` ETH 参考、`bcos/`、`opstack/`）负责预检、扣费、receipt 与链专属 CALL 目标。详见 §3.1。

### 2.2 四类扩展点

| 扩展点 | 触发时机 | 典型用途 |
| --- | --- | --- |
| `StateTransitionHooks` | pipeline 第 1–5 步（EVM 入口前） | message 规范化、规则校验、intrinsic 策略、余额/转账 precheck |
| `StateTransitionErrorPolicy` | intrinsic 失败、异常 catch、EVM 返回后、pipeline 出口 | 异常 → `EVMCResult` 映射、included-vmerr 归一化、链特有 receipt 语义 |
| `EvmHostHooks` | evmone 调用树内部（`runCallFrame`） | selfdestruct、value transfer、SSTORE refund、CREATE nonce |
| `ChainCallTargetPort` | CALL 分类与 dispatch | FISCO 精编译、OP L1Block 等链专属目标 |

`StateTransitionHooks` 决定「能不能跑、怎么跑」；`StateTransitionErrorPolicy` 决定「失败了怎么对外呈现」（`TransactionStatus`、`gas_left`、revert logs、state revert）。二者成对经 `*StateTransitionBindings::bind()` 注入；详见 §3.8。

链外壳通过 `*StateTransitionBindings::bind()` 填充 hooks/errorPolicy；Host 通过 `FiscoEvmHostHooks` 等子类或 `nullptr`（OP）注入。

### 2.3 Port 依赖倒置

`bcos-evm` 只定义纯虚接口 **`AuthPort`**、**`ChainCallTargetPort`**。FISCO 精编译 dispatch 由 TE 的 `ExecutorPrecompileAdapter` 实现并注入；`FiscoChainCallTargetAdapter` 负责 classify 与路由。`eth/` 内核零 executor include；`bcos/` 策略层仍有少量 TE 头文件依赖待清理。

### 2.4 EIP 开关单一推导

`RevisionConfig`（14 bool + fork 参数）由 `revisionConfigFromRevision(rev)` 唯一推导；FISCO 对 A 类 EIP 再用 `Features::Flag` 掩码。预编译 warm 与 dispatch 共用 `PrecompileActive.h`。

### 2.5 深 module

- **`stateTransitionExecute`** — 8 步 canonical pipeline（normalize → rules → gasAffordable → intrinsic → canTransfer → invoke → adopt → finalizeGasUsed；RAII `onComplete`）
- **`innerExecute`** — tx 级 warm、7702 授权、nonce bump；`depth==0` 时进入 TopLevel 帧
- **`runCallFrame`** — 帧语义由显式 `FrameScope`（TopLevel/Nested）驱动，不隐式依赖 depth

`ctx.message` 是 intrinsic gas 扣减的**唯一可变 owner**。

---

## 3. 分层模型

### 3.1 总览与 CMake 对应

概念分层与物理库对照：

| 概念层 | 目录 / 目标 | 说明 |
| --- | --- | --- |
| L4 链外壳 | `bcos/` → `bcos-evm-bcos` | FISCO 生产（`bcos-evm` 默认 alias） |
| L4 链外壳 | `opstack/` → `bcos-evm-op` | OP Stack |
| L4 链外壳 | `eth/apply/` + `eth/settlement/` | ETH 参考路径（与内核**同库** `bcos-evm-eth`） |
| L3 共享编排 | `eth/kernel/state-transition/` | `stateTransitionExecute` |
| L2 执行内核 | `eth/kernel/execution/` + `eth/host/` + `eth/vm/` | `innerExecute` / `runCallFrame` / evmone |
| L1 协议状态 | `eth/eip/` · `eth/gas/` · `eth/precompiled/` · `eth/state/` | EIP、gas、预编译、Journal State |
| L0 基础设施 | evmone · `bcos-framework` · `bcos-evm-storage` | storage 库仅 FISCO 链链接 |

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ L4  链编排外壳                                                               │
│     bcos/  FISCO ── bcos-evm-bcos                                           │
│     opstack/  OP Stack ── bcos-evm-op                                       │
│     eth/apply/ + eth/settlement/  ETH 参考 ── 同库 bcos-evm-eth              │
├─────────────────────────────────────────────────────────────────────────────┤
│ L3  stateTransitionExecute（8 步管线）← StateTransitionHooks / ErrorPolicy  │
├─────────────────────────────────────────────────────────────────────────────┤
│ L2  innerExecute → runCallFrame → evmone                                    │
│     ← EvmHostHooks + ChainCallTargetPort                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│ L1  RevisionConfig · EIP · gas · PrecompileRouter · State/Journal            │
├─────────────────────────────────────────────────────────────────────────────┤
│ L0  evmone · bcos-framework · bcos-evm-storage (LedgerStateView)            │
└─────────────────────────────────────────────────────────────────────────────┘
         ▲                    ▲                         ▲
         │ AuthPort           │ EthEvmHostHooks           │ OpStackChainCallTargetAdapter
         │ ChainCallTargetPort│ callTargetPort=null       │ extension=nullptr
         └──── transaction-executor 适配层（运行期注入）────────────────────────┘
```

### 3.2 L1 — 协议与状态层

| 子层 | 能力 |
| --- | --- |
| `RevisionConfig` | 14 EIP bool；A 类 7 字段 FISCO feature 掩码，B 类 fork 推导，C 类 fork 参数 |
| `eth/eip/` | 1559、2929、4844、7623、7702 等单 EIP 实现 |
| `eth/gas/` | intrinsic、refund cap、fee projection（State-free 算术） |
| `eth/precompiled/` | `PrecompileActive` warm+dispatch 单源；`PrecompileRouter` envelope |
| `eth/state/` | `StateView`（冷读）→ `State`（Journal overlay）→ `StateDiff`（输出 TE） |

### 3.3 L2 — 执行内核

| 组件 | 能力 |
| --- | --- |
| `innerExecute` | tx-entry warm、7702 auth、nonce bump、selfdestruct 收尾 |
| `runCallFrame` | TopLevel/Nested；routing → bytecode → value → CREATE → evmone |
| `EthHost` | evmc host：storage/balance/call/log/selfdestruct |
| `CallTargetResolver` | EOA / 合约 / builtin precompile / chain precompile |

**注入 seam（完整虚表见 `StateTransitionHooks.h` / `EvmHostHooks.h`）：**

```text
StateTransitionHooks          StateTransitionErrorPolicy   EvmHostHooks
· getIntrinsicGasParams       · onIntrinsicGasFailure      · allowSelfdestruct
· onNormalizeMessage          · onException                · skipHostValueTransfer
· onPreCheckRules             · onFinalizeGasUsed          · prepareMessage
· onPreCheckGasAffordable     · onComplete                 · applySstoreRefund / …
· onPreCheckCanTransfer                                    ChainCallTargetPort
· onTuneInnerExecuteInput                                  · classifyTarget / dispatch
· onInvokeInnerExecute
```

### 3.4 L4 — ETH 参考路径（`eth/apply/`）

| 组件 | 能力 |
| --- | --- |
| `applyEthMessage` | buyGas（pipeline 外）→ bind → `stateTransitionExecute` → refund |
| `EthStateTransitionHooks` | 1559 cap、7702/4844 规则；7623 经 `IntrinsicGasMode::FloorDataGas` |
| `EthEvmHostHooks` | 空子类，基类默认 = 标准以太坊 |
| `ChainCallTargetPort` | **nullptr** |

### 3.5 L4 — FISCO 外壳（`bcos/`）

**编排层：**

| 能力 | 机制 |
| --- | --- |
| 权限检查 | `FiscoStateTransitionHooks::onPreCheckRules` → `AuthPort::checkAuth`（需 `enable_auth_check`） |
| 21000 gas | **非 7623 路径**：`onPreCheckCanTransfer` 扣 `BALANCE_TRANSFER_GAS`；7623 启用走 intrinsic floor |
| CREATE 地址 | 顶层 `onNormalizeMessage`；嵌套 `FiscoEvmHostHooks::prepareMessage` |
| 费用 / 错误 | `FiscoTxFeeSettlement`；`FiscoStateTransitionErrorPolicy`（`fix_error_handling` 等） |

**Host 层（`FiscoEvmHostHooks`，多数覆写受 bugfix flag 门控）：**

| 覆写 | 行为 |
| --- | --- |
| `allowSelfdestruct` / `allowDelegateCallToPrecompile` | 恒 `false` |
| `skipHostValueTransfer` | `enable_balance_transfer` 时 true |
| `onCreateTargetInitialized` | `AuthPort::createAuthTable` |
| `applySstoreRefund` / `classifyStorageStatus` | `fix_storage_status` ON → 标准 EIP-3529；OFF → legacy |
| `bumpContractCreateNonce` / `finalizeTopLevelCreateNonce` | web3Tx 或 `fix_nonce_init` 等条件下持久化 |

**Call target：**

| 机制 | 能力 |
| --- | --- |
| `FiscoChainCallTargetAdapter` | classify `0x1000+` / `[PRECOMPILED]` |
| TE `ExecutorPrecompileAdapter` | `ChainCallTargetPort` dispatch（运行期注入） |

**`FiscoRevisionConfig` overlay：** 8 个 bugfix bool（`fix_storage_status` … `fix_precompiled_feature_gate`）+ 5 个链行为 bool（`enable_balance_transfer`、`use_raw_address` 等），包裹内嵌 `RevisionConfig ethConfig`。

```text
applyFiscoMessage
  ├─ FiscoExecutionBundle
  │    ├─ FiscoEvmHostHooks
  │    ├─ FiscoChainCallTargetAdapter
  │    └─ AuthPort* / ChainCallTargetPort*（TE 注入）
  └─ FiscoStateTransitionBindings::bind() → stateTransitionExecute → …
```

### 3.6 L4 — OP Stack 外壳（`opstack/`）

Lifecycle **内联在 `applyOpStackMessage`**（无独立 `runOpStackTxLifecycle` 符号）：

```text
applyOpStackMessage
  ├─ bind(ctx) + lifecycleCheckEntryRules   ← pipeline 外；失败则 earlyExit，不进 stateTransitionExecute
  ├─ acquireGasPool
  └─ deposit: mint → checkpoint → stateTransitionExecute → settleDeposit
     normal:  buyGas → checkpoint → stateTransitionExecute → completeAfterPipeline
```

| 能力 | 机制 |
| --- | --- |
| Blob precheck | `lifecycleCheckEntryRules` 内 `OpStackBlobTxChecks` |
| Floor gas | pipeline 内 `onPreCheckGasAffordable` + `OpStackFloorGas` |
| L1 cost / operator fee | `RollupCost`、`OpStackFeeSettlement` |
| Call target | `OpStackChainCallTargetAdapter`（L1Block、GasPriceOracle） |
| Host | **`extension = nullptr`**（内核 null-check 走标准以太坊语义；与 ETH 的实例接线不同） |
| Revision | TE：`makeOpStackRevisionConfigFromBlock(header, features)`；无块上下文时默认 `makeIsthmusRevisionConfig()`（= `revisionConfigFromRevision(EVMC_PRAGUE)`） |

### 3.7 三链扩展对照

| 扩展 seam | ETH 参考 | FISCO | OP Stack |
| --- | --- | --- | --- |
| `StateTransitionHooks` | 1559/7702/4844 `onPreCheckRules`；7623 intrinsic；余额检查 | CREATE normalize；Auth `onPreCheckRules`；21000/7623 `onPreCheckCanTransfer` | **`lifecycleCheckEntryRules`**（pipeline 外）；floor `onPreCheckGasAffordable` |
| `StateTransitionErrorPolicy` | ADR-015 included-vmerr；EIP-7702 set-code 特殊 | `fix_error_handling` 双轨；`NotFoundCodeError` | deposit/REVERT 特殊；其余 vmerr 部分 ADR-015 |
| `EvmHostHooks` | `EthEvmHostHooks` 实例（基类默认） | `FiscoEvmHostHooks`（bugfix 门控覆写） | **`nullptr`** |
| `ChainCallTargetPort` | null | `FiscoChainCallTargetAdapter` + TE `ExecutorPrecompileAdapter` | `OpStackChainCallTargetAdapter` |
| `AuthPort` | 无 | TE 注入 | 无 |
| Lifecycle 外圈 | buyGas 在 `applyEthMessage` 内 | buyGas/refund 在 **TE Execute**（`fix_gas_precheck` 时；非 apply 内） | **`applyOpStackMessage` 内联**（gasPool + settlement） |
| `RevisionConfig` | 块高 → 主网 fork | derive + A 类 flag 掩码（revision 下限 CANCUN） | Isthmus = Prague dense |

**重要：** ETH 参考路径测通 ≠ FISCO/OP 生产路径测通。

### 3.8 错误处理设计（`StateTransitionErrorPolicy`）

#### 3.8.1 要解决的问题

ADR-019 将三链收敛到共享 `stateTransitionExecute` 后，仍有一块**链相关**逻辑不能写进内核：

- intrinsic gas 扣费失败 → 填什么 `EVMCResult` / `TransactionStatus` / `gas_left`
- precheck 或 pipeline 内抛出的 C++ 异常 → 映射成什么 EVM status、是否 revert state
- EVM 执行完成后 → included-vmerr 是否归一化、CREATE 地址如何回填、revert logs 是否清理

重构前这些规则散落在各链 `apply*Message` wrapper 的 `mapIntrinsicFailure` / `mapException` lambda 中。`StateTransitionErrorPolicy` 将这条 **错误映射 seam** 抽为可注入、可单测的虚接口，使 `StateTransitionExecute.cpp` 保持链无关。

实现文件：

| 层级 | 文件 |
| --- | --- |
| 基类 | `eth/kernel/state-transition/StateTransitionErrorPolicy.h` |
| ETH 参考 | `eth/apply/EthStateTransitionErrorPolicy.h` |
| FISCO | `bcos/FiscoStateTransitionErrorPolicy.h` |
| OP Stack | `opstack/apply/OpStackStateTransitionErrorPolicy.h` |
| ADR-015 共享 helper | `eth/kernel/state-transition/IncludedTxVmerrNormalize.h` |

#### 3.8.2 在 pipeline 中的调用点

```text
stateTransitionExecute(ctx, hooks, errorPolicy)
  │
  ├─ [try] hooks.onNormalizeMessage … onInvokeInnerExecute
  │     ├─ deductIntrinsicGas 失败 → errorPolicy.onIntrinsicGasFailure
  │     └─ innerExecute 成功     → errorPolicy.onFinalizeGasUsed
  │
  ├─ [catch] errorPolicy.onException
  │
  └─ [RAII] errorPolicy.onComplete   ← 所有出口（early-exit / 成功 / 异常）均执行
```

| 方法 | 触发条件 | 职责 |
| --- | --- | --- |
| `onIntrinsicGasFailure` | `deductIntrinsicGas` 返回失败 | 将 intrinsic OOG 映射为可结算的 `ctx.evmcResult` |
| `onException` | pipeline `try` 块内未捕获异常 | 按异常类型映射 status；通常 `ctx.state.revert()` |
| `onFinalizeGasUsed` | `innerExecute` 返回后（含 SUCCESS / REVERT / OOG 等） | post-EVM 结果归一化（ADR-015、CREATE 地址、logs 清理） |
| `onComplete` | pipeline 任意出口（RAII guard） | 兜底修正（如负 `gas_left`） |

**注意：** gas buy/refund、L1 cost、block gas pool 等**费用落账**在 TE / `apply*Message` / `*FeeSettlement` 层，不属于 `StateTransitionErrorPolicy`；ErrorPolicy 只负责 **`ctx.evmcResult` 语义**及其对 receipt 字段的影响。

#### 3.8.3 与 `StateTransitionHooks` 的分工

| 维度 | `StateTransitionHooks` | `StateTransitionErrorPolicy` |
| --- | --- | --- |
| 时机 | EVM 入口前（第 1–5 步） | intrinsic 失败、异常、EVM 返回后、出口 |
| 典型动作 | auth 检查、余额 precheck、抛 `NotFoundCodeError` | 捕获异常并填 `evmcResult`、ADR-015 归一化 |
| earlyExit | 可设 `ctx.earlyExit = true` 阻止进 EVM | 在已失败或已执行后**规范化结果** |
| 链差异示例 | FISCO `onNormalizeMessage`；OP `lifecycleCheckEntryRules`（pipeline 外） | FISCO `fixErrorHandling`；Eth ADR-015；OP deposit/REVERT 特殊 |

部分 precheck 失败在 Hooks 层直接写入 `ctx.evmcResult` 并 `earlyExit`（如 Eth malformed tx、FISCO auth 拒绝），**不经过** `onIntrinsicGasFailure` / `onException`；但 RAII `onComplete` 仍会执行。只有 pipeline `try/catch` 包裹范围内的异常与 intrinsic 失败才由 ErrorPolicy 的 `onIntrinsicGasFailure` / `onException` 映射。

#### 3.8.4 三链 ErrorPolicy 差异

**对齐目标：** Eth → geth；FISCO → 历史链行为 + `bugfix_*` 开关；OP Stack → op-geth。

##### intrinsic gas 失败（`onIntrinsicGasFailure`）

| | ETH | FISCO | OP Stack |
| --- | --- | --- | --- |
| `TransactionStatus` | `OutOfGasLimit` | `OutOfGas` | `OutOfGasLimit` |
| `gas_left` | 0 | **双轨**：`fix_error_handling` 关 → 保留 `message.gas`；开 → 0 | 0 |
| 错误 reason | 无 | 有（EIP-7623/7702 分类字符串，ABI 编码进 output） | 无 |
| 构造 helper | `makeEvmcResult` | `makeErrorEVMCResult(hashImpl, …, clampGasLeft)` | `makeOutOfGasLimitResult` |

##### 异常映射（`onException`）

| 异常 / 场景 | ETH | FISCO | OP Stack |
| --- | --- | --- | --- |
| `OutOfGas` | `OutOfGasLimit` | `OutOfGas`，`gas_left=0` | 一律 `INTERNAL_ERROR` + `Unknown` |
| `NotEnoughCashError` | 多在 Hooks precheck，不经 ErrorPolicy | `NotEnoughCash`，gas 受 `fixErrorHandling` 影响 | 一律 internal error |
| `NotFoundCodeError` | 无（FISCO 独有 precheck） | STATIC/DELEGATECALL → `SUCCESS`；普通 CALL → `REVERT` + `"Call address error."` | 一律 internal error |
| 其他 `std::exception` | `Unknown` + `INTERNAL_ERROR` | legacy → `OutOfGas`；`fix_error_handling` 开 → `Unknown` | 一律 internal error |
| state revert | checkpoint 存在则 `revert()` | 同左 | 同左 |

FISCO `NotFoundCodeError` 由 `FiscoStateTransitionHooks::onPreCheckCanTransfer` 在「目标无 code 且有 calldata」时抛出，是 FISCO 历史「调不存在合约」语义，非标准 geth 行为。

##### EVM 返回后归一化（`onFinalizeGasUsed`，在 `innerExecute` 返回后调用，不仅限于 SUCCESS）

| 能力 | ETH | FISCO | OP Stack |
| --- | --- | --- | --- |
| ADR-015 included-vmerr | **完整**：顶层 vmerr → `status_code=SUCCESS`，receipt `TransactionStatus` 保留失败 | **不做** | **部分**：非 REVERT 的顶层 vmerr 走 ADR-015 |
| EIP-7702 set-code REVERT | `normalizeSetCodeTransactionVmerr` | 无 | 无 |
| 顶层 `REVERT` | 走 ADR-015 归一化 | receipt 直接反映失败 | **保持原始 REVERT**（op-geth D3） |
| Deposit 交易 | 走正常规则 | 走正常规则 | **跳过** finalize 归一化 |
| CREATE `create_address` 回填 | 内核帧内处理 | 成功且空 → 从 `message.recipient` 拷贝 | 标准 Eth 路径 |
| `eth_call` | `isCall` 时跳过 finalize | — | — |
| revert logs 清理 | — | `fix_revert_logs` 开启时清空失败交易 logs | — |

ADR-015（`IncludedTxVmerrNormalize.h`）核心语义：顶层 OOG/REVERT 等 vmerr 交易**仍进块、仍收 gas**；结算侧将 `status_code` 归一化为 `SUCCESS`，receipt 的 `TransactionStatus` 保留失败（Gap 40 geth receiptsRoot 对齐）。

##### 出口兜底（`onComplete`）

仅 FISCO 覆写：检测到 `gas_left < 0` 时强制映射为 OOG，防止 `gasLimit - gas_left` 有符号溢出。Eth / OP 使用基类默认 no-op。

#### 3.8.5 配置与装配

| Policy 字段 | 来源 | 链 |
| --- | --- | --- |
| `isCall` | `EthStateTransitionBindings` | ETH（`eth_call` 跳过 ADR-015 finalize） |
| `hashImpl` | `FiscoStateTransitionBindings` | FISCO（`makeErrorEVMCResult` 编码 error output） |
| `fixErrorHandling` | `FiscoRevisionConfig.fix_error_handling`（`bugfix_v1_error_handling`） | FISCO |
| `fixRevertLogs` | `FiscoRevisionConfig.fix_revert_logs` | FISCO |

装配路径：各链 `*StateTransitionBindings::buildErrorPolicy(ctx)` → `stateTransitionExecute` 第三个参数。

#### 3.8.6 测试

| 测试 | 覆盖 |
| --- | --- |
| `test/eth/EthStateTransitionErrorPolicyTest.cpp` | ADR-015、7702 set-code、intrinsic failure |
| `test/bcos/FiscoStateTransitionErrorPolicyTest.cpp` | `fixErrorHandling` 双轨、`NotFoundCodeError`、负 gas_left |
| `test/opstack/OpStackStateTransitionErrorPolicyTest.cpp` | deposit 跳过、REVERT 不归一化、included vmerr |

设计来源：仓库根 `docs/superpowers/specs/2026-06-24-orchestration-error-policy-design.md`（原名 OrchestrationErrorPolicy，落地为 StateTransitionErrorPolicy）；ADR-015 included-vmerr；ADR-022 FISCO CREATE 地址回填。

---

## 4. 执行流程

### 4.1 节点中的位置

```text
共识 → Baseline Scheduler → transaction-executor (TE) → bcos-evm
              Prepare → Execute → Finalize（TE 三阶段；Execute 调 apply*Message）
```

| `executionPath` | TE | bcos-evm 入口 |
| --- | --- | --- |
| Fisco（默认） | `TransactionExecutorImpl` | `applyFiscoMessage` |
| Eth | `EthTransactionExecutorImpl` | `applyEthMessage` |
| OpStack | `OpStackTransactionExecutorImpl` | `applyOpStackMessage` |

### 4.2 共享 pipeline（`stateTransitionExecute`）

```text
normalizeMessage → preCheckRules → preCheckGasAffordable → deductIntrinsicGas
  → [onIntrinsicGasFailure] preCheckCanTransfer → tuneInput + onInvokeInnerExecute (innerExecute)
  → adoptEvmcResult + settlementSnapshot → [onFinalizeGasUsed]
  → [catch: onException] → [RAII: onComplete]
```

（方括号内为 `StateTransitionErrorPolicy` 回调；详见 §3.8。）

`innerExecute` 内：`prepareState` → `runCallFrame(TopLevel)` → evmone → `EthHost::call` → `runCallFrame(Nested)` → PrecompileRouter envelope。

ETH normal：`applyEthMessage` 先 **buyGas**，再进 pipeline，最后 refund。OP normal/deposit：lifecycle 外圈处理 gasPool 与 settlement 后再/再进 pipeline。

---

## 5. 关键数据结构

**`StateTransitionContext`** — 单笔交易编排上下文（message、state、revision、earlyExit、fee 侧车）；经 `wireExecutionEnvironment(vm, extension, callTargetPort)` 注入执行环境。

**`RevisionConfig`** — 14 EIP bool + calldata floor；全模块唯一开关来源。

**`State` + Journal** — checkpoint/revert；precompile envelope（checkpoint → transfer → dispatch）依赖 journal 原子回滚。

**结算分层：** `eth/gas/` 做 State-free 算术；`eth/settlement/`、`bcos/`、`opstack/settlement/` 做 State-based 落账（buyGas/refund/L1 cost）。

---

## 6. 当前状态（2026-07-08）

### 已完成

- 四静态库 + alias（eth / storage / bcos / op）+ `eth/` 单向依赖
- 三链共享 `stateTransitionExecute` + 统一 `runCallFrame`
- Port 依赖倒置（`AuthPort` + `ChainCallTargetPort`）
- EEST（ETH 参考路径，基准 `build-bcos-evm-check` @ `eest-integration-matrix.md`）：
  - curated manifest Shanghai–Osaka **4140/4140**
  - transaction tests **106/106**
  - granular full-tree **2722/2722** file-clean（`160604Z`）
  - static GST **2445/2445**（`015708Z`）
  - blockchain：Cancun **2181/2181**；Prague **2302/2302**；Osaka **1321/1321**；M4 Berlin/London/Paris/Shanghai；static **40855/40855**

### 待做

| 项 | 说明 |
| --- | --- |
| 入口失败 reject（ADR-028） | geth 不收录 intrinsic/buyGas 失败；TE Finalize 仍 always `makeReceipt`，待 TE/共识层闭合 |
| TE/共识对齐 | finalize/receipt inclusion 语义与 geth 仍有差距（parity audit / Gap 39） |
| Blockchain engine/sync | `blockchain_tests_engine*` / sync 格式与 CL payload 解码 intentionally deferred |

---

## 7. 总结

`bcos-evm` = **可独立演进的标准内核**（L1–L3，`bcos-evm-eth`）+ **三链可注入外壳**（L4）。差异通过 Hooks（交易级/帧级）与 Port（Auth、链 CALL 目标）注入；**错误语义**通过 `StateTransitionErrorPolicy` 与 Hooks 分工注入（§3.8）；EIP 开关集中在 `RevisionConfig`。架构骨架与 EEST 参考路径回归已闭合，剩余主要是 ADR-028 入口失败 reject 与 TE/共识层 inclusion 语义对齐。

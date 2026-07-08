# Eth 费用结算 State 化 — 去除 ledger 依赖

**日期：** 2026-07-03  
**状态：** 待审阅  
**实现计划：** [2026-07-03-eth-fee-settlement-remove-ledger-dependency.md](../plans/2026-07-03-eth-fee-settlement-remove-ledger-dependency.md)  
**相关 ADR：** [ADR-005](../../bcos-evm/docs/adr/005-orchestration-domain-boundaries.md)、[ADR-015](../../bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md)、[ADR-025](../../bcos-evm/docs/adr/025-opstack-entry-failure-early-return.md)、[ADR-026](../../bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md)、[ADR-028](../../bcos-evm/docs/adr/028-consensus-reject-entry-failure-inclusion.md)

**参考实现：** OpStack `OpStackFeeSettlement` + `OpStackNormalTxFeeCoordinator`（已 State-first）  
**基线对照：** go-ethereum `core/state_transition.go`（`execute` / `buyGas` / `returnGas`）、`core/vm/evm.go`（`Call` 内部 Snapshot/RevertToSnapshot）

> **设计交叉审阅（2026-07-03）：** 三 subagent 核查 + geth/kernel 源码比对后，本 spec 已修订
> checkpoint 模型。**核心结论：顶层 EVM overlay 的 vmerr 回退已由 kernel 内部完成**
> （`EvmCallFrame::finalizeFrame` 非 SUCCESS → `state.revert()`；`InnerExecute::finalizeAfterFrame`
> SUCCESS → `state.commit()`），等价 geth `evm.Call` 的 Snapshot/RevertToSnapshot。因此 fee 层
> **无需** hard-fail revert，只需单层 pre-buyGas checkpoint 处理 pre-exec reject 的 buyGas 撤销。

---

## 1. 背景与动机

### 1.1 现状问题

Eth 参考路径存在 **双通道状态写入**：

| 通道 | 写入点 | 后端 |
| --- | --- | --- |
| EVM 执行 | `applyEthMessage` → `StateTransitionContext::state` → `stateDiff` | `state::State` overlay |
| 费用结算 | `EthTransactionExecutorImpl` → `EthTxFeeSettlement::buyGas/refundGas` | `ledger::EVMAccount` 直写 storage |

后果：

1. **`bcos-evm-eth` 非法依赖 ledger：** `EthTxFeeSettlement.h` `#include <bcos-framework/ledger/EVMAccount.h>`；`CMakeLists.txt` PUBLIC link `ledger`。违反 ADR-005「eth/ 不含 bcos 适配」精神。
2. **TE 编排分裂：** buyGas 在 pipeline 外、refundGas 在 applyStateDiff(EVM) 之后；`m_afterBuyGasSavepoint` 用 storage rollback 模拟「保留预扣、丢弃 EVM 变更」——这是**在 storage 层重复**了 kernel State overlay 已经做过的 vmerr 回退（见 §1.3），与 OpStack 已收敛的单通道 `State::commit` 模型不一致。
3. **测试分裂：** 公式层（`TxFeeSettlementTest`）不测余额；bcos-evm eth 单测用 `InMemoryStateView` 不测 fee；TE E2E 才通过 `EVMAccount` 验证 1559 落账。

OpStack 已于 `applyOpStackMessage` 内完成 fee + pipeline 统一编排，Eth 应镜像同一分层。

### 1.2 与 ADR-026 的关系

ADR-026 已将 EIP-1559 **数学** 收拢到 `eth/gas/TxFeeSettlement.h`（纯函数、无 State）。本 spec 完成 **adapter 层** 迁移：从 `EVMAccount` 改为 `state::State`，并内聚 lifecycle 到 `applyEthMessage`。

### 1.3 geth 基线：费用不可回退，EVM 执行体单独 snapshot

geth `stateTransition.execute()`（`core/state_transition.go:538`）的费用/状态模型：

| 步骤 | 代码 | 回退性 |
| --- | --- | --- |
| buyGas 预扣 `SubBalance(from, gasLimit × effectiveGasPrice)` | `buyGas()` `:429`，在 `preCheck` 内、早于 `evm.Call` | **永不回退** |
| EVM 执行 | `evm.Call/Create`：入口 `Snapshot()`，vmerr → `RevertToSnapshot()`（`core/vm/evm.go:266,318`） | **仅执行体回退** |
| returnGas `AddBalance(from, gasRemaining × gasPrice)` | `returnGas()` `:791` | 无条件 |
| coinbase tip `gasUsed × (gasPrice - baseFee)` | `execute` `:690` | 无条件 |
| base fee | 从不 credit | 隐式销毁 |

关键：**buyGas 早于 evm.Call 的 snapshot，故 vmerr 时预扣天然保留**；geth 顶层交易从不为整笔做 snapshot。被拒交易（preCheck/intrinsic 失败）在 L1 是**区块级拒绝**（`ApplyMessage` 返回 err → 整块无效），无 penalty、无 receipt。

**FISCO kernel 已等价复刻此模型**（顶层帧）：

- `EvmCallFrame.cpp:367-412` `finalizeFrame`：非 SUCCESS（含 REVERT / OOG / INVALID）→ `state.revert()`；SUCCESS 顶层不 commit。
- `InnerExecute.cpp:211-222` `finalizeAfterFrame`：SUCCESS → `state.commit()` + `finalize_self_destructs()`；失败 → 仅 `build_diff()`（overlay 已由 finalizeFrame 回退）。

即 `stateTransitionExecute` 返回后，`ctx.state` 中 EVM 执行体改动在 vmerr 时**已被 kernel 回退**，`build_diff()` 只剩 pre-frame（nonce bump 等）。这是本 spec checkpoint 模型简化的**技术前提**：fee 层不必再为 hard-fail 回退 EVM。

**FISCO 与 geth 的唯一偏离** = buyGas penalty：FISCO 共识已排定的交易必须产出 receipt，不能像 L1 那样丢弃，故余额不足时扣 `min(balance, intrinsic × price)` 并出 `NotEnoughCash` receipt（ADR-026 D5 / ADR-028 领域）。

---

## 2. 目标与非目标

### 2.1 目标

1. **`bcos-evm-eth` 零 ledger 依赖：** `eth/**` 不含 `#include "bcos-framework/ledger/*"` / `bcos-ledger/*`；CMake 移除 `ledger` link。
2. **State-first fee adapter：** 新建 `eth/settlement/`，`buyGas` / `refundGas` 通过 `ctx.state.set_balance` 改余额。
3. **Lifecycle 内聚：** `applyEthMessage` 编排 **单层** checkpoint → buyGas → `stateTransitionExecute` → completeAfterPipeline（commit / abort-revert）→ refundGas → `build_diff()`。
4. **TE 瘦身：** `EthTransactionExecutorImpl` 仅 `applyEthMessage` + **一次** `applyStateDiff` + `makeReceipt`。
5. **语义不变：** 保留 buyGas penalty、vmerr 结算（gas 照收、EVM 执行体已由 kernel 回退）、pre-exec reject 退还预扣等生产行为（见 §6）。
6. **测试补齐：** 新增 `EthFeeSettlementStateTest`；现有 `EthTxFeeLedger1559Test` 仍 PASS（E2E 通过 storage 读回验证）。

### 2.2 非目标

- **FISCO 路径：** `bcos/FiscoTxFeeSettlement.h` 仍用 `EVMAccount`（ADR-026 non-goals）。
- **`bcos/` storage 适配：** `StateDiffApplier`、`FiscoStateView`、`FiscoBlockInfo` 继续依赖 ledger — 这是生产 storage port，不是 eth kernel 问题。
- **Blob gas buyGas（EIP-4844）：** Eth TE 当前无 blob 分支；不在本 spec 扩展。
- **三链 fee 数学统一：** OpStack L1/operator fee 仍在 `opstack/fee/`。
- **Receipt 工厂迁移：** `makeReceipt` 暂留 TE（依赖 `TransactionReceiptFactory`），不从 ledger 解耦的必要条件。

---

## 3. 方案对比

| 方案 | 描述 | 优点 | 缺点 |
| --- | --- | --- | --- |
| **A（推荐）** | 镜像 OpStack：新建 `eth/settlement/*`，fee lifecycle 进 `applyEthMessage` | 与 OpStack 一致；单 `stateDiff`；eth/ 无 ledger | TE + apply 层改动；需迁移 gas 计量 |
| **B** | 保留 TE 编排，仅把 `EthTxFeeSettlement` 内部 EVMAccount 换成 State wrapper | 改动面小 | 仍双通道（State overlay vs TE 直写）；wrapper 需 bridging State→storage |
| **C** | 抽象 `BalancePort` 接口，eth 不依赖 ledger 具体类型 | 最通用 | 过度设计；FISCO 与 Eth 仍各需 adapter；YAGNI |

**决策：方案 A。** OpStack 已验证；ADR-005 §4 允许 wrapper 侧 checkpoint/commit/revert；Eth 参考路径无 L1/operator/deposit 复杂度，sidecar 可极简。

---

## 4. 架构

### 4.1 分层

```
eth/gas/TxFeeSettlement.h     ← 纯函数 plan（不变）
eth/eip/Eip1559.h             ← 公式（不变）
eth/settlement/
  EthFeeSidecar.h             ← lifecycle 可变快照
  EthSettlementProjection.h   ← ctx + EthMessageRequest facade
  EthFeeSettlement.*          ← buyGas / refundGas（State 余额）
  EthTxFinalize.*             ← gasUsed 计量、abort 辅助
  EthNormalTxFeeCoordinator.* ← buyGas → execute → completeAfterPipeline
eth/apply/ApplyEthMessage.*   ← 接入 coordinator
transaction-executor/
  EthTransactionExecutorImpl  ← apply + applyStateDiff + makeReceipt
bcos/
  StateDiffApplier            ← 生产落账（仍 EVMAccount，不在范围）
```

### 4.2 数据流（Normal Eth Tx）

```mermaid
sequenceDiagram
    participant TE as EthTransactionExecutorImpl
    participant Apply as applyEthMessage
    participant Coord as EthNormalTxFeeCoordinator
    participant Fee as EthFeeSettlement
    participant Pipe as stateTransitionExecute
    participant State as ctx.state
    participant Storage as applyStateDiff

    TE->>Apply: EthMessageRequest (isCall=false)
    Apply->>State: checkpoint()  (单层, pre-buyGas)
    Apply->>Coord: buyGas
    Coord->>Fee: planPreExecution + set_balance(debit)
    Fee-->>Coord: ok / penalty fail
    alt buyGas fail (penalty)
        Note over Coord,State: 不 revert; 保留 penalty diff
        Coord->>State: build_diff (penalty only)
        Apply-->>TE: EthMessageResult (evmcResult=NotEnoughCash)
    end
    Apply->>Pipe: stateTransitionExecute
    Note over Pipe,State: vmerr 时 kernel 已 revert 顶层 overlay;<br/>SUCCESS 时 kernel commit
    Apply->>Coord: completeAfterPipeline
    alt pre-exec reject (intrinsic / gas-afford)
        Coord->>State: revert()  → 撤销 buyGas
    else 其它 (SUCCESS / REVERT / vmerr / included-vmerr)
        Coord->>Fee: refundGas (unused + coinbase tip)
    end
    Coord->>State: build_diff()  (fee + kernel 结果)
    Apply-->>TE: stateDiff + gasUsed + effectiveGasPrice + gasPriceStr
    TE->>Storage: applyStateDiff once
    TE->>TE: makeReceipt
```

**要点：** 只有 **一层** checkpoint（buyGas 前）。EVM 执行体的 vmerr 回退在 `stateTransitionExecute` 内部由 kernel 完成（§1.3），fee 层对所有非 pre-exec-reject 分支**一律不再 revert**，只做 refund + build_diff。pre-exec reject 时 `revert()` 一次即回退 buyGas。

### 4.3 CMake 边界

| Target | 变更前 | 变更后 |
| --- | --- | --- |
| `bcos-evm-eth` | link `ledger` | **移除** `ledger` |
| `bcos-evm-bcos` | link `bcos-evm-eth` | 不变；transitive 仍可从 bcos/ 用 ledger |

---

## 5. 模块设计

### 5.1 `EthFeeSidecar`

```cpp
struct EthFeeSidecar {
    bcos::u256 effectiveGasPrice{0};
};
```

| 字段 | 写入 | 读取 |
| --- | --- | --- |
| `effectiveGasPrice` | `EthFeeSettlement::buyGas` | `refundGas`、`EthMessageResult` |

Eth 无 L1/operator/floorDataGas；比 OpStack sidecar 更简。

### 5.2 `EthSettlementProjection`

只读 facade，解耦 fee planner 与 `ApplyEthMessage` 内部：

```cpp
struct EthSettlementProjection {
    StateTransitionContext& ctx;
    EthMessageRequest const& input;
    EthFeeSidecar& sidecar;

    bool isCall() const noexcept;
    bcos::u256 gasTipCap() const noexcept;
    bcos::u256 gasFeeCap() const noexcept;
    bcos::u256 gasPriceLegacy() const noexcept;
    uint8_t web3TypedTxKind() const noexcept;
    bool hasExplicitFeeCaps() const noexcept;
    state::BlockInfo const& blockInfo() const noexcept;
    bcos::u256 txValue() const noexcept;
    int64_t gasLimit() const noexcept;
};
```

### 5.3 `EthFeeSettlement`

**职责：** 读 `gas::planPreExecution` / `planPostExecution`，写 `ctx.state` 余额。

```cpp
struct EthFeeSettlement {
    task::Task<bool> buyGas(EthSettlementProjection view);
    task::Task<gas::FeeSettlementPlan> refundGas(
        EthSettlementProjection& view, EthTxFinalizeResult const& settled);
};
```

**buyGas 逻辑：**

1. `isCall` 或 `gasLimit <= 0` 或 `effectiveGasPrice == 0` → 早退 `true`。
2. `totalRequired = plan.maxBalanceDebit + txValue`；余额不足 → penalty（ADR-026 D5）+ `NotEnoughCash` + `false`。
3. 成功 → `set_balance(sender, balance - plan.preDebitAmount)`。

**refundGas 逻辑（对齐 geth `returnGas` + coinbase tip）：**

1. `isCall` 或 `effectiveGasPrice == 0` → 空 plan。
2. `planPostExecution(feeInputs, gasUsed, gasRemaining)`。
3. `sender += unusedRefund`（geth `returnGas`）。
4. `coinbase += coinbaseTip`；coinbase 账户不存在时 `set_balance` 隐式创建（geth `AddBalance` 语义）。
5. **`baseFeeAmount` 不 credit 任何地址**（Eth implicit burn，geth 同）。

**注意：** vmerr 时 EVM 执行体已由 kernel（`finalizeFrame`）回退（§1.3）；`refundGas` **对所有 exitKind 一致执行**（gas 照收、退未用、给 tip），无需区分 hard-fail —— 与 geth「vmerr 仍收 gas」一致。

### 5.4 `EthTxFinalize`

**职责：** 从 TE 搬迁 gas 计量；提供 abort 契约。

```cpp
struct EthTxFinalizeResult {
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
};

bool isEthPreExecutionReject(StateTransitionExitKind exitKind) noexcept;

void abortEthAfterBuyGas(
    StateTransitionContext& ctx, EthMessageResult& output, int64_t originalGasLimit);

EthTxFinalizeResult finalizeEthNormal(
    StateTransitionContext const& ctx,
    StateTransitionExitKind exitKind,
    gas::TxGasSettlementContext const& snapshot,
    bool topLevelIncludedTxVmError);
```

`finalizeEthNormal` 搬迁现 `EthTransactionExecutorImpl::settleGasUsedFromEvmResult`，**逐条保留现有行为**：
- pre-exec reject（`isEthPreExecutionReject`）→ `gasUsed = 0`，`gasRemaining = originalGasLimit`。
- EIP-7623 结算 gate：`ctx.revisionConfig.eip7623 && snapshot.gasLimit > 0` → `gas::settleTopLevelTransactionGas(...)`。
- 否则（legacy London–Cancun）→ `gasUsed = originalGasLimit - gas_left`。

> **⚠️ 去除 `isWeb3`（ADR-005 修正，2026-07-03）：** 现 TE `:190` 用 `isWeb3 = transaction.type() == Web3Transaction` 作为第三重 gate。`isWeb3` 是 **FISCO 编排域**概念（原生 tx vs Web3 tx），**不得**透传进 `bcos-evm/eth` 参考层（违反 ADR-005）。EIP-7623 是否适用的链策略已由 `IntrinsicDebitMode::Eip7623`（各链 `getIntrinsicGasParams()` 决定）表达 —— `captureSettlementSnapshot` 仅在该 mode 下写 `snapshot.gasLimit`（`StateTransitionExecute.cpp:28`），故 **`snapshot.gasLimit > 0` 已等价于「7623 mode 激活」**。OpStack 同层（`OpStackPostExecuteGas.h`）亦无 `isWeb3`。生产 Eth 路径 `isWeb3` 恒真（`eth_sendRawTransaction` 硬写 `Web3Transaction`），故删除后生产零行为变化，并闭合审计 N4 理论缺口（非 Web3 tx 在 Prague+ 现在正确套用 7623 refund settlement）。

**`topLevelIncludedTxVmError` 由 coordinator 从 output 传入**（已是 `EthMessageResult` 的合法 eth-domain 字段）。included-vmerr 的 peak-gas 修正（ADR-015 GAP-TE-002）**沿用现 TE 行为**（当前 TE 也未特化，纯搬迁、零行为变化）；若后续要修，改 `finalizeEthNormal` 一处即可。

### 5.5 `EthNormalTxFeeCoordinator`

镜像 `OpStackNormalTxFeeCoordinator`：

```cpp
struct EthNormalTxFeeCoordinator {
    EthFeeSettlement& ledger;

    task::Task<bool> buyGas(EthSettlementProjection view, EthMessageResult& output);
    task::Task<void> completeAfterPipeline(
        EthSettlementProjection view, EthMessageResult& output);
};
```

| 方法 | 行为 |
| --- | --- |
| `buyGas` | 委托 `ledger.buyGas`。失败（penalty）→ **不** revert（保留 penalty diff）→ `output.evmcResult = std::move(ctx.evmcResult)`、`output.gasUsed`、`output.effectiveGasPrice`、`output.gasPriceStr`、`output.stateDiff = build_diff()` → 返回 false（apply 层据此 early return） |
| `completeAfterPipeline` | ① pre-exec reject（`isEthPreExecutionReject(ctx.exitKind)`）→ `abortEthAfterBuyGas`（`state.revert()` 撤销 buyGas，gasUsed=0，`build_diff`）→ return。② 其它（SUCCESS / REVERT / vmerr / included-vmerr / ExceptionHandled）→ `finalizeEthNormal` → `refundGas` → 填 `gasUsed` / `effectiveGasPrice` / `gasPriceStr` / `topLevelIncludedTxVmError` → `output.stateDiff = build_diff()` |

**关键差异 vs OpStack：** OpStack normal 路径 checkpoint 后对非 pre-exec 分支 `commit()`；本设计 **无第二 checkpoint、无 commit 调用** —— 因为顶层帧的 commit/revert 已由 kernel（`finalizeAfterFrame` / `finalizeFrame`）完成，fee 层的余额改动直接落在 State overlay 基底上，`build_diff()` 汇总即可。pre-exec reject 时 kernel 未进入 EVM 帧、未 commit，故 `revert()` 一次回退到 pre-buyGas checkpoint 即撤销 buyGas。

**`ctx.gasPrice` 注入：** buyGas 成功后、`stateTransitionExecute` 前，apply 层设 `ctx.gasPrice = sidecar.effectiveGasPrice`（对齐 OpStack `ApplyOpStackMessage.cpp:150`），供 kernel `COINBASE`/`GASPRICE` opcode 与 tip 计算读取。

### 5.6 `EthMessageRequest` / `EthMessageResult` 扩展

**Request 新增：**

```cpp
bool isCall{false};
bcos::u256 txValue{0};
```

**Result 新增：**

```cpp
int64_t gasUsed{0};
bcos::u256 effectiveGasPrice{0};
std::string gasPriceStr;
```

`stateDiff`、`topLevelIncludedTxVmError` 已存在于 `EthMessageResult`（现 `applyEthMessage` 已输出后者）；fee + EVM 合并后由 TE 一次 `applyStateDiff`。coordinator 在 `completeAfterPipeline` 内确保 `topLevelIncludedTxVmError` 从 `ctx` 继续传播到 `output`（现 `ApplyEthMessage.cpp:77` 行为保留）。

### 5.7 `EthChainPolicy` 清理

删除 `#include "bcos-framework/ledger/Features.h"` 及 `features()`（Eth 参考路径返回空 Features，无调用方）。

### 5.8 删除 `EthTxFeeSettlement.h`

Task 末删除；`include/bcos-evm/eth_executor.hpp` 改 export `EthFeeSettlement.h`。

### 5.9 `EthTransactionExecutorImpl` 变更

**删除：**

- `TxExec` template 参数 / `m_txExecutor`
- Execute phase 内 `buyGas` / `refundGas` / `settleGasUsedFromEvmResult`
- `m_afterBuyGasSavepoint`
- SUCCESS/REVERT 条件下「仅 EVM stateDiff」的 `applyStateDiff` gate

**保留/新增：**

- `applyEthMessageTx` 填 `input.isCall`、`input.txValue`（`= u256(tx.value())`，与 `message.value` 同源）
- Execute phase 读 `output.gasUsed` / `effectiveGasPrice` / `gasPriceStr` / `topLevelIncludedTxVmError`
- **`applyStateDiff` 门控收敛为：`!m_call && !stateDiff.accounts.empty()`**（见 §6：pre-exec reject 时 coordinator 已 `revert`，diff 为空自然跳过；vmerr 时 kernel 已回退 EVM，diff 仅含 fee + pre-frame，应落账。eth_call 不落账 —— 保留现行为，Q2）
- `makeReceipt` 内联为 TE private 方法（从旧 `EthTxFeeSettlement::makeReceipt` 搬迁；无 ledger 依赖）

---

## 6. 语义矩阵（行为契约）

EVM 执行体的回退由 **kernel** 负责（§1.3）；下表「EVM overlay」列指 `stateTransitionExecute` 返回时 `ctx.state` 中该笔 EVM 执行体改动的存留，fee 层不再干预。

| 场景 | coordinator 分支 | EVM overlay（kernel 结果） | fee 层动作 | stateDiff | gasUsed |
| --- | --- | --- | --- | --- | --- |
| `eth_call`（`isCall=true`） | 跳过 coordinator | 保留（overlay） | 无 buyGas/refund | TE **不** apply（Q2） | 0 |
| buyGas 余额不足（penalty） | `buyGas` 返回 false | 未进入 EVM | penalty 扣减，**不** revert | penalty diff | `penalty / effectiveGasPrice` |
| intrinsic / gas-afford reject | `abortEthAfterBuyGas` | 未进入 EVM | `revert()` 撤销 buyGas | 空 | 0 |
| `EVMC_SUCCESS` | 正常 | kernel `commit` 保留 | refund + tip | fee + EVM | finalize |
| `EVMC_REVERT` | 正常 | kernel `revert` 丢弃执行体 | refund + tip | fee + pre-frame | finalize |
| vmerr（OOG/INVALID 等，非 included） | 正常 | kernel `revert` 丢弃执行体 | refund + tip | fee + pre-frame | finalize |
| included vmerr（`topLevelIncludedTxVmError`） | 正常 | kernel `revert` 丢弃执行体（geth 同：vmerr 回退执行体、仅收 gas） | refund + tip；receipt status normalize→success | fee + pre-frame | finalize |
| `ExceptionHandled` | 正常 | errorPolicy.onException 已 `revert` | refund + tip | fee + pre-frame | finalize |
| `effectiveGasPrice == 0` | buyGas/refund 早退 | 按上表 | 无 | 按 EVM | 按 EVM |

> **included vmerr 澄清（修订）：** 交叉审阅初判「included vmerr 须 commit EVM」有误。geth L1 对 top-level vmerr 的语义是 **回退 EVM 执行体、仍收 gas、交易成功打包**；FISCO 的 `topLevelIncludedTxVmError` 仅把 receipt `status_code` normalize 成 success，**不改变** EVM 执行体已被 kernel 回退的事实。7702 authorization 等在 `InnerExecute::apply7702TxAuthorizationsIfNeeded` 中以**独立 checkpoint+commit**落盘（`InnerExecute.cpp:104-115`），先于顶层帧、不随 vmerr 回退 —— 故 auth 效果得以保留，与 geth 一致。**结论：included vmerr 无需 fee 层 commit EVM。**

**Checkpoint 时序（单层）：**

1. `checkpoint()` — **唯一** checkpoint，buyGas 前（对齐 OpStack `ApplyOpStackMessage.cpp:142`）
2. `buyGas` — 改余额（成功）或 penalty（失败即返回）
3. 设 `ctx.gasPrice = sidecar.effectiveGasPrice`
4. `stateTransitionExecute` — 内部：pre-exec reject 早退（未 commit）/ 顶层帧 SUCCESS commit、非 SUCCESS revert
5. `completeAfterPipeline` — pre-exec reject → `revert()`（回退到 (1)，撤销 buyGas）；其它 → refund + tip（不 revert、不额外 commit）
6. `build_diff()` — 汇总 fee + kernel 结果

**与现 TE 对齐点：**

- vmerr 时现 TE `rollback(m_afterBuyGasSavepoint)`（storage 层）**多余**：kernel State overlay 已回退 EVM；新模型 diff 天然不含被回退的执行体。
- pre-exec reject：单层 `revert()` 撤销 buyGas（现 TE 无此路径的 phantom-charge 修复，本设计对齐 ADR-025）。
- buyGas fail：coordinator early return + penalty diff，TE Finalize 仍 `makeReceipt` 出 `NotEnoughCash`（行为等价现状）。

---

## 7. 错误处理与 ADR 映射

| ADR | 本 spec 落点 |
| --- | --- |
| ADR-005 | fee lifecycle 在 `eth/apply/` + `eth/settlement/`，不含 bcos/op include |
| ADR-015 | included vmerr：kernel 回退执行体、`topLevelIncludedTxVmError` 传播、gas 照收 —— 对齐 geth；7702 auth 经 kernel 独立 commit 保留（§6 澄清） |
| ADR-025 | pre-exec reject 单层 `revert()` 撤销 buyGas，gasUsed=0、空 diff —— 无 phantom charge（修复现 TE 缺口） |
| ADR-026 | adapter 读 plan 写 State；penalty 在 adapter（D5） |
| ADR-028 | **inclusion 层 deferred。** 本 spec 仅在 settlement 层就地 abort：buyGas false / pre-exec reject → gasUsed=0 或 penalty；`RulesRejected`、`consensusOutcome` 传播、Finalize nullptr 等留待 ADR-028 落地（见 §11 Q4） |

---

## 8. 测试策略

| 层级 | 文件 | 覆盖 |
| --- | --- | --- |
| 公式（不变） | `TxFeeSettlementTest` | plan 数值 |
| State adapter（新增） | `EthFeeSettlementStateTest` | buyGas debit、penalty（余额不足）、refund + coinbase tip、`effectiveGasPrice==0` 早退 |
| Coordinator（新增） | `EthFeeSettlementStateTest` | **pre-exec reject → `revert()` 后 sender 余额还原、diff 空、gasUsed=0**（ADR-025 回归）；vmerr → 保留 buyGas、diff 含 fee |
| Finalize（新增） | 同上 | EIP-7623 gasUsed（Web3）、legacy `gasLimit-gas_left` |
| Pipeline（不变） | `StateTransitionExecuteTest` | 无 fee |
| Cross（不变） | `FeeSettlementCharacterizationTest` | plan oracle |
| TE E2E（回归） | `EthTxFeeLedger1559Test` | 1559 sender/coinbase 余额（通过 EVMAccount 读回验证 State 落账无回归） |
| EEST（不变） | GST post-hoc settlement | 参考路径 |

**geth 对照断言（新增 `EthFeeSettlementStateTest` 关键用例）：**

- buyGas 实扣 = `gasLimit × effectiveGasPrice`（geth `mgval`）；余额检查上限 = `gasLimit × gasFeeCap + value`（geth `balanceCheck`）。
- vmerr 后 sender 净扣 = `gasUsed × effectiveGasPrice`、coinbase 得 `gasUsed × (effectiveGasPrice - baseFee)`、base fee 不入账（geth `returnGas` + tip）。

**验收标准：**

```bash
rtk grep -r 'bcos-framework/ledger\|bcos-ledger' bcos-evm/eth/   # 零匹配
cd build-bcos-evm-check && ctest -R 'Eth|TxFeeSettlement|InnerExecute|StateTransition' --output-on-failure
./transaction-executor/tests/EthTxFeeLedger1559Test
```

---

## 9. 迁移与 PR 策略

与实现计划 9 Task 对齐，建议 **单 PR 或 2 PR**：

| PR | 内容 |
| --- | --- |
| PR1 | `eth/settlement/*` + `EthFeeSettlementStateTest` + EthChainPolicy 清理 |
| PR2 | `applyEthMessage` wiring + TE 瘦身 + 删 `EthTxFeeSettlement` + CMake + docs |

PR1 可独立合并（新代码未接线，零行为变化）。

---

## 10. Follow-up（独立 spec）

1. **FISCO fee State 化：** `FiscoTxFeeSettlement` → 统一 `StateDiffApplier` 单通道。
2. **`FiscoBlockInfo` ledger 解耦：** `getBlockHash` port 注入，使 `bcos-evm-bcos` 可选不 link `bcos-ledger`。

---

## 11. 开放问题与决议

| # | 问题 | 决议 |
| --- | --- | --- |
| Q1 | buyGas fail 的 `gasUsed` 由 apply 还是 TE 算？ | **apply/coordinator 写入**；TE 只读 `output.gasUsed` |
| Q2 | eth_call 的 `stateDiff` 是否 apply 到 storage？ | **否**（保留现 TE 行为，门控 `!m_call`） |
| Q3 | 是否需要第二个（post-buyGas）checkpoint？ | **否（已决议）**。kernel 顶层帧已负责 EVM overlay commit/revert（§1.3），fee 层单层 pre-buyGas checkpoint 足矣；hard-fail 无需 fee 层 revert |
| Q4 | ADR-028 共识拒绝语义（RulesRejected 入 abort、`consensusOutcome`、Finalize nullptr）是否纳入？ | **否（用户决议：defer）**。本 spec 为行为等价重构；ADR-028 落地时建于本 State-first 基础之上 |

---

## 附录 A：交叉审阅决议记录（2026-07-03）

三 subagent 交叉审阅 + geth/kernel 源码比对，修订如下：

| 初判问题 | 结论 | 处理 |
| --- | --- | --- |
| C1 双 checkpoint + 单 revert 无法撤销 buyGas | **属实** | 改单层 pre-buyGas checkpoint（§6 时序） |
| C2「镜像 OpStack」声称不实（OpStack 仅 1 checkpoint、从不 hard-fail revert） | **属实** | §5.5 明确差异；采用真·单层模型 |
| C3 included vmerr 须 commit EVM | **撤销**：geth 语义为回退执行体、仅收 gas；7702 auth 由 kernel 独立 commit 保留 | §6 澄清框 + §7 ADR-015 行 |
| I1 §5.9 无条件 applyStateDiff 与 eth_call 矛盾 | **属实** | §5.9 门控收敛为 `!m_call && !empty` |
| I2 `topLevelIncludedTxVmError` 传播缺失 | **属实** | §5.6 明确 coordinator 传播 |
| I3 `finalizeEthNormal` 缺 isWeb3 / included 参数 | **部分修正**：`isWeb3` 系 FISCO tx-type 概念，透传进 eth 层违反 ADR-005；改用 `IntrinsicDebitMode`/`snapshot.gasLimit>0` 表达 7623 gate（OpStack 同层无 isWeb3、生产恒真） | §5.4 签名仅补 `topLevelIncludedTxVmError`；**去除 isWeb3** |
| I4 RulesRejected 未入 abort | **defer**（ADR-028） | §7 / Q4 |
| I5 序列图 buyGas fail revert vs penalty 矛盾 | **属实** | §4.2 图修正：penalty 不 revert |

**geth 基线确认：** buyGas 早于 evm.Call snapshot → 预扣不回退；vmerr 回退执行体、仍收 gas；base fee 隐式销毁。FISCO kernel 顶层帧（`finalizeFrame`/`finalizeAfterFrame`）已等价复刻，故 fee 层去 hard-fail revert 合理。

---

## Spec Self-Review

- **Placeholder scan：** 无 TBD/TODO。
- **内部一致性：** §1.3 geth/kernel 前提 → §4.2 单层图 → §5.5 coordinator → §6 矩阵 → §7 ADR 逐层对齐。
- **Scope：** 单 spec；FISCO follow-up §10；ADR-028 defer（Q4）。
- **歧义：** Q1–Q4 已决议；included-vmerr 语义经 geth 比对定论（§6 澄清框）。

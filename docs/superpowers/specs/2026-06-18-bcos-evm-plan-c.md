# bcos-evm 方案 C：对齐 evmone `state` 库重构 Host

**Status:** Approved + Reviewed（Grill 2026-06-18；§15–§23 补遗与 sub-agent 审查 2026-06-18）  
**Date:** 2026-06-18  
**Supersedes:** 方案 B（TxContextView 分层 Host）— 本期不实施  
**Scope:** 仅 `transaction-executor` + `bcos-evm`（**不含** `bcos-executor` 旧 Host，见 §14 #14）

---

## 1. 背景与动机

当前 `bcos-evm/eth/vm/HostContext.h` 超过 1100 行，同时承担 evmc Host 回调、EIP-2929、预编译、FISCO 权限/地址/余额，且依赖 `bcos-executor` 类型。

**方案 C** 以 evmone `test/state/`（v0.21.0）的 `State` + `Host` + `transition()` 为**语义蓝本**，在 `bcos-evm/eth/state/` **自研生产实现**（`test/state` 属测试代码，不直接引入——见 §18）；`transition()` 仅作 **`eth/` 向量语义基准**；删除旧 `HostContext`。密码学复用 evmone 正式库 `evmone::precompiles`。

---

## 2. 三层语义边界（不变）

| 目录 | 语义基准 | 职责 |
|------|----------|------|
| `bcos-evm/eth/` | **geth / evmone 0.21** | `state/`、`VMInstance`、`transition()`（向量）、`warmTransactionEntry` |
| `bcos-evm/bcos/` | **fisco/release-3.18.0** | `FiscoExecutionContext`、`FiscoStateView`、`FiscoHostExtension`、`FiscoTxExecutor`、`AuthCheck` |
| `bcos-evm/opstack/` | **op-geth**（C6） | 后续扩展 |

**硬约束：**

1. `bcos-evm/eth/**` 零 `#include "bcos-executor` 依赖（`bcos/AuthCheck.h` 等 bcos 层除外）
2. `eth/state` 为自研生产代码，**不引入 evmone `test/` 任何源码/头**；FISCO 扩展点为 `EthHost` 一等接口（见 §7），不打散落分支
3. `bcos-evm` 可单独编译；`eth/state` 单测不链 `bcos-executor`

---

## 3. 目标架构（双执行路径）

### 3.1 FISCO 生产路径（`executeViaHost`）**【目标架构，非现状】**

> **现状：** `TransactionExecutorImpl` 仍走 `m_hostContext.prepare()` / `execute()`，无 `executeViaHost`、`applyStateDiff`；`consumeBalance` 分支未接线（始终 `buyGas`/`refundGas`）。以下为目标态。

```
TransactionExecutorImpl
  ExecutePhase::Prepare
    → FiscoTransactionPrepare（触达 ReadWriteSetStorage，DAG 读集）
  ExecutePhase::Execute
    → [L-A] buyGas 或 legacy：直接 executeViaHost → consumeBalance
    → executeViaHost（Exec-A）
        deriveMessage（CR-A）
        → checkAuth（Auth-A，AuthCheck.h）
        → EIP-7623 / transferBalance / consumeTransferGas（Pre-A）
        → warmTransactionEntry（E-C）
        → syncWait { State + Host::call }
    → [D-A] if SUCCESS: co_await applyStateDiff
    → refundGas 或 consumeBalance（L-A）
  ExecutePhase::Finalize
    → makeReceipt（读 FiscoExecutionContext::logs，Log-B）
```

**不走** `transition()` 的余额段（FIB-75 / 3.18.0 对齐）。

### 3.2 eth/ 向量路径（`transition`）

```
InMemoryStateView
  → transition(state_view, block, tx, rev, vm, props, &EthHostExtension)
```

用于 `eth/state` 单测与 geth/evmone 向量门禁（EthGate-A）。  
未来 `EthTxExecutor` 生产仍走 `executeViaHost` + `EthHostExtension`，可保留 buyGas/refundGas。

### 3.3 与现状对比

| 维度 | 现状 | 方案 C |
|------|------|--------|
| Host | `HostContext<Policy>` + C 回调 | 自研 `EthHost`（参考 evmone 语义） |
| FISCO 入口 | `HostContext::execute()` | `executeViaHost()` |
| eth 向量 | 手工维护 HostContext stub | `transition()` |
| 状态（执行期） | 直写 RollbackableStorage | `State` journal |
| 状态（提交） | 已写 storage | `applyStateDiff`（仅 SUCCESS） |
| Transient | `RollbackableTransientStorage` | `State` 内置（T-A） |
| 预编译 0x01–0x11 | `BuiltinPrecompiles` | 自研分发 + 复用 `evmone::precompiles` 原语（P-A） |

---

## 4. 目录结构

```
bcos-evm/
├── eth/
│   ├── state/                    # 自研生产实现，语义参考 evmone v0.21.0 test/state/（不引入 test 源码）
│   ├── vm/                       # VMInstance, revision.hpp
│   ├── policy/
│   │   ├── HostExtension.h       # §5.3
│   │   └── EthHostExtension.h
│   ├── execution/
│   │   ├── warmTransactionEntry.h
│   │   └── ExecuteViaHost.h      # 编排层入口声明（实现在 bcos/ 或 transaction-executor）
│   └── gas/                      # EthTxGasSettlement（去 executor 依赖）
│
├── bcos/
│   ├── FiscoExecutionContext.h   # 替代 HostContext（Log-B）
│   ├── FiscoTransactionPrepare.h # Prepare 阶段（Prep-A）
│   ├── FiscoStateView.h/.cpp     # 每次 syncWait 穿透 storage（§5.1）
│   ├── FiscoBlockInfo.h
│   ├── FiscoHostExtension.h/.cpp
│   ├── FiscoTxAdapter.h          # deriveMessage + tx 映射（CR-A）
│   ├── StateDiffApplier.h/.cpp   # async applyStateDiff
│   ├── FiscoTxExecutor.h
│   └── AuthCheck.h
│
└── opstack/                      # C6
```

**C4 删除：** `eth/vm/HostContext.h`、`eth/eip2929/*`、`eth/precompiled/PrecompiledRegistrar.*`、`eth/precompiled/BuiltinPrecompiles.*`

---

## 5. 核心接口

### 5.1 `FiscoStateView`（冷状态，每次穿透 async storage）

```cpp
class FiscoStateView : public bcos::evm::state::StateView {
    // 每次 get_account / get_account_code / get_storage：
    // task::syncWait(readFromRollbackableStorage(...))  // 与 HostContext::get_balance 相同模式
};
```

已被 `State` journal 修改的热账户由 `State::find()` 服务，不回落到 `StateView`。

### 5.2 `State` + `applyStateDiff`

- 执行期：EVM 写入仅进 `State` journal
- **D-A：** 仅 `EVMC_SUCCESS` 时 `co_await applyStateDiff(storage, stateDiff)`
- **W-C：** `applyStateDiff` 在 `refundGas` / `consumeBalance` **之前**
- `REVERT` / 其它失败：丢弃 `State`，不 `applyStateDiff`；`refundGas` 按 3.18.0 规则 rollback

### 5.3 `HostExtension`（自研 `EthHost` 的一等扩展接口，见 §7）

```cpp
namespace bcos::evm::state {
struct Account;  // forward

struct HostExtension {
    virtual ~HostExtension() = default;
    virtual bool allowSelfdestruct(const Account& acc) { return true; }
    virtual bool allowDelegateCallToPrecompile() { return true; }
    virtual bool skipHostValueTransfer() { return false; }
    virtual std::optional<evmc::Result> callFiscoPrecompile(
        evmc_revision rev, const evmc_message& msg) { return std::nullopt; }
};

// FISCO 生产扩展（仅 executeViaHost 路径注入；eth 向量用 nullptr）
struct FiscoHostExtension : HostExtension {
    // Hook#8：CREATE 帧入口（initcode 前，含嵌套）— nonce + createAuthTable（§21）
    virtual void onCreateFrameEntry(evmc_message& msg, Account& recipient) {}
    // Hook#7 动态预编译改写为 EthHost 内建逻辑（解析 code 前缀），非虚接口
};
}
```

**Auth-A 职责（编排 + `FiscoHostExtension`）：**

| 能力 | 位置 |
|------|------|
| `checkAuth` | `executeViaHost` → `AuthCheck.h` |
| CREATE 建权限表 | `FiscoHostExtension::onCreateFrameEntry` → `createAuthTable`（Hook#8，§21） |
| CREATE 地址 | `FiscoTxAdapter::deriveMessage`（CR-A） |
| gas 预扣/退还 | `FiscoTxExecutor` buyGas/refundGas（L-A） |

`FiscoHostExtension` 默认值：`allowSelfdestruct→false`，`allowDelegateCallToPrecompile→false`，`skipHostValueTransfer→true`（当 `enable_balance_transfer`），`callFiscoPrecompile` 分发 0x1000+。

### 5.4 `FiscoExecutionContext`（Log-B）

替代 `TransactionExecutorImpl::Data::m_hostContext`：

- `evmc_message message`（经 `deriveMessage` 处理）
- `RevisionConfig revisionConfig`
- `std::vector<protocol::LogEntry> logs()`（`executeViaHost` 从 `host.take_logs()` 转换写入；**须在 `State` 丢弃判定前捕获**，顶层 `fix_revert_logs` 门控清空保持 `makeReceipt` 不变——见 §19）
- `gasSettlementSnapshot`（EIP-7623 结算，与 today 相同）

### 5.5 `transition()`（仅 eth/ 向量）

```cpp
TransactionReceipt transition(
    const StateView& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, const Transaction& tx,
    evmc_revision rev, evmc::VM& vm,
    const TransactionProperties& tx_props,
    HostExtension* ext = nullptr);
```

---

## 6. FISCO 3.18.0 差异映射（Grill 后）

| FISCO 行为 | 注入点 |
|-----------|--------|
| CREATE 地址 | `FiscoTxAdapter::deriveMessage` + `EthHost` CREATE 钩子：预填 `recipient` 时跳过以太坊 CREATE（CR-A） |
| 禁 selfdestruct | `allowSelfdestruct→false`（`EthHost` 钩子，SD-A） |
| ContractAuth | `checkAuth`：`executeViaHost`→`AuthCheck.h`；CREATE 建表：`FiscoHostExtension::onCreateFrameEntry`（Auth-A + Hook#8，§21） |
| Feature flag EIP | `FiscoBlockInfo` + `RevisionConfig` |
| FISCO 预编译 0x1000+ | `callFiscoPrecompile`（P-A） |
| buyGas/refundGas FIB-75 | `FiscoTxExecutor`（`bugfix_gas_payment_balance_precheck` ON） |
| legacy consumeBalance | `FiscoTxExecutor`（precheck OFF，L-A） |
| sealer→coinbase | `FiscoBlockInfo::coinbase` + `warmTransactionEntry`（E-C） |
| DELEGATECALL 预编译 | `allowDelegateCallToPrecompile→false`（`EthHost` 钩子，DC-A） |
| enable_balance_transfer | `executeViaHost` 预 transfer + `skipHostValueTransfer`（Pre-A） |
| EIP-7623 calldata gas | `executeViaHost` 编排层（Pre-A） |
| DAG Prepare 读集 | `FiscoTransactionPrepare`（Prep-A） |

**验收：** 同 tx + 同 feature flag → 与 `release-3.18.0` receipt/status/gas 一致。

---

## 7. 自研 `EthHost` 与 FISCO 扩展接口清单（原 Patch-A 作废，2026-06-18 修订）

**语义蓝本：** evmone tag `v0.21.0` 的 `test/state/host.cpp`（仅参考，不引入源码）。`bcos-evm/eth/state/` 自研 `EthHost`，命名空间 `bcos::evm::state`。FISCO 差异**不再以"打 patch"形式散落**，而是收敛为 `EthHost` 在固定扩展点回调 `HostExtension`（§5.3）。

### 7.1 `EthHost` 扩展点（取代原 6 处 patch 白名单）

| # | `EthHost` 位置 | 扩展行为 | 接口 |
|---|---------------|---------|------|
| 1 | 构造 | 持有 `HostExtension* m_ext`（可空=纯 eth 向量） | — |
| 2 | CREATE/CREATE2 地址 | `recipient` 已预填（`deriveMessage`）→ 跳过以太坊地址计算，仅 `access_account`（CR-A） | 编排预填 + Host 内分支 |
| 3 | `selfdestruct` | `!allowSelfdestruct(acc)` → 不改 State 返回 false（SD-A） | `allowSelfdestruct` |
| 4 | `call`/消息分发 | `callFiscoPrecompile` 优先于 ETH 预编译（0x1000+，P-A） | `callFiscoPrecompile` |
| 5 | `call`/消息分发 | `DELEGATECALL && !allowDelegateCallToPrecompile && is_precompile` → 走空 code（DC-A） | `allowDelegateCallToPrecompile` |
| 6 | `call`/消息分发 | `skipHostValueTransfer()` → 跳过 CALL value 转账（Pre-A） | `skipHostValueTransfer` |
| 7 | `call`/消息分发 | 动态预编译：合约 code 含 `"addr,..."` 前缀 → 改写 recipient/code_address + ABI 编码 | **EthHost 内建**（Hook#7，非 Ext 虚接口） |
| 8 | CREATE 帧入口（initcode 前，含嵌套） | FISCO nonce 语义（`setNonce("1")`/web3 嵌套 `increaseNonce`/`fix_nonce_init`）**+ `createAuthTable`（FIB-82 表名，§21）** | `onCreateFrameEntry` 钩子（`FiscoHostExtension` 持编排上下文） |

**约束：** FISCO 差异经 `HostExtension`/`FiscoHostExtension` 注入或 `EthHost` 内建 Hook#7；禁止散落 FISCO 条件分支。扩展点 8 个（§12-5）：构造注入 `m_ext` + 7 行为钩子。

---

## 8. 执行数据流（FISCO Web3 tx，FIB-75 ON）**【目标架构】**

```
updateNonce
→ buyGas()                                    // RollbackableStorage 预扣 gas
→ executeViaHost(ctx)
    deriveMessage(msg)
    checkAuth()                                 // 失败则返回，不跑 EVM
    debitEip7623Calldata(msg)
    co_await transferBalanceIfEnabled()         // storage
    consumeTransferGas(msg)
    warmTransactionEntry(host, tx, block)
    syncWait { State(view); Host::call(msg) }
    ctx.logs = convert(host.take_logs())
→ if (status == SUCCESS) co_await applyStateDiff(storage, diff)
→ refundGas()                                 // 失败非 REVERT：rollback(afterBuyGasSavepoint)
→ makeReceipt(ctx)
```

**Legacy（precheck OFF）：** 无 buyGas；`executeViaHost` → `consumeBalance`（rollback `m_startSavepoint`）。

---

## 9. async / sync 边界（Exec-A）

| 层 | 模型 |
|----|------|
| `transaction-executor` | `bcos::task::Task` 协程 |
| `Host::call` | `syncWait` 同步（与 today `HostContext` 相同） |
| `FiscoStateView::get_*` | 回调内 `syncWait` 穿透 `RollbackableStorage` |
| `applyStateDiff` | `co_await` 异步写回 |
| `transition()` | 仅 eth 向量；可在测试中 `syncWait` 包装 |

---

## 10. 测试策略

| 层级 | 路径 | 基准 |
|------|------|------|
| `eth/state/` | `transition()` + `InMemoryStateView` | evmone 0.21 / geth |
| `bcos/` | `transaction-executor/tests/*`、3.18.0 快照 | `release-3.18.0` |
| 集成 | `CompatStateTransitionTest`（自 `CompatHostContextTest` 迁移） | CI |
| scheduler | FIB-98~104、Prepare 读集 | 不变（Prep-A） |

---

## 11. 分期交付（Grill 后 + §15–§21 回填）

| 阶段 | 内容（含补遗回填） | 产出 / 验收 |
|------|------|------|
| **C0** | 自研 `eth/state`（参考 evmone，**不引入 test 源码** §18）+ `EthHost` 8 扩展点（§7）；portfile 增装 `keccak.hpp`/`secp256k1.hpp`（§18.3）；保留 fork evmc `hash_fn`/`isSMCrypto`（§16）；**不**引入 `test/utils` mpt/rlp（§18.4） | `bcos-evm` 编译通过；`evmc_host_context` 仍含 `hash_fn`；接口审计 8 点（§12-5） |
| **C1** | `FiscoStateView` + `StateDiffApplier`；…；**T2**：抽 gas 符号入 `bcos-evm`、删 `ModexpGas.cpp` 冗余 include | 读写 FISCO storage；**gas 相关** `eth/**` 零 `bcos-executor` include；**全量零 include = C4**（§12-3） |
| **C2** | `transition()` 向量测试；**不比 state/receipt root**（Q20）；按需补 eth-向量-only 文件 | eth/ 门禁绿灯（status/gas/logs/output vs geth） |
| **C3** | `executeViaHost` + `FiscoExecutionContext` + `FiscoHostExtension` + Prepare；错误处理 catch 表 + `fix_error_handling` 门控（§20.1）；revert logs **先转存再丢 State** + 顶层 flag 门控（§19）；`createAuthTable` 走 CREATE 帧入口钩子（§21）；`checkAuth`（Auth-A）；确认 Hook#2 对**嵌套 CREATE** 生效（§16.2） | 3.18.0 回归（**含国密链** + L-A 双 gas 路径） |
| **C4** | 删 `HostContext` / `eip2929/*` / `BuiltinPrecompiles`（连带消除 §17 中 2 处 `bcos-executor` 依赖） | **FISCO 自维护 Host 代码 -X%**（指标修订，§15.4） |
| **C5** | `TransactionExecutorImpl` 切换至 `executeViaHost` + C3 回归复跑 + scheduler Prepare 读集 CI（FIB-98~104） | 非 DAG 写集端到端（写集/DAG 不在本 spec，§15.2） |
| **C6** | opstack / EIP-7702 | 可选 |
| **C7+** | `bcos-executor` 旧 Host 迁移（双轨一致性见 §13） | 可选（Scope-A 外） |

---

## 12. 成功标准

1. **eth/ 向量：** `transition()` 下 Prague/Osaka 向量与 geth 一致
2. **bcos/ 生产：** `executeViaHost` 下与 `release-3.18.0` 一致（含 L-A 双 gas 路径）
3. **依赖：** C1 = gas 相关 `eth/**` 零 `bcos-executor` include；**C4 后全量零 include**（§12-3）
4. **构建：** `bcos-evm` 独立编译通过
5. **接口审计：** FISCO 差异仅经 §7.1 的 8 个 `EthHost` 扩展点/`HostExtension` 接口注入，`EthHost` 内无散落 FISCO 分支

---

## 13. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 双状态模型 | 执行期 `State` journal；仅 SUCCESS `applyStateDiff`（D-A） |
| syncWait 嵌套 | 与 today HostContext 相同模式；在协程 executor 上运行 |
| 自研漂移（与 evmone 语义偏离） | 以 evmone tag `v0.21.0` 为语义基准，自研代码定期对照 upstream diff + eth 向量门禁（§10） |
| 自研工作量上调 | 自研 `State`/`EthHost`/`transition()` + 预编译分发 > 原"拷贝+patch"；§11 C0/C2 工期需重估，crypto 复用 `evmone::precompiles` 以减负 |
| bcos-executor 双轨 | Scope-A：本期不迁移，文档标注 C7+ |

---

## 14. Grill 决策记录（2026-06-18）

| # | 问题 | 决策 |
|---|------|------|
| 1 | FISCO gas/余额 vs `transition()` | **A**：FISCO 用 `executeViaHost`，不用 `transition()` 余额段 |
| 2 | `FiscoStateView` 读路径 | 每次 `get_account` `syncWait` 穿透 async storage |
| 3 | 写路径 | **W-C**：State journal → `applyStateDiff` → `refundGas` |
| 4 | `applyStateDiff` 时机 | **D-A**：仅 `EVMC_SUCCESS` |
| 5 | Transient storage | **T-A**：自研 `State` 内置；删 `RollbackableTransientStorage` |
| 6 | Legacy gas | **L-A**：恢复 `consumeBalance` 分支（precheck OFF） |
| 7 | EIP-2929 预热 | **E-C**：`warmTransactionEntry()` 薄层 |
| 8 | ETH 预编译 | **P-A**：自研分发 + 复用 `evmone::precompiles` 原语 |
| 9 | CREATE 地址 | **CR-A**：编排层 `deriveMessage` + `EthHost` 钩子 |
| 10 | Prepare / DAG | **Prep-A**：`FiscoTransactionPrepare` |
| 11 | checkAuth | **Auth-A**：编排层 `checkAuth` + CREATE 建表（`onCreateFrameEntry`，§21） |
| 12 | EIP-7623 / value transfer | **Pre-A**：编排层 + `skipHostValueTransfer` |
| 13 | Logs | **Log-B**：`FiscoExecutionContext` |
| 14 | 范围 | **Scope-A**：不含 `bcos-executor` 旧 Host |
| 15 | eth 测试 vs EthTxExecutor | **EthGate-A**：向量 `transition()`；生产 `executeViaHost` |
| 16 | selfdestruct | **SD-A**：`EthHost` 钩子 `return false` |
| 17 | DELEGATECALL 预编译 | **DC-A**：跳过 `call_precompile` |
| 18 | sync 包装 | **Exec-A**：`syncWait` Host + `co_await applyStateDiff` |
| 19 | Host 实现 | ~~Patch-A：§7.1 六处白名单~~ → **修订（2026-06-18）：自研 `EthHost` + 8 个扩展接口（§7）；`test/state` 不直接引入（§18）** |
| 20 | eth 向量 root 比对 | **Q20**：**不比** state/receipt root；比对 status/gas/logs/output；不引入 mpt/rlp（§18.4） |

---

## 15. 评审补遗（代码核验，2026-06-18）

> 对照实际代码（`bcos-evm/eth/vm/HostContext.h`、`bcos-evm/bcos/FiscoPolicy.h`、`transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`、`ports/evmone/portfile.cmake`）核验后的发现。Grill 决策**意图**不变；§18 等已对 §1–§14 **正文**做同步修订（自研 `EthHost`、不引入 `test/state`）。以下为进入实现前需补齐的内容。

### 15.1 已核实属实

- `HostContext.h` = 1110 行；`transaction-executor/vm/HostContext.h` 为转发头 → `bcos-evm/eth/vm/HostContext.h`。
- §6 映射与现状一致：`deriveMessage`、`selfdestruct→false`、`allowDelegateCallToPrecompile→false`、`checkAuth` 当前均在 `FiscoPolicy`。
- evmone port pin `ywy2090/evmone` v0.21.0。

### 15.2 原阻断项（已解决）

**B1（🟠 国密/Hash 注入）：** 现状 `HostContext` 注入 `.isSMCrypto` + `.hash_fn = evm_hash_fn`（`HostContext.h:294-295`），国密链用 SM3 算 SHA3 opcode / CREATE 地址 / codeHash；evmone `state::Host` 默认 keccak256。**已在 §16 解决**：经核验 SM3 经"fork evmc `hash_fn`（解释器）+ `deriveMessage`（地址，CR-A）+ State（codeHash）"三路覆盖，**不需新增 `HostExtension` 注入点**，但产生 1 条新硬约束（必须用 fork 版 evmone/evmc）与 3 个 C0/C1 验收项。详见 §16。

> 注：写集传播 / DAG 可见性（State journal → `applyStateDiff` → 写锁）按决定**不在本 spec 处理**。

### 15.3 高优待办

| # | 问题 | 行动 |
|---|------|------|
| T1 | §7.1 扩展点可能不足 | 见 **§15.5 穷举映射表**；结论：`EthHost` 扩展点需扩到 **8 个**（§7 已更新）；`HostExtension` 接口**不新增**（Hash 注入经 §16 三路覆盖） |
| T2 | §2 约束1 当前未满足 | 见 **§17**；4 处 include 中 2 处随 C4 自动消除，真正需迁移仅 `EthTxGasSettlement.h`（抽 `Eip7623Components`/`calcEip7623Components`/`Eip2930AccessList` 到 `bcos-evm`，反转依赖）+ 确认 `ModexpGas.cpp` 冗余 include |
| T3 | evmone `test/state` 获取方式未定 | 见 **§18（修订）**：`test/state` 属测试代码，**不直接引入**；改为**参考语义自研** `eth/state`，crypto 复用 `evmone::precompiles` 生产库。推翻 §2约束2/§7/Q19 的 patch 白名单模型，**已同步修订（§18.5）** |
| T4 | revert logs 语义 | 见 **§19**：日志容器独立于 D-A，执行后先转存再丢 State；顶层 `fix_revert_logs` 门控清空保持 `makeReceipt` 不变（旧链 OFF 仍发日志） |

### 15.4 次要

- **C4 "-60% 代码"** 过于乐观（新增**自研** state/EthHost + 多个 Fisco 组件，自研量大于"拷贝+patch"）；建议指标改为"FISCO 自维护 Host 代码 -X%"。
- **Scope-A 双轨**：补一句旧/新 Host 并存期一致性保证（共享 `RevisionConfig`/precompile 实现）。
- **分期**：C2（`transition()` 向量）仅依赖 C0，与 C1 可并行。

### 15.5 现状 `HostContext` FISCO 分支穷举映射（T1）

> 逐条盘点 `bcos-evm/eth/vm/HostContext.h` 中所有偏离 evmone `state::Host` 标准语义的逻辑，定其去向。去向取值：**Hook**（`EthHost` 扩展点，§7）/ **Ext**（`HostExtension` 接口方法）/ **编排**（`executeViaHost` 等编排层）/ **删除**（由自研 `State`/`EthHost` 取代）/ **State**（FISCO `FiscoStateView`/Account 承载）。
>
> 注：原"Patch#N"已随 §7 修订改为"Hook#N"（自研 `EthHost` 扩展点，非 vendor 打补丁）。

| # | 现状逻辑 | 锚点 | 去向 | 是否在 §5.3/§7.1 已覆盖 |
|---|---------|------|------|------------------------|
| 1 | SM3 hash（`isSMCrypto`+`evm_hash_fn`）：SHA3 opcode / CREATE 地址 / codeHash | :86,:294-295 | **保留 fork evmc `hash_fn`**（解释器）+ **编排**(`deriveMessage`,CR-A) + **State**(codeHash) | ✅ §16（不新增 Ext） |
| 2 | `deriveMessage`：CREATE/CREATE2 地址、web3 legacy 地址 | :309 / FiscoPolicy:109 | **编排**（`FiscoTxAdapter::deriveMessage`）+ **Hook#2** | ✅ CR-A |
| 3 | `processDynamicPrecompiled`：code 前缀 `"addr,..."` 改写 recipient/code_address + ABI 编码 | :1006-1044 | **Hook#7**（§7 新增） | ✅ §7 Hook#7 |
| 4 | `createAuthTable`：CREATE 时建权限表（含 FIB-82 raw_address hex 路径） | :950 | **Hook#8 同点**（CREATE 帧入口，initcode 前；含嵌套） | ✅ §21（解除） |
| 5 | CREATE nonce：`setNonce("1")`、web3 嵌套 `increaseNonce`、`fix_nonce_init` | :955-977 | **Hook#8**（§7 新增） | ✅ §7 Hook#8 |
| 6 | `setCode` 连带存 ABI 字符串（`m_abi`） | :973 | **State**（`FiscoStateView`/Account 写 ABI） | ❌ 缺（evmone `State` 无 ABI 概念） |
| 7 | `checkAuth`（`enable_auth_check`） | :541-561 | **编排**（`AuthCheck.h`） | ✅ Auth-A |
| 8 | EIP-7623 calldata 预扣 + `captureGasSettlementSnapshot` | :567-584,:223-242 | **编排** | ✅ Pre-A |
| 9 | `transferBalance` + `enable_balance_transfer` + `fix_delegatecall_transfer` | :250-278,:587-599 | **编排** + **Ext**(`skipHostValueTransfer`) + **Hook#6** | ✅ Pre-A |
| 10 | `consumeTransferGas`（level 0 扣 `BALANCE_TRANSFER_GAS`） | :994-1004 | **编排** | ✅ Pre-A |
| 11 | selfdestruct → false | :799-803 | **Ext**(`allowSelfdestruct`) + **Hook#3** | ✅ SD-A |
| 12 | DELEGATECALL 预编译 gate | :1046-1055 | **Ext**(`allowDelegateCallToPrecompile`) + **Hook#5** | ✅ DC-A |
| 13 | FISCO 预编译分发（`m_preparedPrecompiled`/`callPrecompiled`） | :1065-1102 | **Ext**(`callFiscoPrecompile`) + **Hook#4** | ✅ P-A |
| 14 | EIP-2929 自研（`Eip2929AccessState`/CheckpointGuard/warm/create pin） | :527-531,:691-746,:896-920 | **删除** + **编排**(`warmTransactionEntry`) | ✅ E-C（注：删旧实现，依赖自研 `State` 的 access tracking） |
| 15 | 错误处理 flag（FIB-88/89/91/92，`fix_error_handling`） | :611-664 | **编排**（`executeViaHost` 复刻 catch→EVMCResult 映射，flag 门控） | ✅ §20.1（解除） |
| 16 | 失败 rollback + `m_logs.clear()` / `fix_revert_logs` | :666-671 | **State 丢弃（D-A）+ 编排**：日志先转存再丢 State；顶层 flag 门控清空 | ✅ §19（解除） |
| 17 | `sstoreStatus` / `fix_storage_status`（EIP-2200 status） | :871-893 | **自研 `EthHost::set_storage` 复刻 FISCO 4态/2态**（禁用 evmone 9 态机） | ✅ §20.2（解除） |
| 18 | tx_context：coinbase/timestamp/chainId/gasPrice/blockHash（`convertTimestamp`/`use_web3_timestamp`） | :459-483,:813-836 | **编排**(`FiscoBlockInfo`) + **State**(block_hash) | ✅ E-C/FiscoBlockInfo |
| 19 | `getExecutable` LRU 缓存 | :100-123 | **编排**（保留，性能优化） | — 不影响语义 |

**结论：**

- **`EthHost` 扩展点 6 → 8 个**（§7.1）：Hook#7 动态预编译（**EthHost 内建**）+ Hook#8 CREATE 帧入口（**nonce + createAuthTable**，`FiscoHostExtension::onCreateFrameEntry`）。
- **`HostExtension` 基类 4 虚方法不变**；FISCO 专有扩展在 `FiscoHostExtension`（+1 `onCreateFrameEntry`）。Hash 经 §16 三路覆盖。
- **语义对齐点已全部解除**（§19–§21）；行 6 ABI 待 C1 `FiscoStateView` 设计落地。
- §15.5 全表去向已定。

---

## 16. 国密(SM3)/Hash 注入设计（B1）

### 16.1 现状：哈希在两个不同层面注入

代码核验（`HostContext.cpp:7-11`、`HostContext.h:86,294-295`）后，FISCO 的 SM3 注入**不止一处**，且分属两层：

| 层 | 机制 | 现状 |
|----|------|------|
| **解释器层**（SHA3/KECCAK256 opcode 0x20，以及 evmone 内部 CREATE2 salt 哈希） | `evmc_host_context.hash_fn` + `.isSMCrypto` 字段 | **这两个字段是 `ywy2090/evmone` fork 对 evmc 的 patch，非上游成员**；`evm_hash_fn` 回调读全局 `executor::GlobalHashImpl::g_hashImpl` |
| **Host 层**（CREATE/CREATE2 地址、codeHash） | `deriveMessage`（用 `crypto::Hash&`）、`Account::setCode(codeHash)` | 不经 `hash_fn`，各自直接调 `hashImpl.hash()` |

### 16.2 结论：B1 可收敛为 3 个精确集成点（无需"到处加 Ext.hash"）

1. **解释器 SHA3 opcode（必须保留 fork 机制）：** 自研 `EthHost` 仍由 VM(`VMInstance`/evmone baseline，evmone 正式库) 执行，VM 通过 `evmc_host_context.hash_fn` 取哈希。
   → **行动：** `executeViaHost` 构造传给 `vm.execute` 的 host_context 时，**必须继续填充 fork 的 `.hash_fn`/`.isSMCrypto`**。这要求继续用 fork 版 evmone/evmc 库（不可换上游 evmc）。**列入 §2 硬约束 / C0 验收：`evmc_host_context` 仍含 `hash_fn`/`isSMCrypto`。**

2. **CREATE/CREATE2 地址（已被 CR-A 覆盖，需确认嵌套）：** 顶层与嵌套 CREATE 地址均由 `FiscoTxAdapter::deriveMessage`（SM3）+ `EthHost` CREATE 钩子（Hook#2：`recipient!=0` 跳过以太坊地址计算）产出。现状嵌套路径 `externalCall` 已对 CREATE 调 `deriveMessage`（`HostContext.h:699`）。
   → **行动：** 确认 Hook#2 在自研 `EthHost` 的消息分发对**嵌套 CREATE/CREATE2** 同样生效（不仅顶层 tx）。`EthHost` 的以太坊地址计算（keccak）由此被完全旁路。**无需新增 `HostExtension::hash()`。**

3. **codeHash（State 层承载）：** EXTCODEHASH / 账户 code_hash 由 `FiscoStateView`/Account 返回**存储的 SM3 值**，写入时由 Account `setCode` 计算（现状 `HostContext.h:972`）。自研 `State::Account.code_hash` 的 keccak 计算被旁路。
   → **行动：** `FiscoStateView::get_account_code_hash`（或等价）返回存储值；C1 的 Account 写路径负责算 SM3。**确认自研 `EthHost::get_code_hash` 走 `StateView` 而非用 `State` 内重算的 keccak。**

### 16.3 对 §5.3 / §7 的增量

- **`HostExtension` 是否需要 `hash()`？** —— 经上分析**不需要**作为通用注入点；SM3 经"fork evmc `hash_fn`（解释器）+ `deriveMessage`（地址）+ State（codeHash）"三路覆盖。**§15.2 B1 据此降级：从"接口缺失"改为"3 个集成约束 + 验收项"，不改 §5.3 接口。**
- **§2 新增硬约束：** evmone 解释器/evmc **必须用 fork 版（保留 `evmc_host_context.hash_fn`/`isSMCrypto`）**，禁止替换为上游 evmc，否则 SHA3 opcode 在国密链失效。
- **遗留风险（记录）：** `evm_hash_fn` 用**全局** `g_hashImpl`，隐含"单节点单 crypto 类型"假设；跨群组混合 SM/非 SM 时不安全。本期沿用现状假设，标注待后续。

> §16 修订 §15.5 行 1 的去向：**不新增 `HostExtension` 注入点**；改为"保留 fork evmc hash_fn + CR-A + State"三路。§15.3 结论同步更新为 **`EthHost` 扩展点 6 → 8 个；`HostExtension` 接口不新增**。

---

## 17. `eth/**` 去 `bcos-executor` 依赖迁移清单（T2）

### 17.1 现状盘点

`bcos-evm/eth/**` 下共 4 个文件 include `bcos-executor`（违反 §2 硬约束 1）：

| 文件 | include | 实际所需符号 | 处置 |
|------|---------|------------|------|
| `eth/vm/HostContext.h` | `Common.h` + `CallParameters.h` | 多处 | **C4 删除** → 依赖自动消失，无需迁移 |
| `eth/eip2929/Eip2929TransactionPrewarm.h` | `CallParameters.h` | `executor::Eip2930AccessList` | **C4 删除**（`eip2929/*` 整体删，E-C 取代）→ 无需迁移 |
| `eth/gas/EthTxGasSettlement.h` | `Common.h` + `CallParameters.h` | `Eip7623Components`、`calcEip7623Components`、`Eip2930AccessList` | **必须迁移**（该文件保留） |
| `eth/precompiled/ModexpGas.cpp` | `Common.h` | **无**（冗余；符号来自 `bcos-utilities/Common.h` + `DataConvertUtility.h`，后者经 `ModexpGas.h` 间接引入） | **删 include**；若编译失败则显式 `#include "bcos-utilities/DataConvertUtility.h"` |

> 结论：4 处里 **2 处随 C4 自动消除**、**1 处（ModexpGas.cpp）冗余可直接删**，真正需迁移的只有 `EthTxGasSettlement.h`。工作量远小于初判。

### 17.2 待迁移符号（均为纯类型/纯函数，无 storage 依赖）

| 符号 | 现位置 | 性质 | 目标位置 |
|------|--------|------|---------|
| `Eip7623Components`（struct） | `bcos-executor/src/Common.h:237` | 纯 POD | `bcos-evm/eth/gas/`（新 `Eip7623.h`，`namespace bcos::evm::gas`） |
| `calcEip7623Components(bytesConstRef)` | `Common.h:244`（`inline`） | 纯计算 | 同上 |
| `Eip2930AccessList`（`using`） | `bcos-executor/src/CallParameters.h:13` | 纯 typedef | `bcos-evm/eth/`（新 `AccessList.h` 或并入 gas 头） |
| `EMPTY_EVM_BYTES32` / `VMSchedule` | `Common.h:59 / :172` | 常量/表 | 仅 `ModexpGas.cpp` 确认需要时再迁；否则不动 |

### 17.3 迁移策略（关键：反转依赖方向）

`Common.h` 体量 423 行且自身 `#include "vm/EVMCWasm.h"` + 多个 storage 头——**eth/ 依赖它是依赖方向错误**。正确做法：

1. 把上述纯符号**抽到 `bcos-evm/eth/`** 下的小头文件，命名空间归 `bcos::evm::gas`/`bcos::evm`。
2. `bcos-executor/src/Common.h` **反向 include** 新头并 `using executor::Eip7623Components = bcos::evm::gas::Eip7623Components;`（别名回兼，避免改动 executor 侧大量调用点）。
3. `EthTxGasSettlement.h` 改 include 新头，删 `bcos-executor` include；`TxGasSettlementContext.calldata` 字段类型改 `bcos::evm::gas::Eip7623Components`。

这样依赖方向变为 `bcos-executor → bcos-evm`（符合分层），且 executor 侧调用点零改动。

### 17.4 验收

- `rg '#include "bcos-executor' bcos-evm/eth` **仅剩 0 处**（C4 后）。
- `bcos-evm` 独立编译通过（§12-4）。
- 列为 **C0/C1 前置**：C1 `FiscoStateView`/gas 编排开始前完成抽取，否则 `eth/` 无法独立编译。

---

## 18. `eth/state` 实现来源：参考 evmone，不直接引入（T3，修订）

> **决策（2026-06-18）：** evmone 的 `state::State`/`Host`/`transition()`/precompiles 全部位于 **`test/` 目录**，属测试/参考代码，**不作为生产依赖直接 vendor**。`bcos-evm/eth/state/` 改为**自研生产实现**，以 evmone `test/state/`（v0.21.0 @ `3585c2cb`）为**语义蓝本**（geth/evmone 对齐基准）。
>
> ⚠️ **此决策推翻了原 §2 约束2 / §7 / §14-Q19（Patch-A）的"vendor host.cpp + 白名单 patch"模型** —— 详见 §18.5。

### 18.1 参考蓝本 → 自研组件映射

逐文件以 evmone `test/state/` 为语义参考，在 `bcos-evm/eth/state/`（`namespace bcos::evm::state`）**重写为生产代码**：

| evmone 参考文件 | 自研组件 | 说明 |
|----------------|---------|------|
| `state.hpp/.cpp`（`State`/journal/`transition()`） | `State` + `transition()` | 参考 journal/revert 语义重写 |
| `host.hpp/.cpp`（`state::Host`） | `EthHost`（含 FISCO 扩展点） | **不再是"打 patch"，而是自有 Host**，FISCO 钩子作为一等接口（取代 §7.1 白名单） |
| `state_view.hpp` / `state_diff.hpp` | `StateView` / `StateDiff` | 接口照搬语义，`FiscoStateView` 实现之 |
| `account.hpp` / `transaction.hpp` / `block.hpp` | 对应结构 | 重写；FISCO 字段（ABI 等）一并纳入 |
| `bloom_filter.{hpp,cpp}` / `errors.hpp` | 同名组件 | 算法照搬（纯函数，可逐行参考） |
| `precompiles*.{hpp,cpp}` | `EthPrecompiles` 分发器（**P-A**） | 见 §18.2：**crypto 原语复用 evmone 生产库**，仅 dispatch/gas 自研 |
| `system_contracts` / `requests` / `ethash_difficulty` / `block.cpp` 终结算 | 仅 eth 向量需要 | FISCO `executeViaHost` 不触发；按需最小实现 |

### 18.2 可直接链接的 evmone **生产库**（非 test，允许依赖）

区别于 `test/state/`，以下是 evmone 已安装的**正式库**，可直接 link，无需重写：

- **`evmone::evmone`**：baseline 解释器（VM 执行核）——现状已用（`VMInstance`）。
- **`evmone::precompiles`**：crypto 原语库（sha256/ripemd160/bn254/bls/kzg/blake2b/secp256r1/modexp/keccak/secp256k1）——**预编译的密码学计算复用它**，自研代码只写"地址分发 + gas 计量 + revision 门控"。

→ 这样 `eth/state` 自研量集中在 **状态模型 + Host 编排 + 预编译分发**，而非密码学。

### 18.3 port 仍需新增安装的头（即便不 vendor test/state）

因 §18.2 复用 `evmone::precompiles` 原语 + 参考实现需对照编译，以下头当前 portfile **未安装**，建议补：

| 缺失头 | 用途 | 行动 |
|--------|------|------|
| `evmone_precompiles/keccak.hpp` | keccak 原语（自研 `hash_utils` 等价物会调用） | portfile 增装 |
| `evmone_precompiles/secp256k1.hpp` | ecrecover 原语 | portfile 增装 |

> 其余 `evmone_precompiles/*` 与 `evmone/{constants,delegation,…}` 已安装。

### 18.4 不引入的部分（明确排除）

- `test/utils/{mpt,mpt_hash,rlp,rlp_encode,stdx}` —— **不引入**。FISCO 两路径均不需 MPT root / 以太坊 RLP（不用 MPT 根；CREATE 地址走 SM3）。eth 向量门禁若需比对 root，另行最小实现，不拷 test 代码。
- `test/state/*` 任何文件 —— **不作为源码或头直接纳入构建**；仅作 review/对照参考。

### 18.5 ⚠️ 对原 spec 的连带影响（已同步修订）

本决策与以下原文冲突，**已同步修订**：

1. **§2 硬约束 2** → 改为"自研 `EthHost`，不引入 evmone test 源码，FISCO 扩展点为一等接口"。
2. **§7 整节 + §14-Q19** → 从"patch 白名单"改为"`EthHost` 扩展接口清单（8 个扩展点）"；§15.5 的去向列 `Patch#N` → `Hook#N`。
3. **§13 风险** → "vendor 漂移"改为"自研漂移（对照 upstream diff）"，并增"自研工作量上调"风险。
4. **§11 C0 / §12-5** → C0 改"自研 eth/state + EthHost 接口"；成功标准改"接口审计"。
5. **正面影响**：`bcos-evm` 不再把 evmone 测试代码编入生产；许可证/边界更干净；FISCO 钩子是显式接口而非散落 patch。

---

## 19. Revert logs 语义对接（T4 / §15.5 行16）

### 19.1 现状：两层、其中顶层 flag 门控

| 层 | 行为 | 是否 flag 门控 | 锚点 |
|----|------|--------------|------|
| **子调用（EVM 内 revert）** | 非 `EVMC_SUCCESS` → 丢弃本帧日志；父帧仅合并成功子调用的日志 | ❌ 无条件（标准 EVM） | `HostContext.h:666-671,713` |
| **顶层 tx** | 顶层失败 → `fix_revert_logs` ON 时清空日志；**OFF 时保留**（旧链兼容） | ✅ `bugfix_revert_logs`（V3_16_4 起） | `FiscoTxExecutor.h:216-219` |

> 关键：`bugfix_revert_logs` **OFF 的旧链**，顶层 receipt 层**不**清空已捕获日志（`FiscoTxExecutor.h:216-219`）。**注意：** 现状 `HostContext::execute` 在失败时亦会 `m_logs.clear()`（`:670`），故完整 execute 路径下「OFF 仍发日志」依赖 §19.3 的**先转存再丢 State**；现有单测多仅覆盖 `makeReceipt` 隔离场景。

### 19.2 与 D-A 的张力（必须显式处理）

§5.2 D-A 规定"仅 `EVMC_SUCCESS` 才 `applyStateDiff`，失败丢弃 `State`"。若日志随 `State`/host 一并丢弃：

- 顶层 revert 时 `take_logs()` 返回空 → **等价于 `fix_revert_logs` 永远 ON** → **破坏 flag OFF 旧链**（§12-2 回归失败）。

### 19.3 自研实现的对接规则

1. **日志容器独立于"是否 applyStateDiff"**：`EthHost` 累积的日志在每次顶层执行结束后，**先无条件转存** `FiscoExecutionContext.logs`（即 §5.4 的 `ctx.logs = convert(host.take_logs())`），**在 `State` 丢弃判定之前**完成捕获。
2. **子调用 revert 丢日志**：由 `EthHost` 在消息分发处保证——失败子帧不向父帧合并日志（等价 `HostContext::externalCall` 现状）。这与 `State` journal 的帧级回滚天然一致。
3. **顶层 flag 门控清空**：**保持 `FiscoTxExecutor::makeReceipt` 现状逻辑不变**——`if (fix_revert_logs) ctx.logs.clear()`。**不可**用"丢 State 即丢日志"替代该 flag 门控。
4. **`take_logs()` 语义**：自研 `EthHost::take_logs()` 返回"已成功合并"的日志集（成功子调用 + 当前帧），**不受 D-A 丢弃影响**。

### 19.4 验收

- flag OFF：顶层 revert 的 receipt **仍含**执行期产生的日志（与 `release-3.18.0` 一致）。
- flag ON：顶层 revert 的 receipt 日志为空。
- 两种情况下，**失败子调用**的日志均不出现（无条件）。
- → 据此 **§15.5 行16 的 ⚠️ 解除**：去向明确为"日志容器独立 + 顶层 flag 门控（makeReceipt 不变）"，非"随 State 丢弃"。

---

## 20. 残留语义对齐确认（§15.5 行15 / 行17）

### 20.1 行15：错误处理 flag（`fix_error_handling`，FIB-88/89/91/92）

**现状**（`HostContext.h:611-664` catch 块）：异常 → `EVMCResult` 的映射受 `fix_error_handling` 门控：

| FIB | flag ON | flag OFF（旧链） |
|-----|---------|----------------|
| 88 | 致命错误 `gas_left = 0`（耗尽全 gas） | 保留 `ref->gas` |
| 89 | OOG 用 `gas_left = 0`（非未初始化值） | — |
| 91 | `checkAuth` 在 try 内（异常触发 rollback） | — |
| 92 | `EVMC_INTERNAL_ERROR` 用 `Unknown` 状态 | 用 `OutOfGas` 状态 |

**结论：** evmone `transition()`/自研 `EthHost` 有各自的错误处理与 gas 耗尽约定，**不能直接采用**。`executeViaHost` **编排层必须保留与现状等价的 try/catch → `EVMCResult` 映射**，并按 `fix_error_handling` 门控 gas/status——否则旧链（OFF）与新链（ON）的失败 receipt 的 `gasUsed`/`status` 均会漂移。
→ **去向：编排层（`executeViaHost`）复刻现状 catch 表，flag 门控保持。** §15.5 行15 ⚠️ 解除。

### 20.2 行17：`sstoreStatus` / `fix_storage_status`（SSTORE gas）

**现状**（`HostContext.h:871-893`）：`set_storage` 返回的 `evmc_storage_status` 受 `fix_storage_status` 门控，**且即使 ON 也是 4 态**（仅依据"当前值是否为零"）：

| | flag ON | flag OFF（旧链） |
|--|---------|----------------|
| newZero & existZero | `ASSIGNED` | `DELETED` |
| newZero & existNonZero | `DELETED` | `DELETED` |
| newNonZero & existZero | `ADDED` | `MODIFIED` |
| newNonZero & existNonZero | `MODIFIED` | `MODIFIED` |

**⚠️ 关键差异：** FISCO 两种 flag 都**不是** evmone 的完整 **9 态 EIP-2200**（evmone 追踪 *original* 值，含 `*_RESTORED`/`*_DELETED_ADDED` 等 dirty 态）。interpreter 由该 status 算 SSTORE gas/refund——

- 若自研 `State::set_storage` **直接采用 evmone 完整状态机**，则 **新旧链 SSTORE gas 全部漂移**（净零改写、回填等场景差异最大），§12 两条成功标准均破。

**结论：** 自研 `State`/`EthHost` 的 `set_storage` **必须复刻 FISCO `sstoreStatus`**（flag 门控的 4 态/2 态），**禁止**沿用 evmone 的 original-value EIP-2200 状态机。
→ **去向：自研 `EthHost::set_storage` 内复刻 `sstoreStatus`（含 `DIRECT` 读避免污染 DAG 读集——见现状 `:880`），flag 门控保持。** §15.5 行17 ⚠️ 解除。

> 备注：这意味着 FISCO 的 SSTORE gas 模型本就**偏离标准 EIP-2200**；本期保持现状语义，不"顺手修正"，以保向前兼容。如未来要对齐标准，须新开 feature flag 单独评估。

---

## 21. `createAuthTable` 编排时机（§15.5 行4 跟进）

### 21.1 现状

`createAuthTable`（`AuthCheck.h:18`）在 `executeCreate` 内、**initcode 执行前**调用（`HostContext.h:950`）：

- 条件：`blockNumber != 0`。
- 作用：在合约地址建权限表，记录 `origin`/`sender` 为部署者。
- 表名 `tableName` 有 **FIB-82** 细节：`fix_auth_check && use_raw_address` 时用 hex 路径（`USER_APPS_PREFIX + hex(code_address)`），否则用账户 `path()`。
- **关键：`executeCreate` 服务所有层级**——顶层 CREATE 与 **EVM 内嵌套 CREATE/CREATE2** 都会建表。

### 21.2 纠偏：不是"顶层编排后置"，而是"CREATE 帧入口钩子"

原 §15.5 行4 写"编排（`executeViaHost` create 后置）"**不准确**：

- 顶层编排只能覆盖顶层 CREATE；**嵌套 CREATE 发生在自研 `EthHost` 递归内**，顶层编排够不到。
- 且必须在 **initcode 执行前**建表（顺序敏感）。

→ **正确去向：`createAuthTable` 与 Hook#8（CREATE nonce 语义）同一时机点——自研 `EthHost` 的 CREATE 帧入口钩子**，在 `account.create()` 之后、initcode 之前，对**每个** CREATE 帧（含嵌套）回调。

### 21.3 接口与顺序

定义 CREATE 帧入口回调（归入 `HostExtension` 或 `FiscoHostExtension`，由其持有 storage/precompiledManager/ledgerConfig 等编排上下文引用）：

```
EthHost::execute_create(msg):
    deriveMessage 已定 code_address（CR-A，Hook#2）
    account.create()
    ext->onCreateFrameEntry(msg)        # FISCO：createAuthTable(FIB-82 表名) + nonce(Hook#8)
    run initcode
    on SUCCESS: setCode(code, abi, SM3 codeHash)   # ABI/SM3 见 §15.5 行6 / §16
```

### 21.4 与 State journal / 回滚的一致性（D-A）

- `createAuthTable` 的写入**须进 `State` journal / 同一回滚域**，使 CREATE 帧 revert 时一并回滚（等价现状 `execute` 失败 rollback savepoint，`HostContext.h:668`）。
- 不可经独立的 `RollbackableStorage` 直写路径绕过 journal，否则失败的 CREATE 会残留权限表。
- → **依赖项**：`buildLegacyExecutive`/`creatAuthTable` 的写入需接到自研 `State` 的写通道（C3 实现时确认 `ExecutiveWrapper` 的 storage 后端即 `State` 视图）。

### 21.5 增量

- **§7 Hook#8** 扩展为"CREATE 帧入口：nonce 语义 **+ `createAuthTable`（FIB-82 表名）**"。
- **Auth-A（§5.3/§6/§14-Q11）** 职责从"仅 `checkAuth`"扩为"`checkAuth` + CREATE 建表"。
- **§15.5 行4 ⚠️ 解除**：去向 = Hook#8 同点（CREATE 帧入口钩子），非顶层编排后置。

---

## 22. 实现期 checklist（§15–§21 汇总，按阶段）

### C0 — 自研 eth/state + EthHost 骨架
- [ ] 自研 `eth/state`：`State`/`EthHost`/`StateView`/`StateDiff`/`Account`/`Transaction`/`BlockInfo`/`bloom_filter`/`errors`（参考 evmone，不拷 test 源码）（§18.1）
- [ ] `EthHost` 8 个扩展点（§7.1）+ `HostExtension` 接口（§5.3）
- [ ] 预编译分发器 `EthPrecompiles`，crypto 复用 `evmone::precompiles`（§18.2）
- [ ] portfile 增装 `evmone_precompiles/keccak.hpp`、`secp256k1.hpp`（§18.3）
- [ ] 确认仍用 fork 版 evmone/evmc（`evmc_host_context` 含 `hash_fn`/`isSMCrypto`）（§16）
- [ ] **不**引入 `test/utils/{mpt,rlp,...}`（§18.4）

### C1 — FISCO 状态视图 + 去 executor 依赖
- [ ] `FiscoStateView`（每次 `syncWait` 穿透 storage）+ `StateDiffApplier`
- [ ] `FiscoStateView` 服务 SM3 codeHash（§16.2-3）与 ABI 存储（§15.5 行6）
- [ ] `EthHost::set_storage` 复刻 `sstoreStatus` 4态/2态 + `DIRECT` 读（§20.2）
- [ ] T2：抽 `Eip7623Components`/`calcEip7623Components`/`Eip2930AccessList` 入 `bcos-evm` + `using` 别名回兼（§17.3）
- [ ] T2：删 `ModexpGas.cpp` 冗余 `#include "bcos-executor/src/Common.h"`（§17.1）
- [ ] 验收：`rg '#include "bcos-executor' bcos-evm/eth` → gas 相关文件清零；`HostContext`/`eip2929` 残留至 **C4**

### C2 — eth 向量门禁（不比 root，Q20）
- [ ] 不比 state/receipt root；比对 status/gas/logs/output
- [ ] 按需补 eth-向量-only 文件（`system_contracts`/`requests`/`ethash_difficulty`/`block.cpp`）（§18.1）
- [ ] Prague/Osaka 向量 vs geth 绿灯

### C3 — FISCO 生产路径回归（含国密）
- [ ] 错误处理 catch→`EVMCResult` 表 + `fix_error_handling` 门控（§20.1）
- [ ] revert logs：执行后**先转存** `ctx.logs` 再判 State 丢弃；顶层 `fix_revert_logs` 门控保持 `makeReceipt` 不变（§19）
- [ ] `createAuthTable` 走 CREATE 帧入口钩子（含嵌套，FIB-82 表名，进 journal）（§21）
- [ ] `checkAuth`（Auth-A）+ CREATE 建表职责合并
- [ ] 确认 Hook#2 对嵌套 CREATE/CREATE2 生效（§16.2-2）
- [ ] 国密链回归：CREATE 地址/codeHash/SHA3 opcode 走 SM3（§16）
- [ ] 与 `release-3.18.0` receipt/status/gas 一致（含 L-A 双 gas 路径）

### C4/C5
- [ ] 删 `HostContext`/`eip2929/*`/`BuiltinPrecompiles`；`eth/**` 零 `bcos-executor` include
- [ ] `TransactionExecutorImpl` 切换 + C3 回归复跑 + scheduler Prepare CI（非 DAG 写集）

> 说明：写集/DAG 可见性（State journal → 写锁）按决定**不在本 spec**（§15.2）。

---

## 23. Sub-agent 审查汇总（2026-06-18）

三路并行审查：**一致性**、**完整性/歧义**、**代码库事实核验**。本节记录结论与已 inline 修订项。

### 23.1 审查结论

| 维度 | 结果 | 要点 |
|------|------|------|
| 一致性 | ✅ 已修 | 原 BLOCKER：§15.5 与 §21 对 `createAuthTable` 矛盾 → 已统一为 Hook#8；Auth-A 扩为 checkAuth+建表；§5.3 增 `FiscoHostExtension` |
| 完整性 | ✅ 可进 writing-plans | 闭合 Q20（不比 root）；C1/C4 拆分 `bcos-executor` 验收时点；C5 降级为可测 scope |
| 代码事实 | ✅ 11/12 PASS | §3/§8 已标目标架构；§19 补充 `:670` 与 flag OFF 张力；ModexpGas 删 include 需留意 `DataConvertUtility.h` |

### 23.2 建议实施分解（非单一 mega-plan）

```text
Plan 1 (C0) → Plan 2 (C1) ∥ Plan 3 (C2) → Plan 4 (C3→C4→C5)
```

### 23.3 进入 writing-plans 前自检

- [x] 无 TBD/TODO 占位符
- [x] §6/§7/§11/§14/§15.5 三条主线一致（自研、8 Hook、不引入 test/state）
- [x] 开放决策：Q20 root、C5 scope、Auth-A、C1 include 时点
- [ ] 用户确认本 spec 后可 invoke **writing-plans**（按 Plan 1 起笔）

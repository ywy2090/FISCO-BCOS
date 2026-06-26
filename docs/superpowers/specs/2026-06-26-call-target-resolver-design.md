# CallTargetResolver — Precompile 分类与 Warm 单源 — 设计规格

**日期：** 2026-06-26  
**版本：** v1.1（subagent 审查修订）  
**状态：** 已批准  
**ADR：** [ADR-024](../../bcos-evm/docs/adr/024-call-target-resolver-deepening.md)（Accepted，v1.1 同步）  
**迁移策略：** **六 PR 渐进**（PR1–PR6）；PR4 为行为切换点  
**Grilling 决策：** D1=甲 · D2=2b+ · D3=3b · D4=4a · D5=5b

**前置：**

- [2026-06-23-precompile-router-design.md](./2026-06-23-precompile-router-design.md)（Phase 1 Done）
- [2026-06-23-precompile-port-design.md](./2026-06-23-precompile-port-design.md)（ADR-017 Done）
- [2026-06-24-execution-frame-design.md](./2026-06-24-execution-frame-design.md)（ExecutionFrame Done；**本 spec PR6 修订 §2.2 注入面**）
- `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md`（**PR6 更新 §3 chain precompile 行**）
- `bcos-evm/docs/adr/018-revision-gating-single-source.md`

**v1.1 修订摘要：**

- §4.3：`classifyTarget` 统一含 `FrameScope`；FISCO `executionAddress` 规范；adapter 组合模型
- §4.4 / ADR：对齐 `PrecompileEnvelopeInput`（无 `policy`，仅 `skipValueTransfer`）
- §4.5：补 `EmptyAccount` 行为表
- §5.1：PR4 delta + RR6 + RR7 + CREATE 不变声明
- §5.4：Injection wiring matrix（新增）
- §4.6 / §5.2：warm 步骤表述修正
- §6 PR1/PR3/PR5：Port 过渡、FISCO adapter 结构、PR5 合并门槛
- §7：测试迁移拆分策略
- §8.2：补 orchestrator / EthHost 路径
- §9.3：PR4 / PR6 分级 compliance checklist

---

## 1. 背景与动机

### 1.1 问题

一次 CALL 命中 precompile 或链扩展目标时，开发者需在四个 module 间跳转：

| 步骤 | Module | 职责 |
| --- | --- | --- |
| 1 | `FrameTargetResolver` | 7702 / CREATE 地址归一化、`executionAddress` |
| 2 | `PrecompileRouter::dispatchPrecompile` | **分类** + envelope |
| 3 | `VmHostPolicy::tryChainPrecompile` | 链扩展 hook |
| 4 | `ChainPrecompilePort`（FISCO）或 `OpStackVmHostPolicy` 内联 | 链 dispatch |

**摩擦：**

1. **分类无 locality** — 分类与地址工作分裂在两文件。
2. **Warm 与 dispatch 分裂** — tx-entry 仅枚举 builtin precompile 地址（`forEachActivePrecompile`）；链目标可 dispatch 但不在同一 warm 枚举源。
3. **链 seam 不一致** — FISCO Port vs OpStack 内联。
4. **测试沿旧 seam** — 无单一 interface 测 kind + warmPolicy。

### 1.2 与既有 spec 的关系

| 既有决策 | 本 spec |
| --- | --- |
| PrecompileRouter Phase 1 | 保留 envelope；迁出分类 |
| ADR-017 Port | 扩展为 `ChainCallTargetPort`；dispatch 实现仍驻 TE |
| ExecutionFrame | PR4 仅替换 `tryPrecompileDispatch` 块 |
| `PrecompileActive.h` | builtin 单源；resolver 调用不复制 |

---

## 2. 目标与非目标

### 2.1 目标

1. `CallTargetResolver` deep module（A+B）。
2. `CallTargetDescriptor` + `WarmPolicy`；`enumerateTxEntryWarmTargets`（2b+）。
3. `ChainCallTargetPort` + 两链 adapter（3b）。
4. `executePrecompileEnvelope` 信任 descriptor（4a）。
5. 三 test module + characterization（5b）。
6. 六 PR；PR2 可选 dual-run；PR4 行为切换。

### 2.2 非目标

- 不改 EVM 语义、OpStack fee、FISCO auth。
- 不改 `EthPrecompiles` gas 数学。
- 不 warm FISCO 全 `>= 0x1000` 范围。
- 不在此 spec 完成 AuthPort 全生命周期。
- 不重构 CREATE 九步 pipeline（RR7 冻结）。

---

## 3. 架构决策（Grilling 记录）

| ID | 结论 |
| --- | --- |
| D1 | **甲** — A+B 合并；C 留 envelope |
| D2 | **2b+** — taxonomy + `WarmPolicy` |
| D3 | **3b** — 中立 `ChainCallTargetPort` |
| D4 | **4a** — envelope 信任 descriptor |
| D5 | **5b** — 按 interface 重建测试 |

---

## 4. 模块与 Interface

### 4.1 文件布局

```text
bcos-evm/eth/execution/
  CallTargetResolver.h / .cpp
  FrameTargetResolver.h / .cpp    # PR6 删除或 thin delegate

bcos-evm/eth/ports/
  ChainCallTargetPort.h

bcos-evm/eth/precompiled/
  PrecompileRouter.h / .cpp       # → executePrecompileEnvelope

bcos-evm/opstack/
  OpStackChainCallTargetAdapter.h / .cpp

transaction-executor/bcos-transaction-executor/adapters/
  FiscoChainCallTargetAdapter.h / .cpp
  ExecutorPrecompileAdapter.h / .cpp   # dispatch 后端；由 Fisco adapter 组合
```

**Seam 纪律：** `CallTargetResolver.*`、`ChainCallTargetPort.h` 不得 `#include` `bcos/` 或 `opstack/`。

### 4.2 核心类型

```cpp
namespace bcos::evm::execution {

enum class CallTargetKind { EvmContract, BuiltinPrecompile, ChainPrecompile,
                            EmptyAccount, PolicyRejected };

enum class WarmPolicy { Never, TxEntryAlways, TxEntryIfStatic, FrameEntryOnly };

struct CallTargetDescriptor {
    CallTargetKind kind;
    evmc_address   dispatchAddress;
    WarmPolicy     warmPolicy;
    evmc_message   routed;
};

CallTargetDescriptor resolveCallTarget(
    state::State&,
    bcos::evm_standard::RevisionConfig const&,
    evmc_message msg,
    FrameScope scope,
    ChainCallTargetPort* chainPort,
    state::VmHostPolicy* extension);

void enumerateTxEntryWarmTargets(
    bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetPort const* chainPort,
    std::invocable<evmc_address const&> auto&& consume);

}  // namespace bcos::evm::execution
```

术语：`extension` 与现网 `FrameContext::extension` / `PrecompileRouterInput::extension` 同指 `VmHostPolicy*`。

### 4.3 ChainCallTargetPort

```cpp
namespace bcos::evm {

struct ChainCallTargetPort {
    virtual ~ChainCallTargetPort() = default;

    /// C7：scope 必传 — 仅 emptyCode || scope==Nested 时 resolver 调用本方法。
    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(
        state::State&,
        evmc_address const& executionAddress,
        evmc_message const& msg,
        execution::FrameScope scope) = 0;

    virtual std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) = 0;

    virtual void forEachStaticWarmTarget(
        std::invocable<evmc_address const&> auto&& consume) const = 0;
};

}  // namespace bcos::evm
```

**FISCO `classifyTarget` 规范：**

- 主键：`executionAddress`（`resolveFrameTarget` 产出）。
- `[PRECOMPILED],0x…` 代理：`state.get_code(executionAddress)` → 解析 `dispatchAddress`。
- 禁止仅用 `msg.recipient` / `msg.code_address` 绕过 `executionAddress`。

**Adapter 组合（C8 闭合）：**

```text
FiscoChainCallTargetAdapter : ChainCallTargetPort
  ├─ classifyTarget()      # 自 FiscoVmHostPolicy::tryChainPrecompile 迁入
  ├─ forEachStaticWarmTarget()  # 默认 no-op
  └─ dispatch()            # 委托 ExecutorPrecompileAdapter（或内联同等逻辑）

OpStackChainCallTargetAdapter : ChainCallTargetPort
  ├─ 构造：State*, u256 l2BaseFee, OpStackForkSchedule, uint64_t blockTimestamp
  ├─ classifyTarget / dispatch / forEachStaticWarmTarget(L1 + GasOracle)
```

**ADR-017 `ChainPrecompilePort` 过渡（PR1）：**

- 新增 `eth/ports/ChainCallTargetPort.h`。
- `bcos/ports/ChainPrecompilePort.h` 保留为 `[[deprecated]]` alias（继承或 typedef）至 PR6。
- `ExecutorPrecompileAdapter` PR1–PR2 仍 include 旧头；PR3 切 canonical path。

### 4.4 Precompile Envelope（4a）

```cpp
struct PrecompileEnvelopeInput {
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    execution::CallTargetDescriptor const& target;
    evmc_message const& message;
    bool skipValueTransfer;   // caller: extension->skipHostValueTransfer()
    ChainCallTargetPort* chainPort;
};
```

**无 `VmHostPolicy*` in envelope** — DELEGATECALL 在 `resolveCallTarget`；`skipValueTransfer` 由 caller 镜像。

Envelope 禁止：`isActivePrecompile`、`tryChainPrecompile`、`classifyTarget`。

### 4.5 resolveCallTarget 分类规则

| 条件 | `CallTargetKind` | `WarmPolicy` |
| --- | --- | --- |
| CREATE / CREATE2 | caller 不进入本 switch | — |
| 7702 delegation designator | `EvmContract` | `Never` |
| DELEGATECALL + deny + empty active builtin | `PolicyRejected` | `Never` |
| `classifyTarget` 命中（`emptyCode \|\| nested`） | `ChainPrecompile` | adapter 设置 |
| empty + `isActivePrecompile` | `BuiltinPrecompile` | `TxEntryAlways` |
| empty + 非上两者 | `EmptyAccount` | `Never` |
| non-empty + 非 7702 delegate | `EvmContract` | `Never` |

**EmptyAccount 行为（与现网 parity）：**

| 项 | 行为 |
| --- | --- |
| Journal | checkpoint → success → commit |
| `gas_left` | `message.gas` |
| `precompileHit` | `true` |
| Top-level | `finalizePrecompileHit`（无 sender nonce bump） |
| Nested | 同现 precompile hit finalize |

**Precedence：** chain → builtin。 **C7：** `emptyCode \|\| scope == Nested` 才调 `classifyTarget`。

**`FrameEntryOnly`：** CREATE warm-pin 在 `resolveFrameTarget` 设置 descriptor.warmPolicy；`enumerateTxEntryWarmTargets` 不消费。

### 4.6 Warm 策略表（2b+）

| Target 类 | `WarmPolicy` | enumerate 行为 |
| --- | --- | --- |
| Builtin | `TxEntryAlways` | `forEachActivePrecompile` |
| OpStack L1 / GasOracle | `TxEntryIfStatic` | `forEachStaticWarmTarget` |
| FISCO 直连小地址 | 默认不 tx-entry warm | adapter no-op |
| FISCO 代理 | 代理可走 `warmDestination` | 解析目标不 enumerate |
| 普通合约 | `Never` | — |

PR5 `warmTransactionEntry` 变更：

```cpp
// 替换原 forEachActivePrecompile 块；其余子步骤语义不变
enumerateTxEntryWarmTargets(cfg, chainPort, [&](evmc_address const& a) {
    state.warm_up_address_no_journal(a);
});
// 仍执行：sender → destination → coinbase → createCodeAddress → access list
```

仅 **precompile/chain static 地址的来源** 改变；各子步骤相对语义不变。

---

## 5. 数据流

### 5.1 帧级 PR4 delta（CALL / STATICCALL / DELEGATECALL）

**不改动：** CREATE / CREATE2 九步 pipeline（RR7 TopLevel vs Nested 顺序冻结）。

```text
runExecutionFrame — 仅替换原 tryPrecompileDispatch 等价块：

① frameTarget = resolveFrameTarget(state, revision, msg, scope)
② skipVt = extension && extension->skipHostValueTransfer()
③ descriptor = resolveCallTarget(state, revision, frameTarget.routed, scope,
                                 chainPort, extension)
   ※ RR6：Nested 必须在 prepareNestedMessage 之前执行 ③
④ switch (descriptor.kind)
     BuiltinPrecompile | ChainPrecompile:
       return executePrecompileEnvelope({descriptor, message, skipVt, chainPort, ...})
     EmptyAccount:
       return emptyAccountFrameResult(...)   // §4.5 表
     PolicyRejected:
       return FrameResult{PRECOMPILE_FAILURE, precompileHit=false}
     EvmContract:
       break → ⑤
⑤ prepareNestedMessage (Nested only)
⑥ 现有 transfer / checkpoint / runVm / finalizeFrame
```

### 5.2 Tx-entry warm（PR5）

见 §4.6。Eth ref（`chainPort == nullptr`）enumerate 结果与今天 `forEachActivePrecompile` 等价。

### 5.3 注入点摘要

`FrameContext` + `ExecuteMessageInput` 新增 `ChainCallTargetPort* chainPort{nullptr}`（与 `extension` 并列）。

### 5.4 Injection wiring matrix

| 入口 | `chainPort` 构造 | 生命周期 | 传入 `FrameContext` |
| --- | --- | --- | --- |
| Eth `TxExecutionAdapter` | `nullptr` | — | `ExecuteMessageInput.chainPort` |
| FISCO `TransactionExecutorImpl` | `FiscoChainCallTargetAdapter` 栈对象 | per `executeViaHost` | `FiscoExecutionBridge` → `executeMessage` |
| OpStack `OpStackTxLifecycle` | `OpStackChainCallTargetAdapter(state, baseFee, fork, ts)` 栈对象 | per lifecycle | `ctx` 新字段 → pipeline → `executeMessage` |
| Nested `EthHost::call` | **与 top-level 相同指针** | borrow | `EthHost.cpp` 构造 `FrameContext` 时填入 |

**PR3–PR4 必改文件：**

- `bcos-evm/eth/execution/ExecutionFrame.h` — `FrameContext::chainPort`
- `bcos-evm/eth/ExecuteMessage.h` — `ExecuteMessageInput::chainPort`
- `bcos-evm/eth/state/EthHost.cpp` — nested 传递 `chainPort`
- `bcos-evm/bcos/FiscoExecutionBridge.cpp` — FISCO 注入
- `bcos-evm/opstack/OpStackTxLifecycle.cpp` — OpStack 注入

---

## 6. 迁移计划（六 PR）

| PR | 范围 | 行为变更 |
| --- | --- | --- |
| **PR1** | `CallTargetDescriptor`、`eth/ports/ChainCallTargetPort.h`；`bcos/ports/ChainPrecompilePort` deprecated alias | 无 |
| **PR2** | `CallTargetResolver` + `CallTargetResolverTest`；**建议** dual-run R1–R8 | 无 |
| **PR3** | `FiscoChainCallTargetAdapter`（含 classify + dispatch 组合）、`OpStackChainCallTargetAdapter`；`chainPort` 字段 wired，主路径未切 | 无 |
| **PR4** | `ExecutionFrame` delta §5.1；`executePrecompileEnvelope`；`CallTargetCharacterizationTest` | **是** |
| **PR5** | `warmTransactionEntry` → enumerate；OpStack static warm | **仅当** geth 引用或 C2 gas baseline 已记录 |
| **PR6** | 删 shim / 旧测试 / `ChainPrecompilePort.h`；更新 ADR-005、ADR-017、Execution Frame Design、`architecture-overview.md` | 清理 |

**PR4 回归门：**

```bash
ctest -R 'CallTargetCharacterization|CallTargetResolver|PrecompileEnvelope|ExecutionFrame|FrameTarget'
```

---

## 7. 测试（5b）

### 7.1 新 test module

| 文件 | Interface | 迁移策略 |
| --- | --- | --- |
| `test/eth/CallTargetResolverTest.cpp` | resolver + enumerate | **拆分**迁入：precedence、7702 分类、gate matrix、FISCO 动态解析；`PrecompileRouter7702Test` 中 E2E 部分留 characterization |
| `test/eth/PrecompileEnvelopeTest.cpp` | envelope | **新建** descriptor 直驱用例 + 少量 E2E smoke；不等同复制 `PrecompileRouterEnvelopeTest` 全文 |
| `test/cross/CallTargetCharacterizationTest.cpp` | `runExecutionFrame` | 替代 `PrecompileRouterCharacterizationTest`；**必须**含 C7 |

### 7.2 保留

- `EthPrecompiles`、L1Block/GasOracle 单元测试
- `PrecompileActiveGateMatrixTest` 至 PR6（逻辑迁入后可删）

### 7.3 TE

- `FiscoChainCallTargetAdapter` 全 port smoke（一条）
- bcos-evm：`InMemoryChainCallTargetAdapter`；零 executor include

### 7.4 矩阵 R1–R8 / W1–W2

（同 v1.0 §7.4；R7 覆盖 TopLevel + Nested proxy；C7 在 characterization 不在 resolver 单测）

---

## 8. 文件清单

### 8.1 新增

（同 v1.0 §8.1）

### 8.2 修改

| 路径 | PR | 要点 |
| --- | --- | --- |
| `bcos-evm/eth/execution/ExecutionFrame.h/.cpp` | 4 | PR4 delta；`FrameContext::chainPort` |
| `bcos-evm/eth/ExecuteMessage.h` | 4 | `chainPort` 字段 |
| `bcos-evm/eth/state/EthHost.cpp` | 4 | nested `chainPort` |
| `bcos-evm/eth/precompiled/PrecompileRouter.cpp` | 4 | envelope only |
| `bcos-evm/eth/execution/WarmTransactionEntry.h` | 5 | enumerate |
| `bcos-evm/bcos/FiscoExecutionBridge.cpp` | 3–4 | FISCO inject |
| `bcos-evm/bcos/FiscoVmHostPolicy.cpp` | 3–4 | 删 tryChainPrecompile 主体 |
| `bcos-evm/opstack/OpStackTxLifecycle.cpp` | 3–4 | OpStack inject |
| `bcos-evm/opstack/OpStackVmHostPolicy.h` | 3–4 | 删内联 dispatch |
| `transaction-executor/.../FiscoChainCallTargetAdapter.*` | 3 | 全 port |
| `transaction-executor/.../ExecutorPrecompileAdapter.*` | 3 | dispatch 后端 |
| `bcos-evm/docs/architecture-overview.md` | 6 | ADR 001–024 |
| `docs/superpowers/specs/2026-06-24-execution-frame-design.md` | 6 | §2.2 chainPort |
| `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md` | 6 | §3 表 |

### 8.3 删除（PR6）

（同 v1.0 §8.3）

---

## 9. 验收

### 9.1 编译边界

（同 v1.0 §9.1）

### 9.2 行为等价

**PR4 门：**

- C1–C7 + **C7 depth 不对称** 与 characterization 一致
- RR6 / RR7 / EmptyAccount §4.5
- R4 precedence、R7 FISCO proxy
- Eth `chainPort=nullptr` 等价今天

**PR5 门（额外）：**

- OpStack predeploy warm：**必须**有 op-geth 文档引用或 C2 gas baseline 对比后方可合并；否则 PR5 仅 plumbing + flag off

### 9.3 Compliance checklist

**PR4 gate：** ADR-024 § Compliance「PR4 gate」全部勾选。

**PR6 gate：** ADR-024 § Compliance「PR6 gate」全部勾选。

---

## 10. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| PR4 切换 | 六 PR；PR2 dual-run R1–R8；§5.1 冻结 |
| RR6 顺序 | §5.1 显式；characterization + FISCO nested CREATE 测 |
| EmptyAccount finalize | §4.5 表 + C3 baseline |
| `chainPort` 未传到 nested | §5.4 matrix；EthHost PR4 checklist |
| OpStack warm | PR5 独立；geth/C2 门槛 §9.2 |
| FISCO classify/dispatch 分裂 | 组合 adapter §4.3；R7 + TE smoke |
| Execution Frame Design 冲突 | PR6 修订 §2.2 |
| Port 头文件过渡 | PR1 alias §4.3 |

---

## 11. Spec 自检

| 检查 | 结果 |
| --- | --- |
| TBD / TODO | 无 |
| ADR-024 v1.1 对齐 | ✅ |
| classifyTarget 含 scope | ✅ |
| PrecompileEnvelopeInput 一致 | ✅ |
| RR6 / RR7 / EmptyAccount / C7 | ✅ |
| Injection matrix | ✅ §5.4 |
| PR5 合并门槛 | ✅ |
| PR4 vs PR6 checklist 分级 | ✅ |
| 可六 PR 实施 | ✅ |

---

## 12. 变更记录

| 日期 | 变更 |
| --- | --- |
| 2026-06-26 | v1.0：grilling 初版 |
| 2026-06-26 | v1.1：subagent 审查修订（C1–C8、M1–M8） |

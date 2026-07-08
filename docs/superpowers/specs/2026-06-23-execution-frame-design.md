# ExecutionFrame — 内核帧信封单点化（候选 1 + 3）设计规格

**日期:** 2026-06-23
**分支:** feat-evm-refactor
**状态:** 待评审
**词汇:** codebase-design（module / interface / implementation / depth / seam / adapter / leverage / locality）
**验收 north star:** geth / GST·EEST `stateRoot` · `gasUsed` · `logsHash` 等价（ETH 参考 profile）
**关联:** ADR-005（orchestration 边界）· ADR-015（included-tx vmerr）· ADR-017（fisco-precompile-port）· ADR-019（orchestration pipeline）· [error-handling-parity-design](2026-06-23-eth-evm-error-handling-parity-design.md) · [precompile-router-design](2026-06-23-precompile-router-design.md) · 拟新增 **ADR-020（ExecutionFrame 信封契约）**

---

## 1. 问题陈述

编排层已收敛到 `runOrchestration`（ADR-019），但**内核「EVM 帧」语义仍有两条实现路径**，且 `PrecompileRouter` 自带一套与字节码路径相反的 state 信封。

### 1.1 三处分裂的信封顺序（源码事实）

| 路径 | checkpoint vs transfer | precompile dispatch | CREATE bind |
| --- | --- | --- | --- |
| `executeMessage`（depth=0，字节码/CREATE） | checkpoint → transfer（`executeMessage.cpp` L221–222） | 顶层空 code 直接 `dispatchPrecompile` 后 return，**无外层 checkpoint**（L205–217） | checkpoint **之后**（L233） |
| `EthHost::call`（depth>0） | checkpoint → transfer（`EthHost.cpp` L299–306） | 探测命中直接 return（L274–285） | checkpoint **之前**（L295） |
| `PrecompileRouter::dispatchPrecompile` | **transfer → checkpoint**（`PrecompileRouter.cpp` L45–57） | 自带 `finalizeEnvelope` commit/revert | — |

### 1.2 缺陷与 friction

1. **P0 state 正确性（候选 3）：** `PrecompileRouter` 先 `transfer` 后 `checkpoint`；付费 precompile 失败时 `finalizeEnvelope` 的 `revert` 无法回滚已发生的转账 → 直接污染 `stateRoot`。geth `evm.Call` 为 `Snapshot → Transfer → Run → 失败 RevertToSnapshot`。（error-handling spec §4.1）
2. **无 locality（候选 1）：** 同一帧语义在 `executeMessage` 与 `EthHost::call` 各写一遍；`PrecompileRouterEnvelopeTest` / `PrecompileRouterEquivalenceTest` 用 `runDepth0` / `runDepth1` 双 harness 手工维护 depth0/depth1 等价——团队已知两路径需对齐，但靠测试而非结构保证。
3. **CREATE bind 顺序两路不一致：** depth=0 在 checkpoint 后 bind，depth>0 在 checkpoint 前 bind。无金标准锁定，是下一类 stateRoot drift 的温床。
4. **gas_left 规则散落：** `INSUFFICIENT_BALANCE` 在三处硬编码 `gas_left=0`，与 geth/EVMC「保留 gas」冲突（error-handling spec §4.2）。

### 1.3 deletion test

删除 `executeMessage` 与 `EthHost::call` 中重复的 checkpoint / transfer / precompile / CREATE 分支，复杂度集中到单一 `ExecutionFrame` —— **通过**（complexity concentrates）。删除 `PrecompileRouter` 的 `finalizeEnvelope`，commit/revert 上移 Frame —— **通过**（Router 变纯 dispatch）。

---

## 2. 目标与非目标

### 2.1 目标

1. 在 `eth/execution/` 引入深 module **`ExecutionFrame`**：拥有帧 lifecycle 信封（checkpoint → transfer → run → finalize），`executeMessage` 与 `EthHost::call` 在 route/warm 之后共用同一 implementation。
2. **修复候选 3：** 信封顺序对齐 geth（checkpoint 先于 transfer），付费 precompile 失败回滚转账。
3. **`PrecompileRouter` 瘦身为纯 dispatch**：移除 transfer / checkpoint / commit / revert / empty-account 副作用，只判定「是否 precompile + 调哪个」。
4. **CREATE bind 顺序统一**：以 geth `evm.Create` 为金标准锁定单一顺序，两路径一致。
5. **gas_left 纳入 Frame 契约**：`FrameOutcome` 出口即按 geth/EVMC 规则填好（含 `INSUFFICIENT_BALANCE` 保留 gas）。
6. 验收：ETH 参考 profile 下 GST/EEST `stateRoot`/`gasUsed`/`logsHash` 与 geth 一致；depth0/depth1 等价作为推论性回归。

### 2.2 非目标

- 不改 orchestration 层（`runOrchestration` / 三链 hook 装配，ADR-019 已交付）。
- 不实现 `EthTxOutcome` 分类与 included-tx normalize —— 属 orchestration `postAdopt`（error-handling spec / ADR-017 错误分类，**独立 spec**）。Frame 仅产出结构化 `FrameOutcome` 供其消费。
- 不改 `warmTransactionEntry`、EIP-7702 `applyAuthorizations`、`finalize_self_destructs`、`fixNonceInit` 等 **tx-level / top-level 专属**逻辑（留 `executeMessage` 外壳）。
- 不引入跨链 policy 抽象；FISCO/OP deviation 仍走 `HostExtension` hook（ADR-005）。
- 不审 evmone opcode 内部。

### 2.3 已确认决策（grilling）

| # | 议题 | 结论 |
| --- | --- | --- |
| D1 | 验收标准 | **A：** geth/GST stateRoot 为 north star；depth0/1 等价为推论 |
| D2 | 实现形态 | ExecutionFrame 深 module；executeMessage / EthHost::call 变薄 wrapper |
| D3 | Router 边界 | 纯 dispatch，无 state 信封；commit/revert/empty-account 上移 Frame |
| D4 | depthPolicy | **不引入** —— CREATE 顺序锁单一 geth 值，差异不进 interface |
| D5 | ValueTransferPolicy | **不三路** —— 仅 `CallTransfer` 与 `CreateEndowment` 两态；CREATE endowment 内联 init 相位 |
| D6 | gas_left | 入 `FrameOutcome` 契约（Frame 内部 gas 规则），不延后到 error spec |
| D7 | prepareMessage 相位 | precompile 路径**跳过** `prepareMessage`；仅字节码/CREATE 路径调用（保留现状语义） |
| D8 | 与 error spec 关系 | 接口相邻、spec 分离；本 spec 引用 §4.1/§4.2 为验收条目，不合并 |
| D9 | CREATE bind 锁定 | 先 characterization 锁 geth 顺序，再统一两路径 |

---

## 3. 架构

```text
  runOrchestration ─► executeMessage ─┐  warm / 7702 auth / resolve code（tx-level，外壳）
                                       │
  evmone callback ─► EthHost::call ───┤  routeCall / delegatecall guard（外壳）
                                       │
                                       ▼
                          runExecutionFrame(FrameRequest) ──► FrameOutcome
                          ┌────────────────────────────────┐
                          │  ExecutionFrame (deep, eth/)    │
                          │  ① guard  ② prepare(仅字节码)    │
                          │  ③ checkpoint                   │
                          │  ④ value transfer (policy)      │
                          │  ⑤ CREATE init (if CREATE)      │
                          │  ⑥ run: dispatch | vm.execute   │
                          │  ⑦ finalize: deposit/install/   │
                          │     commit|revert/gas_left      │
                          └────────────────────────────────┘
                                       │
                          PrecompileRouter (thin) ◄── ⑥ 调用，无副作用
                                       │
                          (下游) EthTxOutcome 分类 ── 独立 spec
```

**seam 纪律：**
- ExecutionFrame 在 `eth/` kernel 内，不 include `bcos/` / `opstack/`（ADR-005 Rule 1）。
- `HostExtension` hook 相位固定在 Frame 内（`prepareMessage` 在 checkpoint 前、字节码路径；`tryChainPrecompile` 在 ⑥ run 内）。
- Frame **不碰** warm set / 7702 auth / self-destruct finalize —— 这些是 tx-level，留外壳。

---

## 4. 组件

| module | interface | implementation | 变更 |
| --- | --- | --- | --- |
| **ExecutionFrame**（新，`eth/execution/ExecutionFrame.{h,cpp}`） | `FrameOutcome runExecutionFrame(FrameRequest&)` | §5 固定信封；precompile 与字节码共用 ③–⑦ | 新增 |
| **PrecompileRouter**（改） | `DispatchResult tryDispatchPrecompile(state, revision, extension, message, target)` | 仅 chain→kernel 路由 + EthPrecompiles 调用；**删** transfer/checkpoint/finalizeEnvelope/empty-account commit | 瘦身 |
| **executeMessage**（改） | 签名不变 | tx-level warm/7702/resolve code → 构造 `FrameRequest` → `runExecutionFrame` → 映射 `ExecuteMessageOutput` | 变薄 |
| **EthHost::call**（改） | 签名不变 | routeCall + delegatecall guard → 构造 `FrameRequest` → `runExecutionFrame` | 变薄 |
| **CreateExecution**（复用） | 不变 | `bindCreateMessageForInit` / `initializeCreateTargetAccount` / `applyCreateCodeDepositGas` / `installCreatedContractCode` —— Frame 在固定相位调用 | 不变 |

### 4.1 FrameRequest

```cpp
namespace bcos::evm {

enum class FrameValueTransfer {
    None,            // value==0 或 skipHostValueTransfer
    CallTransfer,    // sender → 归一后 target（CALL/CALLCODE）
    CreateEndowment  // sender → 新合约 recipient（CREATE/CREATE2，⑤ 内联）
};

struct FrameRequest {
    state::State& state;
    state::EthHost& host;              // logs / nonce / execution address owner
    evmc::VM& vm;
    bcos::evm_standard::RevisionConfig revisionConfig;
    state::HostExtension* extension{nullptr};
    evmc_message message;             // route 归一后
    bcos::bytes code;                 // 空 => precompile 探测
    bool isPrecompileTarget{false};   // route 阶段已判定
    FrameValueTransfer valueTransfer{FrameValueTransfer::None};
    bool isCreate{false};
    bool fixStorageStatus{true};
    bool fixNonceInit{false};         // top-level CREATE 才置位（外壳传入）
};

}  // namespace bcos::evm
```

### 4.2 FrameOutcome

```cpp
enum class FrameCommitment { Committed, Reverted, NoCheckpoint };

struct FrameOutcome {
    evmc::Result result;        // gas_left 已按 §5.3 geth 规则填好
    int64_t gasRefund{0};
    FrameCommitment commitment{FrameCommitment::NoCheckpoint};
};
```

- `result.status_code` 为原始 evmc status（REVERT/OOG/SUCCESS/INSUFFICIENT_BALANCE…）；**不做** included-tx normalize。
- `commitment` 让外壳/编排区分「已提交 vs 已回滚 vs 未建 checkpoint」，供 settlement 与 EthTxOutcome 使用。

---

## 5. 数据流

### 5.1 外壳职责（不进 Frame）

| 逻辑 | 位置 | 理由 |
| --- | --- | --- |
| `warmTransactionEntry` | `executeMessage` 前置 | tx-level |
| EIP-7702 `applyAuthorizations` + sender nonce bump + warm delegation | `executeMessage` 前置 | tx-level，首帧前 |
| `resolveCodeAddress` / `resolveExecutableCode` | `executeMessage` 前置 | 决定 code → 填 `FrameRequest.code` |
| `routeCall`（CREATE 地址归一、7702 target、precompile 探测） | `EthHost::call` 前置 | 帧入口归一 |
| delegatecall-to-precompile guard | `EthHost::call` 前置（depth>0） | 现状 L268–272 |
| `finalize_self_destructs` | `executeMessage` 收尾（top-level success） | tx-level |
| logs 映射 / stateDiff 映射 | 外壳 | 输出契约（ADR-019 §5.3） |

### 5.2 固定信封（Frame 内，7 步）

```text
runExecutionFrame(req):
  ① guard        若 precompile 路径：跳到 ⑥a（不 prepare、不 bind）
  ② prepare      字节码/CREATE 路径：extension->setCallerAddress + prepareMessage
                 CREATE：bindCreateMessageForInit
  ③ checkpoint   state.checkpoint()                         ← 信封起点（修复 C3）
  ④ transfer     switch(req.valueTransfer):
                   CallTransfer:   canTransfer 失败 → revert + INSUFFICIENT_BALANCE(gas 保留)
                                   成功 → transfer(sender → target)
                   CreateEndowment: 见 ⑤
                   None: skip
  ⑤ create init  若 isCreate：
                   endowment!=0：canTransfer 失败 → revert + INSUFFICIENT_BALANCE(gas 保留)
                                 成功 → transfer(sender → recipient)
                   initializeCreateTargetAccount
  ⑥ run
    ⑥a precompile: out = tryDispatchPrecompile(...)         // 无副作用
                   命中 → result = out；未命中且 emptyCode → EMPTY_ACCOUNT_SUCCESS
    ⑥b bytecode:  result = vm.execute(host, revision, message, code)
  ⑦ finalize
                 CREATE success：applyCreateCodeDepositGas → installCreatedContractCode
                                 markCreatedInTx + create_address 修补 + (fixNonceInit)
                 success → gasRefund = state.get_refund(); commit(); commitment=Committed
                 failure → revert(); commitment=Reverted
                 CREATE：sender nonce bump + extension->bumpContractCreateNonce（非 origin）
  return FrameOutcome{result, gasRefund, commitment}
```

> 注：①precompile 路径无 ③–⑤ 的 prepare/bind，但**仍走 ③ checkpoint + ④ transfer + ⑦ commit/revert**——这是修复候选 3 的关键：付费 precompile 失败时 ④ 的 transfer 被 ⑦ revert 回滚。

### 5.3 gas_left 规则（geth 对齐，error-handling spec §4.2）

| 场景 | status | gas_left |
| --- | --- | --- |
| `CallTransfer` / `CreateEndowment` 余额不足 | `EVMC_INSUFFICIENT_BALANCE` | **`message.gas`（保留）** |
| precompile OOG / failure | 对应 status | 由 precompile 返回 |
| 字节码 REVERT | `EVMC_REVERT` | evmone 返回（保留） |
| 字节码非 REVERT vmerr | 对应 status | evmone 返回（通常 0） |
| empty account | `EVMC_SUCCESS` | `message.gas` |

> 现状三处 `makeInsufficientBalanceResult` 硬编码 `gas_left=0`，本 spec 改为保留 `message.gas`。**注意：** 顶层 CALL transfer 失败的 consensus-reject 语义由 orchestration 决定（error spec §3.2），Frame 仅产 evmc 结果。

### 5.4 CREATE bind 顺序统一（D9）

现状 depth=0 在 checkpoint 后 bind、depth>0 在 checkpoint 前 bind。**统一为 geth `evm.Create` 顺序**（待 characterization 锁定具体值后写死）：

```text
候选顺序（待 characterization 确认）：
  ② bindCreateMessageForInit（checkpoint 前，与 EthHost::call 现状一致）
  ③ checkpoint
  ⑤ endowment transfer + initializeCreateTargetAccount
```

锁定流程：先加 characterization 测试记录两路径**当前** CREATE stateRoot/gas，再切到统一顺序，用 GST CREATE fixture 验证与 geth 一致。任一路径若行为变化，记入 capability-matrix 并在 ADR-020 说明。

---

## 6. PrecompileRouter 瘦身

### 6.1 新 interface

```cpp
namespace bcos::evm::precompiled {

enum class PrecompileDispatchOutcome { NotApplicable, Dispatched, EmptyAccountSuccess };

struct DispatchResult {
    PrecompileDispatchOutcome outcome{PrecompileDispatchOutcome::NotApplicable};
    evmc::Result result{};
    int64_t gasRefund{0};
};

// 纯 dispatch：无 transfer / checkpoint / commit / revert
DispatchResult tryDispatchPrecompile(state::State& state,
    bcos::evm_standard::RevisionConfig const& revision,
    state::HostExtension* extension, evmc_message const& message, evmc_address target);

}  // namespace bcos::evm::precompiled
```

### 6.2 删除项

- `makeInsufficientBalanceResult` 内的 value transfer 前检查 → 移至 Frame ④
- `state.checkpoint()` / `finalizeEnvelope`（commit/revert）→ 移至 Frame ⑦
- `EmptyAccountSuccess` 的 `finalizeEnvelope` commit → Frame ⑦ 统一处理
- `transfer(...)` → Frame ④

Router 仅保留：chain precompile（`extension->tryChainPrecompile`）→ kernel precompile（`EthPrecompiles::tryDispatchInCall`）→ empty-account 判定，返回 `DispatchResult`，**不触碰 state 信封**。

---

## 7. 测试策略

### 7.1 先增后改（防回归）

1. **付费 precompile 失败回滚（候选 3 核心，当前缺失）：** 余额充足，precompile OOG/failure，断言 sender/target balance + storage + logs **全部回滚**。当前 `PrecompileRouterEnvelopeTest` 只覆盖 success / insufficient-balance，**不覆盖此路径**。新增 `c5b_precompile_failure_reverts_value_transfer`，depth0 + depth1 双断言。
2. **gas_left 保留：** 嵌套 `INSUFFICIENT_BALANCE` 断言 `gas_left == message.gas`（error spec §4.2）。
3. **CREATE characterization：** 锁定两路径当前 CREATE stateRoot/gas（`PrecompileRouterCharacterizationTest` 风格 baseline 常量）。

### 7.2 重构后

4. **收敛双 harness：** `runDepth0` / `runDepth1` 收敛为 `ExecutionFrameTest` 单 harness（直接构造 `FrameRequest`）+ 薄 integration smoke（`executeMessage` / `EthHost::call` 各一条），验证两入口委托同一 Frame。
5. **验收（north star）：** GST/EEST `stateRoot`/`gasUsed`/`logsHash` 在 ETH 参考 profile 下与 geth 一致（无 `expectException` 用例）。
6. **回归保护：** 现有 `PrecompileRouterEquivalenceTest` / `PrecompileRouterCharacterizationTest` 保持绿；若 envelope 修复改变了「错误固化」的 baseline 常量，更新常量并在 commit message 说明 geth 依据。

### 7.3 测试矩阵

| 用例 | depth0 | depth1 | 断言 |
| --- | --- | --- | --- |
| identity precompile success | ✓ | ✓ | status/gasLeft/balance 等价 |
| 付费 precompile 失败 | ✓ | ✓ | balance/storage/logs 回滚（新增） |
| insufficient balance | ✓ | ✓ | status + gas_left=message.gas |
| empty account success | ✓ | ✓ | SUCCESS + value committed |
| CREATE success | ✓ | ✓ | create_address / nonce / code install |
| CREATE endowment 不足 | ✓ | ✓ | revert + gas 保留 |
| chain precompile（FISCO/OP hook） | ✓ | ✓ | 经 extension，Frame 信封一致 |

---

## 8. 错误处理与边界

| 场景 | Frame 行为 | 下游（外壳/编排） |
| --- | --- | --- |
| 余额不足（CALL/CREATE） | revert + `INSUFFICIENT_BALANCE`(gas 保留) + `commitment=Reverted` | 顶层：编排判 consensus-reject（error spec §3.2） |
| precompile/字节码 vmerr | 原始 status + revert | 顶层：`postAdopt` included-tx normalize（ADR-015） |
| delegatecall-to-precompile 禁止 | 外壳 guard 返回 `PRECOMPILE_FAILURE`（不入 Frame） | — |
| CREATE code-deposit OOG | `applyCreateCodeDepositGas` → 对应 status + revert | error spec CREATE 错误族 |
| 未预期异常 | Frame **不** catch；由编排 `mapException` 兜底（ADR-019） | `if has_checkpoint() revert()` |

> Frame **不** include 链特有异常类型；不做 normalize；不碰 self-destruct finalize（top-level only）。

---

## 9. 文件布局

```text
bcos-evm/eth/execution/
  ExecutionFrame.h        # FrameRequest / FrameOutcome / runExecutionFrame
  ExecutionFrame.cpp      # 固定信封 implementation
bcos-evm/eth/precompiled/
  PrecompileRouter.h      # tryDispatchPrecompile（瘦身）
  PrecompileRouter.cpp    # 删 envelope
bcos-evm/eth/
  executeMessage.cpp      # 变薄：tx-level → FrameRequest → runExecutionFrame
bcos-evm/eth/state/
  EthHost.cpp             # call() 变薄：routeCall → FrameRequest → runExecutionFrame
bcos-evm/test/eth/
  ExecutionFrameTest.cpp              # 新单 harness
  PrecompileRouterEnvelopeTest.cpp    # 增付费失败回滚用例
  PrecompileRouterCharacterizationTest.cpp  # CREATE baseline 锁定
bcos-evm/docs/adr/
  020-execution-frame-envelope.md     # 新 ADR
```

---

## 10. 实施顺序（建议给 writing-plans）

1. **characterization 锁定**（7.1.3）：记录 CREATE 两路径当前行为，付费 precompile 失败 baseline。
2. **抽 ExecutionFrame**：先把 `EthHost::call` 信封逐字搬入 `runExecutionFrame`，`EthHost::call` 委托之；保持行为不变，全测试绿。
3. **executeMessage 接入**：顶层（含空 code precompile）改走 `runExecutionFrame`；删顶层 inline 信封。
4. **Router 瘦身**：移除 transfer/checkpoint/finalizeEnvelope，Frame 接管 ④③⑦。
5. **修候选 3 顺序**：checkpoint 先于 transfer；加付费失败回滚测试。
6. **修 gas_left**：`INSUFFICIENT_BALANCE` 保留 gas。
7. **统一 CREATE bind 顺序**（5.4）：切 geth 顺序，GST CREATE 验证。
8. **收敛测试 harness** + GST/EEST 验收 + 写 ADR-020 + 更新 capability-matrix。

> 每步独立可测、可回滚；2–4 为纯结构搬运（行为等价），5–7 为语义修复（行为改变，需 geth 依据 + matrix 更新）。

---

## 11. 非目标重申与 spec 边界

- **EthTxOutcome / included-tx normalize / orchestration settlement** → error-handling-parity spec（本 spec 仅引用其 §4.1/§4.2 为验收）。
- **ActivePrecompileSet warm/dispatch 单源**（候选 2）→ revision-single-source spec（正交，本 spec 不动 warm 集）。
- 本 spec 终态交付物：`ExecutionFrame` module + `PrecompileRouter` 瘦身 + ADR-020 + 测试矩阵。

---

## 12. 变更记录

| 日期 | 说明 |
| --- | --- |
| 2026-06-23 | 初版：候选 1+3 收敛为 ExecutionFrame 深 module（brainstorming 方案 A，grilling D1–D9） |

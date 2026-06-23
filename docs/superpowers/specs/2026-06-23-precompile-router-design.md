# PrecompileRouter — Kernel 统一 Precompile Dispatch — 设计规格

**日期：** 2026-06-23  
**版本：** v1.0  
**状态：** 已批准（brainstorming 范围 A）  
**范围决策：** **Phase 1 only** — 统一 `executeMessage` 顶层与 `EthHost::call` 嵌套的 precompile dispatch（precedence + execution envelope）；**不**做 Phase 2 顶层 spine 收敛；**不**做 Precompile Port（bcos→executor 解耦）  
**前置：** [2026-06-18-bcos-evm-layer-refactor-design.md](./2026-06-18-bcos-evm-layer-refactor-design.md) §14、`bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md`、`bcos-evm/docs/adr/005-orchestration-domain-boundaries.md`、`bcos-evm/capability-matrix.md`（chain precompile / builtin precompiles 行）

---

## 1. 背景与动机

### 1.1 问题

当前 kernel 存在 **两套 precompile dispatch 策略**：

| 路径 | 文件 | Precedence | Envelope |
|------|------|------------|----------|
| 顶层 tx | `eth/executeMessage.cpp` | **builtin → chain** | checkpoint + value transfer + commit |
| 嵌套 CALL | `eth/state/EthHost.cpp` | **chain → builtin** | 短路 return，无 checkpoint / value transfer |

这与已批准设计 §14 钩子顺序表（`tryChainPrecompile` → `EthPrecompiles` → …）不一致，且导致：

1. **同一 precompile 目标**在 depth=0 与 depth>0 可能产生不同结果（审计已记录 7212/0x0100 类分叉）。
2. **维护 leverage 低**：新 builtin 或 chain hook 需改两处。
3. **测试 locality 差**：`PrecompileInCallTest` 测 nested；`EmptyCodeHookTest` 测顶层；无等价 contract。

### 1.2 范围 A 决策（brainstorming 确认）

- **做：** kernel dispatch 统一（precedence + envelope）。
- **不做：** FISCO chain vs builtin **产品 precedence** 的 re-design；不修改 matrix deviation 语义定义。
- **不做：** Phase 2（顶层 execution 全收敛到 `EthHost::call`）。
- **不做：** `PrecompiledManager` / `bcos-executor` 解耦（architecture review #3，独立 spec）。

---

## 2. 目标与非目标

### 2.1 目标

1. 新增 `PrecompileRouter` module，`executeMessage` 与 `EthHost::call` **共用**。
2. 冻结统一 precedence：**chain → builtin**（与设计 §14、嵌套现状、FISCO deviation 文档一致）。
3. 统一 execution envelope：value transfer、checkpoint、commit/revert、empty-account fallback。
4. 顶层与嵌套对同一 precompile 场景 **等价**（`PrecompileRouterEquivalenceTest`）。
5. 现有 regression 套件不退化（见 §7）。

### 2.2 非目标

- 不修改三 orchestrator（`executeViaEth` / `executeViaHost` / `opStackExecuteViaHost`）。
- 不主动对齐 geth 7212 语义（6900 gas + verify）；统一 envelope 后行为以 equivalence test 记录为准。
- 不修改 `EthPrecompiles::dispatch` / `tryDispatchInCall` 内部 gas 数学。
- 不修改 `HostExtension` interface。
- 不新增 `bcos/` 或 `opstack/` include 到 `eth/`。

---

## 3. PrecompileRouter Module

### 3.1 位置

```
bcos-evm/eth/precompiled/PrecompileRouter.h
bcos-evm/eth/precompiled/PrecompileRouter.cpp
```

链接目标：`bcos-evm-eth`（与 `PrecompileActive.h`、`EthPrecompiles` 同库）。

### 3.2 Public interface

```cpp
namespace bcos::evm::precompiled {

enum class PrecompileDispatchOutcome {
    NotApplicable,       // 非 precompile 场景；caller 继续 EVM
    Dispatched,          // precompile 已执行
    EmptyAccountSuccess  // 空 code、chain/builtin 均未 dispatch
};

struct PrecompileRouterInput {
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    state::HostExtension* extension;  // nullable
    evmc_message const& message;
    evmc_address target;              // resolve 后的 code/recipient
    bool skipValueTransfer;           // true when extension->skipHostValueTransfer()
};

struct PrecompileRouterOutput {
    PrecompileDispatchOutcome outcome{PrecompileDispatchOutcome::NotApplicable};
    evmc::Result result{};
    int64_t gasRefund{0};
};

PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input);

}  // namespace
```

### 3.3 职责边界

| 在 Router 内 | 不在 Router 内 |
|--------------|----------------|
| precedence（chain → builtin） | 7702 authorization apply |
| `isActivePrecompile` + empty code 判定 | tx-entry warm（`warmTransactionEntry`） |
| value transfer（`Transfer.h`） | `prepareMessage` / CREATE bind |
| checkpoint / commit / revert | `vm.execute` |
| empty-account SUCCESS fallback | DELEGATECALL 到 precompile 的 **门控决策**（caller 负责，见 §5.2） |
| 调用 `EthPrecompiles::tryDispatchInCall` | gas 扣减算法本身 |

---

## 4. Precedence

### 4.1 冻结顺序

```
1. extension->tryChainPrecompile(revision, message)  → 若返回 value，Dispatched
2. EthPrecompiles::tryDispatchInCall(target, ...)    → 若返回 value，Dispatched
3. 若 empty code 且两者均未 dispatch                 → EmptyAccountSuccess
4. 否则                                              → NotApplicable
```

**依据：** 2026-06-18 设计 §14；当前 `EthHost::call` 嵌套路径；capability-matrix chain precompile deviation（FISCO precedence）。

### 4.2 Characterization 门控（PR-1）

合并前新增 `PrecompileRouterCharacterizationTest`，记录 **现状** depth=0 vs depth=1 行为快照：

| Case ID | 场景 |
|---------|------|
| C1 | builtin only（identity 0x04） |
| C2 | chain only（Op `OpHostExtension` + L1Block 地址，参考 `EmptyCodeHookTest`） |
| C3 | empty EOA |
| C4 | DELEGATECALL → precompile + `allowDelegateCallToPrecompile=false` |
| C5 | CALL + non-zero value → builtin precompile |
| C6 | modexp / BLS revision gate（可选，复用现有 gate test 数据） |

**合并后：** `PrecompileRouterEquivalenceTest` 断言 depth=0（经 `executeMessage` 或 router 直调）≡ depth=1（经 `EthHost::call`）。

### 4.3 顶层 precedence 变更说明

统一为 chain → builtin 后，**顶层历史 builtin-first 视为 bugfix 对齐嵌套**，不是 FISCO 产品 precedence re-design。若 `CompatExecuteViaHost` 失败，以 characterization baseline + 产品确认决定是否保留顶层例外（**本 spec 默认无例外**）。

---

## 5. Execution Envelope

### 5.1 统一流程（`PrecompileExecutionScope` 内联于 Router）

```
1. if message.value != 0 && !skipValueTransfer:
       if !canTransfer → INSUFFICIENT_BALANCE, gas_left=0, outcome=Dispatched, 不 commit
2. state.checkpoint()
3. 按 §4.1 precedence dispatch
4. if Dispatched:
       status SUCCESS → state.commit(); gasRefund = state.get_refund()
       else           → state.revert()
5. if EmptyAccountSuccess:
       result = SUCCESS, gas_left = message.gas
       state.commit(); gasRefund = state.get_refund()
6. return PrecompileRouterOutput
```

### 5.2 DELEGATECALL 门控

**保留在 `EthHost::call`**（Router 调用之前），与现 277–281 行等价：

```cpp
if (msg.kind == EVMC_DELEGATECALL && isPrecompileTarget && extension &&
    !extension->allowDelegateCallToPrecompile())
    return PRECOMPILE_FAILURE;
```

FISCO `allowDelegateCallToPrecompile() == false` 行为不变。

### 5.3 Empty code 非 precompile

- `EmptyAccountSuccess`：`EVMC_SUCCESS`，`gas_left = message.gas`（与现 `executeMessage` 顶层 `makeSuccessResult` 一致）。
- 嵌套路径对齐到同一 outcome（现 nested 可能走 EVM 空 code，统一后一致）。

---

## 6. 集成点

### 6.1 `executeMessage.cpp`

**删除：** 约 214–273 行（顶层 builtin dispatch、empty code + chain hook、empty SUCCESS 块）。

**保留：** 7702 auth apply（198–211）、CREATE 路径、`vm.execute` 块、installCreatedContractCode。

**新增：** empty code 分支调用 `dispatchPrecompile`：

```cpp
if (!isCreateKind(input.message.kind)) {
    auto const codeAddress = resolveCodeAddress(input.message);
    auto code = state.get_code(codeAddress);
    code = resolveExecutableCode(state, std::move(code), input.revisionConfig.eip7702);
    if (code.empty()) {
        bool const skipVt = input.extension && input.extension->skipHostValueTransfer();
        auto routed = precompiled::dispatchPrecompile({
            state, input.revisionConfig, input.extension, input.message,
            codeAddress, skipVt});
        if (routed.outcome != PrecompileDispatchOutcome::NotApplicable) {
            output.result = std::move(routed.result);
            output.gasRefund = routed.gasRefund;
            output.stateDiff = state.build_diff();
            output.logs = host.take_logs();
            return output;
        }
    }
    // non-empty code → 现有 vm.execute 路径
}
```

### 6.2 `EthHost.cpp`

**删除：** 约 268–290 行 precompile 短路。

**新增：** `routeCall` 之后、`prepareMessage` 之前：

```cpp
if (routed.hasPrecompileTarget || shouldTryEmptyCodePrecompile(routed)) {
    auto out = precompiled::dispatchPrecompile({...});
    if (out.outcome != NotApplicable)
        return Result(std::move(out.result));
}
```

`shouldTryEmptyCodePrecompile`：empty code 且非 CREATE（与现 `executeMessage` empty 分支条件对齐）。

**不变：** `prepareMessage` → CREATE bind → checkpoint（EVM 路径）→ `transferValue` → `vm.execute`；嵌套 EVM 路径仍保留独立 checkpoint（precompile 路径 envelope 已在 Router 内完成）。

### 6.3 CMake

`bcos-evm/CMakeLists.txt` → `BCOS_EVM_ETH_SOURCES` 增加 `eth/precompiled/PrecompileRouter.cpp`。

---

## 7. 测试与验收

### 7.1 新增测试

| 测试 target | 阶段 | 断言 |
|-------------|------|------|
| `PrecompileRouterCharacterizationTest` | PR-1（Router 合并前） | 记录现状 depth=0/1 快照；CI 绿 |
| `PrecompileRouterEquivalenceTest` | PR-2 | C1–C5 等价 |
| `PrecompileRouterEnvelopeTest` | PR-2 | C5 value transfer；commit/revert |
| `PrecompileRouterPrecedenceTest` | PR-2 | mock extension 验证 chain 先于 builtin |

### 7.2 回归（必须 PASS）

```bash
ctest --test-dir build/bcos-evm/test -R \
  'PrecompileInCall|ExecuteMessageSmoke|EmptyCodeHook|FiscoHostExtension|Eip7212|Eip7823|Eip2537|PrecompileRevisionGate|CompatExecuteViaHost|NestedCallHost|NestedRevertWarm' \
  --output-on-failure
```

TE 侧（若本机已构建）：

```bash
ctest --test-dir build/transaction-executor/test -R 'CompatExecuteViaHost|ExecuteViaHostCompat' --output-on-failure
```

### 7.3 capability-matrix 更新（PR-2 同 PR）

| 行 | 变更 |
|----|------|
| builtin precompiles (0x01–0x11) | Test ref 追加 `PrecompileRouterEquivalenceTest` |
| chain precompile routing | Test ref 追加 `PrecompileRouterPrecedenceTest` |

**不修改** status token（仍为 inherited / deviation）。

---

## 8. 交付顺序

| PR | 内容 |
|----|------|
| **PR-1** | `PrecompileRouterCharacterizationTest` only；文档化 baseline |
| **PR-2** | `PrecompileRouter` + 集成 + equivalence/envelope/precedence tests + matrix Test ref |

估时：**3–4 人天**。

---

## 9. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 顶层 builtin-first → chain-first 边缘 BCOS case | PR-1 characterization + CompatExecuteViaHost |
| 嵌套新增 checkpoint/value transfer 改变 balance/refund | `PrecompileRouterEnvelopeTest` + 回归 |
| 7212 统一后行为变化 | 记录于 equivalence test；不在本 spec 主动修 geth 对齐 |
| Router 与 EVM 路径双 checkpoint | precompile 短路在 Router 内完整 envelope；EVM 路径保持现 `EthHost` checkpoint |

---

## 10. 审批记录

| 项 | 状态 |
|----|------|
| 范围 A（Phase 1 only，不 re-design FISCO precedence） | **已确认（2026-06-23）** |
| 统一 precedence：chain → builtin | **已确认（2026-06-23）** |
| 统一 envelope（value + checkpoint + commit/revert） | **已确认（2026-06-23）** |
| 全文 | **已批准（2026-06-23）** |

---

## Spec Self-Review（2026-06-23）

| 检查项 | 结果 |
|--------|------|
| TBD/TODO 占位 | 无 |
| 内部一致性 | precedence / envelope / 集成点与 §14、ADR-005 一致；DELEGATECALL 门控明确在 EthHost |
| 范围聚焦 | Phase 1 only；Port 与 spine 合并明确排除 |
| 歧义 | empty code 判定与 CREATE 排除已写明；顶层 precedence 变更归类为 bugfix 对齐 |

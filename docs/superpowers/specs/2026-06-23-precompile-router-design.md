# PrecompileRouter — Kernel 统一 Precompile Dispatch — 设计规格

**日期：** 2026-06-23  
**版本：** v1.2（二次审查修订）  
**状态：** 已批准（brainstorming 范围 A + grilling 方案 A + v1.2 审查补丁）  
**范围决策：** **Phase 1 only** — 统一 `executeMessage` 顶层与 `EthHost::call` 嵌套的 precompile dispatch（precedence + execution envelope）；**不**做 Phase 2 顶层 spine 收敛；**不**做 Precompile Port（bcos→executor 解耦）  
**前置：** [2026-06-18-bcos-evm-layer-refactor-design.md](./2026-06-18-bcos-evm-layer-refactor-design.md) §14、`bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md`、`bcos-evm/docs/adr/005-orchestration-domain-boundaries.md`、`bcos-evm/capability-matrix.md`（chain precompile / builtin precompiles 行）

**v1.2 修订摘要（二次审查）：**
- §5.1：余额检查后、checkpoint 前显式 `transfer(sender → target)`
- §3.4 / §6.2：`EthHost::call` 排除 CREATE/CREATE2；澄清 `input.target` 为 transfer 目标
- §7.1：`PrecompileRouterCharacterizationTest` 链接 `bcos-evm` + `bcos-evm-op`（C2）

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
4. **empty code** 场景下顶层与嵌套 **等价**（`PrecompileRouterEquivalenceTest`，见 §2.2）。
5. 现有 regression 套件不退化（见 §7）。

### 2.2 等价范围（grilling 2026-06-23 — 方案 A）

Router **内部**流程唯一；**caller 调用时机**双入口：

| Caller | 何时调用 `dispatchPrecompile` | 理由 |
|--------|------------------------------|------|
| `EthHost::call` | **每次** CALL/STATICCALL/DELEGATECALL（`routeCall` 后、DELEGATECALL 门控后） | 对齐嵌套现状：chain hook 对 non-empty `[PRECOMPILED]` code 亦生效 |
| `executeMessage` | 仅 **empty code**（7702 resolve 后、非 CREATE） | 对齐顶层现状：non-empty code 直进 `vm.execute` |

**Phase 1 等价承诺：** 仅 **empty code** 路径 depth=0 ≡ depth>0（C1–C5）。

**刻意不在 Phase 1 等价：** 顶层 non-empty code CALL 到 FISCO `[PRECOMPILED]` 合约（depth=0 走 EVM，depth>0 走 chain hook）。PR-1 用 **C7** characterization 记录该不对称；后续独立 spec 若需对齐。

### 2.3 非目标

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

### 3.4 双入口约定（grilling — 方案 A）

`dispatchPrecompile` **内部**始终按 §4.1 执行（含 chain 段）。差异仅在 caller 是否 invoke：

- **`EthHost::call`：** 仅 **CALL / STATICCALL / DELEGATECALL** invoke（**排除 CREATE / CREATE2**，见 §6.2）；`NotApplicable` → 继续 `prepareMessage` / EVM 路径。
- **`executeMessage`：** 仅 **empty code** 时 invoke（7702 resolve 后、非 CREATE）；non-empty 不 invoke（§6.1）。

Router 内通过 `state.get_code(target).empty()` 判定 empty code；**builtin 段**额外要求 `isActivePrecompile(revision, cfg, target)`（与现顶层 214–215 行门控一致）。

**Value transfer 目标地址：** `PrecompileRouterInput.target` 与 caller 侧 `resolveCodeAddress(message)` / `routeCall` 后的 target 一致（`code_address` 非零时取 `code_address`，否则 `recipient`）。Router 内 `transfer(sender, target, value)` 使用该字段，对齐现 `applyTopLevelValueTransfer` 与 `EthHost::transferValue` 语义。

---

## 4. Precedence

### 4.1 冻结顺序

```
1. extension->tryChainPrecompile(revision, message)  → 若返回 value，Dispatched
2. if empty code && isActivePrecompile(revision, cfg, target):
       EthPrecompiles::tryDispatchInCall(...)        → 若返回 value，Dispatched
3. if empty code && 仍未 dispatch                    → EmptyAccountSuccess
       （含 7212/0x0100 等 active 但未 wired：嵌套从 fallthrough vm.execute
         改为 EmptyAccountSuccess — intentional，equivalence test 锁定）
4. else                                              → NotApplicable
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
| C7 | FISCO `[PRECOMPILED]` non-empty code，nested chain hook vs 顶层 EVM（**记录不对称**，PR-2 不要求等价） |

PR-1 baseline：测试内注释或具名常量记录 depth=0/1 的 `status_code` / `gas_left`；**允许 C7 断言「当前不等价」**，PR-2 仅对 C1–C5 改为等价。

**合并后：** `PrecompileRouterEquivalenceTest` 断言 **C1–C5** depth=0（`executeMessage`）≡ depth=1（`EthHost::call`）。

### 4.3 顶层 precedence 变更说明

统一为 chain → builtin 后，**顶层历史 builtin-first 视为 bugfix 对齐嵌套**，不是 FISCO 产品 precedence re-design。若 `CompatExecuteViaHost` 失败，以 characterization baseline + 产品确认决定是否保留顶层例外（**本 spec 默认无例外**）。

---

## 5. Execution Envelope

### 5.1 统一流程（`PrecompileExecutionScope` 内联于 Router）

```
1. if message.value != 0 && !skipValueTransfer:
       value = fromEvmC(message.value)
       if !canTransfer(state, sender, value)
           → INSUFFICIENT_BALANCE, gas_left=0, outcome=Dispatched, 不 checkpoint/commit, return
       transfer(state, sender, target, value)   // Transfer.h；target 见 §3.4
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

**余额不足（grilling 2026-06-23 — 方案 A）：** 顶层与嵌套 precompile 路径均如此；`CALL + value > 0` 且 `!canTransfer` 时返回 `EVMC_INSUFFICIENT_BALANCE`、`gas_left=0`，**不** checkpoint/commit（v1.2 明确：先于 step 2，与现 `executeMessage` 先 checkpoint 再 revert 的 journal 细节略有不同，语义等价，由 C5 envelope test 锁定）。

**成功路径 value transfer（v1.2 补充）：** step 1 在 checkpoint **之前**完成 `canTransfer` 检查与实际 `transfer()`，与现顶层 `applyTopLevelValueTransfer` / 嵌套将新增的 transfer 行为一致。

### 5.2 DELEGATECALL 门控

**保留在 `EthHost::call`**（Router 调用之前），与现 277–281 行等价：

```cpp
if (msg.kind == EVMC_DELEGATECALL && isPrecompileTarget && extension &&
    !extension->allowDelegateCallToPrecompile())
    return PRECOMPILE_FAILURE;
```

FISCO `allowDelegateCallToPrecompile() == false` 行为不变。

**顶层 `executeMessage`：** Phase 1 不经过 `EthHost::call` 的 DELEGATECALL 门控；若 depth=0 DELEGATECALL 直调 precompile 成为实测问题，单列 follow-up（不在本 PR）。

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

**删除：** 约 268–290 行独立 `tryChainPrecompile` + builtin 短路。

**新增：** DELEGATECALL 门控（§5.2）之后、`prepareMessage` 之前 — 对 **非 CREATE** 的 call kind invoke router（方案 A）：

```cpp
if (!isCreateKind(callMessage.kind))
{
    bool const skipVt = m_extension && m_extension->skipHostValueTransfer();
    auto const target = isZeroAddress(callMessage.code_address) ?
                            callMessage.recipient :
                            callMessage.code_address;
    auto out = precompiled::dispatchPrecompile({
        m_state, m_revisionConfig, m_extension, callMessage, target, skipVt});
    if (out.outcome != PrecompileDispatchOutcome::NotApplicable)
        return Result(std::move(out.result));
}
```

**CREATE / CREATE2 排除（v1.2）：** 普通 CREATE 目标账户 `code` 为空但非 precompile 场景；若 invoke router 会错误返回 `EmptyAccountSuccess` 而跳过 init code 执行。CREATE 路径保持现有 `bindCreateMessageForInit` → checkpoint → `transferValue` → `vm.execute` 不变。

`hasPrecompileTarget` **不再**作为「是否 invoke router」的外层条件；DELEGATECALL 门控仍使用 `routed.hasPrecompileTarget`（§5.2）。

**不变：** `prepareMessage` → CREATE bind → checkpoint（EVM 路径）→ `transferValue` → `vm.execute`；嵌套 EVM 路径仍保留独立 checkpoint（precompile 路径 envelope 已在 Router 内完成）。

### 6.3 CMake

`bcos-evm/CMakeLists.txt` → `BCOS_EVM_ETH_SOURCES` 增加 `eth/precompiled/PrecompileRouter.cpp`。

---

## 7. 测试与验收

### 7.1 新增测试

| 测试 target | 阶段 | 断言 |
|-------------|------|------|
| `PrecompileRouterCharacterizationTest` | PR-1（Router 合并前） | 记录现状 depth=0/1 快照（C1–C7）；链接 `bcos-evm` + `bcos-evm-op`；CI 绿 |
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

估时：**4–5 人天**（含 C7 characterization 与 FISCO `[PRECOMPILED]` nested 回归）。

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
| Grilling：Router 方案 A（双入口） | **已确认（2026-06-23）** |
| Grilling：等价范围 empty-code only（C7 记录 non-empty 不对称） | **已确认（2026-06-23）** |
| Grilling：C5 余额不足 → INSUFFICIENT_BALANCE / gas_left=0（方案 A） | **已确认（2026-06-23）** |
| v1.2：§5.1 显式 `transfer()`；§6.2 排除 CREATE | **已确认（2026-06-23）** |

---

## Spec Self-Review（2026-06-23 v1.2）

| 检查项 | 结果 |
|--------|------|
| TBD/TODO 占位 | 无 |
| 内部一致性 | §3.4 / §6.1 / §6.2 双入口一致（EthHost 排除 CREATE；executeMessage empty-only）；§5.1 transfer 与 §3.3 职责一致 |
| 范围聚焦 | Phase 1 only；C7 明确排除 non-empty 等价；Port 与 spine 合并排除 |
| 歧义 | CREATE 排除 explicit；value transfer 使用 `input.target` explicit；7212 fallthrough 变更 explicit |

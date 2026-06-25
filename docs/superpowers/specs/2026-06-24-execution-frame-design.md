# ExecutionFrame — 统一帧执行 deep module

**Status:** Accepted (post-grilling)  
**Date:** 2026-06-24  
**Last reviewed:** 2026-06-24 third pass (subagent review merge)  
**Implementation status:** **Done** — `eth/execution/ExecutionFrame.*`；`executeMessage` + `EthHost::call` delegate to `runExecutionFrame` (PR1–PR2, commits `ea1e4f2dc..274047e4a`).  
**Related:** ADR-005, ADR-017, ADR-019, `bcos-evm/docs/architecture-overview.md`, `bcos-evm/docs/architecture-review-post-orchestration-2026-06-23.md` §候选 1/3

### Terminology alignment (spec ↔ current code)

| Spec (grilling era) | Current code |
| --- | --- |
| `runOrchestration()` | `runTxPipeline()` (`eth/orchestration/TxPipeline.cpp`) |
| `HostExtension*` | `state::VmHostPolicy*` (`eth/policy/VmHostPolicy.h`; FISCO/OpStack 子类) |
| `FiscoHostExtension` | `FiscoVmHostPolicy` |
| `hooks.prepareMessage` (top-level CREATE) | FISCO：`txSetupMessage` → `deriveMessage()`；ETH ref：message 已在进 `executeMessage` 前就绪 |
| `hooks.executeMessageOverride` | `TxPipelineHooks::txRunEvmExecutionOverride` |
| `OrchestrationContext` | `TxPipelineContext` |
| `mapException` | `OrchestrationErrorPolicy::onPipelineException` |

---

## 1. Problem

ADR-019 将三链编排收敛到 `runTxPipeline()`，但 **帧级 EVM 执行**仍拆成两条 implementation path：

| Path | Entry | File |
| --- | --- | --- |
| Top-level frame (depth=0) | `executeMessage()` | `eth/ExecuteMessage.cpp` |
| Nested frame (depth>0) | `EthHost::call()` | `eth/state/EthHost.cpp` |

同一 CALL/CREATE 帧语义（precompile envelope、value transfer、CREATE finalize、7702 路由、`VmHostPolicy` hooks）在两处重复维护。`PrecompileRouterEnvelopeTest`（`test/eth/`）与 `PrecompileRouterCharacterizationTest` / `PrecompileRouterEquivalenceTest`（`test/cross/`）通过 `runDepth0` / `runDepth1` 手工对齐，说明团队已知双轨 drift 风险。

**2026-06-24 代码核对：** 双轨仍成立——`ExecuteMessage.cpp:229` 与 `EthHost.cpp:303` 各有一处 `precompiled::dispatchPrecompile`；Nested 路径 ③ 先于 ②（`tryPrecompile` 在 `prepareMessage` 之前，RR6 仍准确）。

**Deletion test 失败：** 删除任一路径的帧逻辑，复杂度不会消失，而是散落到另一路径及 call site。

---

## 2. Decision

引入 **`ExecutionFrame` deep module**：单一 `runExecutionFrame()` 承载全部帧级语义；`executeMessage` 与 `EthHost::call` 降为 thin adapter。

### 2.1 分层（与 ADR-019 同构）

```text
tx 级 (不变)          帧级 (新增)                    evmc 级 (thin)
─────────────         ─────────────                  ──────────────
runTxPipeline     →   runExecutionFrame()       →    EthHost::call()
executeMessage        FrameContext + FrameScope        evmone VM
(tx adapter)          (deep module)
```

### 2.2 Seam 纪律

- 新增代码位于 `bcos-evm/eth/execution/`。
- **`eth/execution/` 不得 `#include` `bcos/` 或 `opstack/`**（延续 ADR-005 / ADR-019）。
- 链定制仅通过 `FrameContext::extension`（`state::VmHostPolicy*`）注入。
- `PrecompileRouter::dispatchPrecompile` 保留为独立 module；**Frame 是唯一 call site**。

---

## 3. Public interface

### 3.1 Files

```text
bcos-evm/eth/execution/
  # 已有（tx 级 helper，非 Frame 本体）
  WarmTransactionEntry.h    # canonical：`warmTransactionEntry(revisionConfig, ...)`（include 路径与文件名一致）
  BlockInfoBuilder.h
  TxFeaturePrepare.h
  # PR1 新增
  FrameScope.h              # enum FrameScope（或合并进 ExecutionFrame.h）
  RouteMessage.h / .cpp
  FrameCaller.h             # resolveCallerAddress（自 EthHost private 迁出，R10）
  Delegation7702Frame.h     # isDirectDelegated7702 / isDelegated7702Message（RouteMessage + Frame 共用）
  ResolveExecutionCode.h
  FrameValueTransfer.h
  ExecutionFrame.h / .cpp
```

### 3.2 Types

```cpp
namespace bcos::evm::execution {

enum class FrameScope { TopLevel, Nested };

struct FrameContext {
    state::State& state;
    evmc::VM& vm;
    bcos::evm_standard::RevisionConfig const& revisionConfig;
    state::VmHostPolicy* extension{nullptr};
    evmc_address txOrigin;
    evmc_address& executionAddress;
    bool fixNonceInit{false};
};

struct FrameResult {
    evmc::Result result;
    int64_t gasRefund{0};
    bool precompileHit{false};
};

FrameResult runExecutionFrame(
    FrameContext& ctx,
    evmc_message message,
    FrameScope scope,
    state::EthHost& host);

}  // namespace bcos::evm::execution
```

**Invariants:**

- `FrameContext` construction-valid；无 default constructor（mirror `TxPipelineContext`）。
- `runExecutionFrame` 同步；不 catch exception（`OrchestrationErrorPolicy::onPipelineException` 仍在外层 `runTxPipeline`，ADR-019 Q6/Q20）。
- 不使用 hooks / `std::function`；TopLevel vs Nested 差异由 `FrameScope` enum 内聚。
- **`FrameScope` 由 adapter 显式传入**；Frame 内部不得用 `message.depth` 驱动行为分支（仅可用于 trace/logging）。`executeMessage` 虽可接收 `depth>0`（测试/遗留路径），生产 tx 路径恒传 `TopLevel`；nested 帧仅经 `EthHost::call` → `Nested`。
- `runExecutionFrame` 接受 `state::EthHost&`；`ExecutionFrame.h` forward declare `EthHost`，`.cpp` include 实现（接受 execution↔Host concrete coupling）。
- `FrameContext` 只携带 Frame 真正需要的数据：`txOrigin`，不暴露完整 `evmc_tx_context`。
- `createdInTx` 不进入 `FrameContext`；Frame 通过 `host.markCreatedInTx(addr)` 标记 created-in-tx。
- `EthHost` 提供窄 accessor `execution_address_ref() noexcept` 供 adapter 构造 `FrameContext`；不使用 `friend` 暴露 private layout。
- `resolveCallerAddress` **迁入** `eth/execution/FrameCaller.h`（R10）；PR1 删除 `EthHost` private 版本。

---

## 4. Pipeline (fixed 9 steps)

`runExecutionFrame` 按固定顺序执行；任一步 early-return 则跳过后续主路径步骤。

> **PR1 执行序约定（RR6 / RR7）：** 下表 step 编号是**逻辑分组**，不代表 PR1 各 scope 的实际执行序。PR1 **严格冻结现状每-scope 顺序**以满足 RR3 等价；真正合一延后到独立 PR。
>
> - **RR6（Nested ②/③）：** `EthHost::call` 为 `tryPrecompile`(③) **先于** `setCallerAddress + prepareMessage`(②)，因 `prepareMessage` 会 mutate `callMessage` 而 precompile `target` 需在其之前求值。
> - **RR7（CREATE ④/⑤）：** TopLevel 与 Nested **相反**——
>   - **TopLevel CREATE（现状 `executeMessage`）：** ⑤ checkpoint → ⑦ transfer skip → ④ bindCreate → endowment → ⑥ initAccount → vm
>   - **Nested CREATE（现状 `EthHost::call`）：** ② prepareMessage → ④ bindCreate → ⑤ checkpoint → ⑥ initAccount → ⑦ transfer → vm
>   PR1/PR2 不得统一此顺序；`bindCreateMessageForInit` 虽无 state mutation，但顺序影响 revert 边界，须逐 scope 冻结。

| Step | Function | Responsibility |
| --- | --- | --- |
| ① | `routeMessage` | 7702 路由 + code_address 解析 + precompile target 标记（both scope）；CREATE recipient/code_address 互填 + EIP-2929 warm pin（**Nested only**，见 §4.1） |
| ② | `guardChecks` | DELEGATECALL→precompile 拒绝；Nested：`extension.setCallerAddress` + `prepareMessage` |
| ③ | `tryPrecompile` | 唯一 `precompiled::dispatchPrecompile` 调用；hit → finalize envelope → return；**统一 EthHost 7702 guard**（见 §4.2） |
| ④ | `prepareExecution` | CREATE：`bindCreateMessageForInit` |
| ⑤ | `checkpoint` | `state.checkpoint()`（主路径；Router 内部仍有独立 checkpoint） |
| ⑥ | `initializeCreateAccount` | CREATE：`initializeCreateTargetAccount` |
| ⑦ | `transferFrameValue` | CALL value + CREATE endowment；统一 `skipHostValueTransfer` |
| ⑧ | `runVm` | `resolveExecutionCode` + `vm.execute(host, ...)` |
| ⑨ | `finalizeFrame` | codeDepositGas；installCode；markCreatedInTx；nonce bump；commit/revert；Nested 更新 executionAddress |

### 4.1 FrameScope branching

| Step | TopLevel | Nested |
| --- | --- | --- |
| ① routeMessage code_address 解析 + precompile target | **Run** — PR2 前现状用 `resolveCodeAddress()`（较 `routeCall` 简化，无 `isDirectDelegated7702` / `hasPrecompileTarget`）；PR2 迁 TopLevel 时须 parity 验证 | **Run** — 完整 `routeCall` 逻辑 |
| ① routeMessage CREATE recipient 互填 + warm-pin | **Skip** — 顶层 CREATE 地址在进 Frame 前已就绪（FISCO：`txSetupMessage`→`deriveMessage()`；ETH ref：caller 已填 `message.recipient`） | **Run** |
| ② `prepareMessage` | **Skip** — 顶层不调用 `VmHostPolicy::prepareMessage` | **Run** — `extension->prepareMessage()`（FISCO 嵌套 CREATE 走 `FiscoVmHostPolicy::deriveNestedCreateAddress`） |
| ④/⑤ CREATE step order | **RR7：** ⑤ checkpoint **先于** ④ bindCreate | **RR7：** ④ bindCreate **先于** ⑤ checkpoint |
| ⑦ transfer | CALL：原 `applyTopLevelValueTransfer` 语义；CREATE：endowment 检查 + transfer（`applyTopLevelValueTransfer` 对 CREATE skip） | 原 `transferValue` 语义（函数内 scope 分支） |
| ⑨ `fixNonceInit` | 当 `ctx.fixNonceInit`：success 后 `set_nonce(createAddr, 1)` | Skip |
| ⑨ `bumpContractCreateNonce` | **Skip** | Nested **CREATE attempt**（`isCreateKind && depth>0`，**不依赖** `EVMC_SUCCESS`）且 sender ≠ tx_origin 时 **Run** |
| ⑨ sender `set_nonce+1` | **Skip in Frame** — tx adapter 在 Frame 返回后、depth=0 **success** 时 bump（含 7702 auth prebump 逻辑） | **Nested CREATE attempt**（`isCreateKind && depth>0`，**不依赖** `EVMC_SUCCESS`）— 与 `EthHost.cpp:394-405` 冻结一致；普通 nested CALL 不 bump |
| ⑨ `executionAddress` 更新 | Skip | Nested **非 CREATE success** 时更新（`EthHost.cpp:373-381`） |
| tx `finalize_self_destructs` | **不在 Frame** — `executeMessage` 在 Frame 返回后执行 | N/A |

### 4.2 Step ③ — unified precompile guard

Both scopes use the **Nested (stricter) guard**:

```cpp
!isCreateKind(msg.kind) &&
!(isDelegated7702Message(originalMsg) && routed.kind != EVMC_CALL)
```

TopLevel transactions are never DELEGATECALL/CALLCODE, so this is a no-op at depth=0 but keeps one implementation. Add nested 7702 DELEGATECALL regression in `ExecutionFrameTest` if not already covered.

### 4.3 7702 tx-level auth（不进 Frame）

以下保留在 `executeMessage`（tx adapter），在调用 `runExecutionFrame(TopLevel)` **之前**：

- `applyAuthorizations(state, authorizations, chainId)`
- sender nonce bump + `warmDelegationTarget`（当 authorization list 非空）

Frame ① `routeMessage` / ⑧ `resolveExecutionCode` 负责帧内 7702 delegation code 解析（自 `EthHost::resolveExecutionCode` 迁出）。

### 4.4 FISCO CREATE 地址（本次不统一）

- 顶层 CREATE：FISCO 在 `runTxPipeline` 的 `txSetupMessage` hook 内 `deriveMessage()` 决定地址；ETH ref 由 caller 预先构造 message。
- 嵌套 CREATE：`FiscoVmHostPolicy::prepareMessage()`（Frame ② Nested 路径）。

统一为 `FiscoAddressDerivation` module 留作后续 work；本 spec 仅固定 hook 触达点，不移动 FISCO 地址逻辑。

### 4.5 PR1 实际执行序（按 scope，勿按 ①–⑨ 表格字面排序）

**Nested（生产路径，冻结 `EthHost::call`）：**

```text
routeMessage(Nested)
→ DELEGATECALL→precompile guard（hasPrecompileTarget）
→ tryPrecompile（dispatchPrecompile；hit → return）
→ resolveCallerAddress + setCallerAddress + prepareMessage
→ [CREATE] bindCreateMessageForInit
→ checkpoint
→ [CREATE] initializeCreateTargetAccount
→ transferFrameValue(Nested)
→ resolveExecutionCode + vm.execute
→ finalizeFrame（含 commit/revert、executionAddress 更新、CREATE attempt nonce bump）
```

**TopLevel（PR1 仅 characterization，冻结 `executeMessage` 帧体）：**

```text
routeMessage(TopLevel)   # CALL：zero code_address→recipient；CREATE：skip 互填/warm-pin
→ [CALL] resolveExecutionCode；若 empty → tryPrecompile（hit → return）
→ checkpoint
→ transferFrameValue(TopLevel)   # CREATE：skip CALL transfer
→ [CREATE] bindCreate + endowment + initializeCreateTargetAccount   # RR7：checkpoint 先于 bind
→ vm.execute
→ finalizeFrame（fixNonceInit；无 sender nonce bump）
```

---

## 5. Adapter changes

### 5.1 `executeMessage` → thin tx adapter

```text
validate input
trace scope (depth=0)
resolveState + clear_refund
warmTransactionEntry(state, revisionConfig, tx, blockInfo, txProps, ...)  # tx-level
[7702] applyAuthorizations + warmDelegation       # tx-level
build EthHost + set_execution_address (non-CREATE)
build FrameContext from ExecuteMessageInput + EthHost member refs
FrameResult fr = runExecutionFrame(ctx, message, TopLevel, host)
assemble ExecuteMessageOutput:
  result ← fr.result
  logs   ← host.take_logs()
  gasRefund ← fr.precompileHit ? fr.gasRefund : state.get_refund()
  if fr.precompileHit: stateDiff ← state.build_diff(); return output
  on normal success: finalize_self_destructs(); stateDiff ← state.build_diff()
  on failure: revert path gasRefund + stateDiff (preserve current behavior)
```

**Remove from `ExecuteMessage.cpp`:** 内联 precompile dispatch、主 checkpoint、applyTopLevelValueTransfer、CREATE/vm/finalize 双轨逻辑。

### 5.2 `EthHost::call` → thin evmc adapter

```text
ExecutionAddressGuard
build FrameContext from m_state, m_revisionConfig, m_vm,
                       m_extension, m_txContext.tx_origin, execution_address_ref()
auto fr = runExecutionFrame(ctx, msg, Nested, *this)
return fr.result
```

**Remove from `EthHost.cpp`:** `routeCall`, `resolveExecutionCode`, `transferValue`, `resolveCallerAddress` 及 `call()` 内联帧逻辑（迁到 `eth/execution/`）。

**Retain in `EthHost`:** evmc Host callbacks（account/storage/log/access/selfdestruct 等）；`ExecutionAddressGuard` 仍留在 `EthHost::call`，Frame 接收 guarded `executionAddress&`，不负责恢复。

---

## 6. PrecompileRouter

- **Keep** `precompiled::dispatchPrecompile` 及现有 envelope（checkpoint → transfer → tryChainPrecompile → tryDispatchInCall → finalize）。
- **Change:** 删除 `ExecuteMessage.cpp` 与 `EthHost.cpp` 中的直接调用；仅 `ExecutionFrame.cpp` step ③ 调用。
- **Router envelope 不变：** 本轮不 refactor Router 内部 checkpoint→transfer→dispatch 顺序；Router 内 checkpoint + Frame 主路径 checkpoint（precompile miss 后）保留现状。
- ADR-017 不变：Router（kernel）与 `ChainPrecompilePort`（chain）正交。

---

## 7. Error handling

| Scenario | Behavior | Owner |
| --- | --- | --- |
| Transfer insufficient balance | `state.revert()` + `EVMC_INSUFFICIENT_BALANCE`, `gas_left=0` | Frame ⑦ |
| Precompile transfer insufficient | Router revert + same status | Router via Frame ③ |
| `vm.execute` failure | revert main checkpoint | Frame ⑨ |
| CREATE codeDepositGas failure | clear output bytes; preserve status/gas_left | Frame ⑨ |
| DELEGATECALL→precompile blocked | `EVMC_PRECOMPILE_FAILURE`; no main checkpoint | Frame ② |

Frame 不 catch exception；`runTxPipeline` 外层 `OrchestrationErrorPolicy::onPipelineException` 负责 state revert（ADR-019 Q20）。

For TopLevel precompile hit, `FrameResult::precompileHit == true` is an `executeMessage` early-return signal. The tx adapter must preserve current behavior: build `stateDiff`/`logs` immediately and return, without running `finalize_self_destructs()` or normal tx finalize.

`stateDiff` and `logs` remain owned by the `executeMessage` adapter. Frame commits/reverts state and returns `FrameResult`; the tx adapter collects `host.take_logs()` and `state.build_diff()` according to `precompileHit` and `result.status_code`.

**Precompile `gasRefund` asymmetry (frozen):** TopLevel adapter consumes `fr.gasRefund` on precompile hit (current `executeMessage` behavior). Nested adapter (`EthHost::call`) **ignores** `fr.gasRefund`, preserving current behavior where nested precompile refund is discarded. Any correction is out of scope and must be a separate PR with GST/EEST validation.

**Checkpoint ordering (RR7):** `bindCreateMessageForInit` performs no state mutation (only mutates `message` + `set_execution_address`)，但 TopLevel/Nested 对 ④/⑤ 的相对顺序**不同且须冻结**（见 §4 RR7 注）。未来若统一执行序，须分别用 TopLevel/Nested CREATE 回归用例验证 revert 边界。

---

## 8. Testing

### 8.1 New: `test/eth/ExecutionFrameTest.cpp`

**PR1 gate (before `executeMessage` migrates):**

| PR1 scope | Method |
| --- | --- |
| Nested extract equivalence | 既有 nested 测试套件（`test/eth/PrecompileRouterEnvelopeTest`、`test/cross/PrecompileRouter*Test` depth1 + 全量 `bcos-evm/test/eth/*`）在 Frame-delegated `EthHost::call` 下全绿 + `ExecutionFrameTest` 新增 Nested 用例（含 §4.2 nested 7702 DELEGATECALL guard） |
| TopLevel characterization | `runExecutionFrame(TopLevel)` vs expectations from current `executeMessage` frame logic (direct Frame test, adapter not wired yet) |
| Parity matrix | Migrate cases from `PrecompileRouterEnvelopeTest` |

**Full matrix (PR2+):**

| Category | Cases |
| --- | --- |
| TopLevel/Nested parity | Same cases via production adapters after PR2 |
| Value transfer + precompile | Balance parity depth=0 vs depth=1 |
| CREATE endowment | Insufficient balance both scopes |
| CREATE sender nonce bump | Nested CREATE **attempt**（depth>0，含 failure）；见 §4.1 |
| TopLevel CREATE step order | RR7：checkpoint → bindCreate（direct Frame test） |
| Nested CREATE step order | RR7：bindCreate → checkpoint（direct Frame test） |
| TopLevel routeMessage parity | PR2 gate：`routeMessage(TopLevel)` vs 现状 `resolveCodeAddress` + empty-code precompile path |
| DELEGATECALL→precompile | `allowDelegateCallToPrecompile=false` |
| 7702 delegation routing | Empty delegate code → precompile path; nested DELEGATECALL guard (§4.2) |
| fixNonceInit | TopLevel only |

### 8.2 Existing tests

- `PrecompileRouter*Test` — unchanged (Router unit tests).
- Deprecate/remove 全部 depth0/depth1 parity helper（含 `runDepth0`/`runDepth1`、`runDepth0EmptyCall`/`runDepth1EmptyCall` 等）once FrameTest covers parity.

### 8.3 Regression gate (each PR)

- Full `bcos-evm/test/eth/*` + `test/cross/PrecompileRouter*Test` depth1
- FISCO smoke: `FiscoExecutionBridgeSmoke`, `Bcos7702*`, `Bcos7212*`
- OpStack smoke: `OpStackExecutionBridgeSmoke`
- GST spot check (no full EEST gate required per PR)

---

## 9. Migration (3 PRs)

### PR1 — Frame module + EthHost migration

- Add `eth/execution/ExecutionFrame.*`, `RouteMessage.*`, `FrameValueTransfer.h`, `FrameCaller.h`, `Delegation7702Frame.h` 等；`bcos-evm/CMakeLists.txt` 经 `GLOB_RECURSE eth/*.cpp` 自动收录；在 `bcos-evm/test/cmake/EthTests.cmake` 注册 `ExecutionFrameTest` / `RouteMessageTest` 等。
- **Fix `bcos-evm/test/cmake/StateTests.cmake`：** 6 个内联 `EthHost.cpp` 的 target（PragueState、NestedCallHost、PrecompileInCall、BlockHashHost、NestedRevertWarm、EvmoneRefundSpike）改为链接 `bcos-evm-eth`，避免 `runExecutionFrame` 链接失败。
- `EthHost::call` delegates to `runExecutionFrame(Nested)`.
- `executeMessage` **unchanged** (temporary dual-track at tx entry only).
- Add `ExecutionFrameTest` per §8.1 PR1 gate (Nested equivalence + TopLevel characterization + parity matrix); CI green.
- `TxPipelineHooks::txRunEvmExecutionOverride` **unchanged** — still replaces entire `executeMessage()`; Frame is internal to default path only.
- Temporary helper duplication is allowed in PR1 when needed for TopLevel characterization; PR2 must delete the old copies from `ExecuteMessage.cpp` and leave Frame as the single implementation.

### PR2 — executeMessage migration

- `executeMessage` becomes thin tx adapter per §5.1.
- Delete dead code from `ExecuteMessage.cpp` / `EthHost.cpp`.
- Simplify parity test helpers.
- **PR2 gate：** `routeMessage(TopLevel)` 须与现状 `resolveCodeAddress` + precompile/7702 路径 parity（§4.1 ① 注）；**`resolveExecutionCode(TopLevel)` 须与现状 `resolveExecutableCode` / Nested `resolveExecutionCode` parity**（CREATE initcode、7702 delegation、empty precompile shortcut）；TopLevel CREATE 须保持 RR7 顺序。

### PR3 — Cleanup + docs

- Update `bcos-evm/docs/architecture-overview.md` (frame layer diagram).
- Optional ADR-021 or §追加至 ADR-019 related docs.
- Remove redundant `runDepth0`/`runDepth1` if fully superseded.

After PR2, **no frame-level dual-track** remains.

---

## 10. Out of scope

- ~~`ActivePrecompileSet` — warm/dispatch 单源~~ **Done**（`070434886`：`PrecompileActive.h` 单源 + `warmTransactionEntry(revisionConfig)`；Frame ③ 直接受益，无需再改 Router）
- `FiscoAddressDerivation` — 顶层/嵌套 CREATE 地址统一（候选 6）
- `OrchestrationErrorPolicy` — 三链 error taxonomy 完整落地（候选 4）；**注：** `eth/orchestration/OrchestrationErrorPolicy.h` 抽象已存在，`runTxPipeline` 已接入，链实现仍分散
- `Transition.cpp` bypass — 标注/deprecate（候选 8）；**注：** `eth/state/Transition.cpp` vector 路径仍直接调 `executeMessage()`，非帧级第三入口，但 PR2 后仍绕过 `runTxPipeline`
- `PrecompileRouterCharacterization` C7 depth0/depth1 非对称（chain hook vs vm）— **保留**，非 Frame parity 目标
- specs-tests production path alignment
- Inline `PrecompileRouter` into Frame.cpp（future optional）

---

## 11. Success criteria

1. `executeMessage` 与 `EthHost::call` 均 delegate 至 `runExecutionFrame`；**帧级**逻辑无第三入口（`Transition.cpp` tx 级 bypass 除外，见 §10）。
2. `PrecompileRouterEnvelopeTest` parity cases pass via `ExecutionFrameTest` without `runDepth0`/`runDepth1`.
3. 全量 eth unit + FISCO/OpStack smoke 无回归。
4. `eth/execution/` 无 `bcos/` / `opstack/` include。
5. Frame 相关 bug fix 只需改一处 implementation。

---

## 12. Grilling resolutions (2026-06-24)

| # | Question | Resolution |
| --- | --- | --- |
| Q1 | `EthHost&` vs abstract host interface | **A** — `EthHost&` + forward declare；接受 kernel 内 concrete coupling |
| Q2 | `FrameScope` vs `message.depth` | **A** — call site 决定 scope；Frame 不用 `depth` 驱动行为 |
| Q3 | PR1 是否允许 `executeMessage` 暂不迁移 | **A** — PR1 只迁 EthHost；PR2 迁 executeMessage；PR1 必须带 parity 测试 |
| Q4 | Step ③ precompile 7702 guard | **A** — 统一 EthHost 较严 guard（§4.2） |
| Q5 | CREATE sender nonce bump | **A** — Nested CREATE **attempt**（depth>0，含 failure）；TopLevel sender nonce 由 tx adapter success 路径处理 |
| Q6 | PrecompileRouter envelope refactor | **A** — 不动 Router；仅统一 call site |
| Q7 | PR1 `ExecutionFrameTest` 范围 | **A** — Nested 等价 + TopLevel characterization（§8.1） |
| Q8 | `txRunEvmExecutionOverride` hook | **A** — 不变；override 替换整个 executeMessage |

Review pass resolutions:

| # | Question | Resolution |
| --- | --- | --- |
| R1 | Frame 访问 `EthHost` private state | **A** — `EthHost` 提供窄 `execution_address_ref()` accessor；不使用 friend |
| R2 | `FrameContext` 是否持有完整 `evmc_tx_context` | **A** — 只传 `txOrigin` |
| R3 | `runExecutionFrame` host 参数形状 | **A** — 只传一个 `EthHost& host` |
| R4 | `createdInTx` 是否进 `FrameContext` | **A** — 不进；Frame 调 `host.markCreatedInTx(addr)` |
| R5 | `ExecutionAddressGuard` 所在位置 | **A** — 留在 `EthHost::call` adapter |
| R6 | TopLevel precompile hit tx finalize | **A** — 保持 early-return，不执行 `finalize_self_destructs()` |
| R7 | `stateDiff/logs` owner | **A** — 继续由 `executeMessage` adapter 负责 |
| R8 | PR1 helper duplication | **A** — 允许短期复制，PR2 必须删除旧份 |
| R9 | Review 决议是否写回 spec | **A** — 立即合并到本 spec |

Second review pass resolutions (spec + plan):

| # | Question | Resolution |
| --- | --- | --- |
| RR1 | TopLevel `routeMessage` 是否做 CREATE recipient/warm-pin | **A** — scope 分支：code_address 解析 + precompile target 标记 both scope；CREATE recipient 互填 + warm-pin 仅 Nested（§4.1） |
| RR2 | PR1 是否删 `EthHost::routeCall/resolveExecutionCode/transferValue` | **A** — PR1 移入 Frame 内部实现并删除旧 EthHost 版本（dead code 不留） |
| RR3 | Nested 等价 oracle | **A** — 复用既有 nested 测试套件全绿 + `ExecutionFrameTest`，不造新 golden snapshot |
| RR4 | Nested precompile `gasRefund` 不对称 | **A** — 冻结当前行为（Nested 忽略 `fr.gasRefund`），修正另开 PR |
| RR5 | 本轮决议写回 spec | **A** — 立即合并到本 spec |
| RR6 | Nested ②/③ 执行序（spec 排 ②前③后，现状 ③先②后） | **A** — PR1 冻结现状顺序；spec 编号仅为逻辑分组；执行序合一延后独立 PR（见 §4 注） |
| RR7 | TopLevel/Nested CREATE ④/⑤ 顺序相反 | **A** — 逐 scope 冻结（TopLevel：⑤→④；Nested：④→⑤）；PR1/PR2 不得统一；见 §4 注 |

Third review pass resolutions (2026-06-24):

| # | Question | Resolution |
| --- | --- | --- |
| RR8 | TopLevel ① 是否等价于完整 `routeCall` | **A** — 否；PR2 前现状用 `resolveCodeAddress`；PR2 gate 须 parity 验证（§4.1 ① 注） |
| RR9 | Nested sender nonce 范围 | **A** — `isCreateKind && depth>0` 即 bump（**不依赖** success）；非全部 nested 帧（§4.1） |
| RR10 | 本轮审查决议写回 spec | **A** — 立即合并到本 spec |

Fourth review pass resolutions (subagent merge, 2026-06-24):

| # | Question | Resolution |
| --- | --- | --- |
| R10 | `resolveCallerAddress` 迁移 | **A** — 迁入 `eth/execution/FrameCaller.h`；PR1 删 EthHost private 版本 |
| R11 | StateTests 内联 EthHost 链接 | **A** — PR1 改 `StateTests.cmake` 6 target 链接 `bcos-evm-eth` |
| R12 | 7702 frame helpers 共享 | **A** — `Delegation7702Frame.h` 供 RouteMessage + ExecutionFrame 共用 |

Prior decisions:

| Question | Resolution |
| --- | --- |
| 方案 A/B/C | **A** — ExecutionFrame deep module |
| PrecompileRouter 去留 | 保留；Frame 为唯一 call site |
| TopLevel CREATE 地址 | Skip Frame ②；FISCO 由 `txSetupMessage`→`deriveMessage()` 决定；ETH ref 由 caller 预填 |
| finalize_self_destructs | tx-level in executeMessage after Frame returns |
| Phased vs big-bang | 3 PR phased migration |

No blocking TBDs for PR1; apply R10/R11 before implementation.

---

## 13. Code review notes (2026-06-24)

### Third pass — subagent merge (spec + plan)

| Finding | Severity | Status |
| --- | --- | --- |
| Nested CREATE nonce：attempt 即 bump，非 success-only | P1 | ✅ §4.1 / Q5 / RR9 已修正 |
| `resolveCallerAddress` 迁移未定 | P1 | ✅ R10 + §5.2 |
| PR2 缺 `resolveExecutionCode` parity gate | P1 | ✅ §9 PR2 |
| StateTests.cmake 内联 EthHost 链接 | P0 | ✅ §9 PR1 + plan Task 5 |
| C7 characterization 非对称 | P2 | ✅ §10 |
| Smoke ctest 名称 | P2 | ✅ §8.3 |

### First pass

| Area | Verdict | Notes |
| --- | --- | --- |
| 双轨问题描述（§1） | ✅ 仍准确 | 两处 `dispatchPrecompile`；`runDepth0`/`runDepth1` 仍在 |
| 9 步 pipeline + RR6 顺序 | ✅ 仍准确 | `EthHost::call` ③→② 与 spec 注一致 |
| §4.2 unified precompile guard | ✅ 已对齐 | Nested guard 已在 `EthHost.cpp:297-298`；TopLevel 仅 empty-code 路径 |
| §4.3 7702 tx-level auth | ✅ 仍准确 | `ExecuteMessage.cpp:208-222` 在 Frame 调用前 |
| `warmTransactionEntry` | ✅ 已更新 | 首参含 `revisionConfig`；与 ActivePrecompileSet 单源一致 |
| 术语（orchestration/HostExtension） | ✅ 已修正 | 见文首 Terminology alignment |
| `execution_address_ref()` | ⏳ 待 PR1 | spec 规划 API；当前仅有 `set_execution_address()` |
| ActivePrecompileSet | ✅ 已完成 | 移出 §10 blocking scope |
| 已知测试债务 | ℹ️ 非 Frame | `ExecuteMessageSmoke` / `FiscoVmHostPolicy` / `DepositCreateNonce` nonce 失败与 Frame 无关 |

### Second pass — gaps fixed in this revision

| Gap | Severity | Resolution in spec |
| --- | --- | --- |
| TopLevel/Nested CREATE ④/⑤ 顺序相反 | P0 | RR7 + §4.1 行 + §8.1 测试行 |
| §4.1 sender nonce 过宽 | P1 | ✅ 已修正为 CREATE attempt（含 failure） |
| TopLevel 未走完整 `routeCall` | P1 | §4.1 ① 注 + RR8 + PR2 gate |
| §12 Prior decisions 术语过时 | P2 | 更新 TopLevel CREATE 行 |
| §7 `mapException` 过时 | P2 | 改为 `onPipelineException` |
| §3.1 未列已有 execution/ 文件 | P2 | 补充 WarmTransactionEntry 等 |
| `executeMessage(depth>0)` 边界 | P2 | §3.2 Invariants 注明 |
| `Transition.cpp` bypass 边界 | P2 | §10 + §11 澄清 |

**Spec 可直接用于 PR1 实现计划**；实现前建议 invoke `writing-plans` 生成逐步 checklist。

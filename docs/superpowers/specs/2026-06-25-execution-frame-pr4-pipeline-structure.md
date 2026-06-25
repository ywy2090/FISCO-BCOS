# ExecutionFrame PR4 — Pipeline 结构重组（内部双轨合并）

**Status:** Implemented  
**Date:** 2026-06-25  
**Implementation:** `ExecutionFrame.cpp` — named pipeline steps + `runTopLevelSteps` / `runNestedSteps`  
**Depends on:** PR1–PR2（`2026-06-24-execution-frame-design.md`，Implementation status: Done）  
**Related:** ADR-005, `bcos-evm/docs/architecture-overview.md` §3.1, `bcos-evm/docs/architecture-review-post-orchestration-2026-06-23.md` 候选 1（adapter 双轨 ✅；内部 implementation 双轨 ⏳）

---

## 1. Problem

PR1–PR2 已将 **adapter 双轨**（`executeMessage` vs `EthHost::call`）收敛到单一 `runExecutionFrame()`。但 `ExecutionFrame.cpp` 内部仍维护两条 **implementation 路径**：

| 路径 | 函数 | 行数（约） |
| --- | --- | --- |
| TopLevel | `runTopLevelExecutionFrame()` | ~110 |
| Nested | `runExecutionFrame()` Nested 分支 inline | ~130 |

两条路径重复实现同一批帧语义（precompile dispatch、checkpoint/transfer、CREATE finalize、vm execute），仅在 **执行序**（RR6/RR7）与 **finalize 归属**（commit/nonce）上按 scope 分叉。

**Deletion test 部分失败：** 删除 `runTopLevelExecutionFrame` 后，TopLevel 逻辑不会消失——它仍完整存在于 Nested 分支的镜像副本中；复杂度未 concentrate。

**Locality 问题：** 修 precompile hit、CREATE deposit gas、insufficient balance 等 bug 时，须人工核对两条路径是否同步。`ExecutionFrameTest` 的 TopLevel/Nested parity case 即为此风险的显式 oracle。

---

## 2. Goals / Non-Goals

### 2.1 Goals

- **结构合并：** 删除 `runTopLevelExecutionFrame`；单一 `runExecutionFrame` 按 `FrameScope` 分发到两个 thin sequence 函数。
- **命名 step 函数：** 在 `ExecutionFrame.cpp` anonymous namespace 内抽出 spec §4 九步对应的 step 实现；已有 helper（`routeMessage`、`transferFrameValue`、`resolveExecutionCode`）直接复用。
- **显式执行序：** `runTopLevelSteps()` / `runNestedSteps()` 各自以可读调用序表达 RR6/RR7 冻结差异。
- **Early-return 协议：** 可 early-return 的 step 返回 `std::optional<FrameResult>`；`std::nullopt` = 继续 pipeline。
- **单一 `finalizeFrame(scope, …)`：** vm 后处理共用；commit/nonce/executionAddress 差异在 step ⑨ 内部 scope 分支。
- **零行为变更：** 现有 `ExecutionFrameTest` + `PrecompileRouter*Test` 全绿即 merge gate。

### 2.2 Non-Goals

- **语义合一（RR6/RR7 取消）：** 不统一 TopLevel/Nested 的 CREATE ④/⑤ 顺序、不调整 Nested precompile→prepareMessage 顺序；需新 ADR + EEST/GST 全量回归，另开 PR。
- **Public interface 变更：** `ExecutionFrame.h` 类型签名不变；adapter（`executeMessage` / `EthHost::call`）不变。
- **新增 `.cpp` 文件 per step：** step 函数留在 `ExecutionFrame.cpp` anonymous namespace（非 orchestration 式一 step 一文件）。
- **PrecompileRouter envelope 顺序变更**（checkpoint→transfer）：属 architecture-review 候选 3，不在本 PR。
- **FiscoAddressDerivation / OrchestrationErrorPolicy / ActivePrecompileSet：** 均不在 scope（前两者 out of scope；后者已 Done）。

---

## 3. Grilling 决策摘要（2026-06-25）

| # | 问题 | 选择 |
| --- | --- | --- |
| 1 | 合并边界 | **A** — 结构合并；保留 RR6/RR7 冻结语义 |
| 2 | step 组织 | **A** — anonymous namespace 命名 step 函数 |
| 3 | 执行序表达 | **A** — `runTopLevelSteps` / `runNestedSteps` 两个 thin sequence |
| 4 | 回归 gate | **A** — 现有测试套件全绿；失败时才补覆盖 |
| 5 | 步骤 ⑨ 组织 | **A** — 单一 `finalizeFrame(scope, …)` + 内部 scope 分支 |
| 6 | early-return 协议 | **A** — step 返回 `std::optional<FrameResult>` |

---

## 4. Architecture

### 4.1 Module 分层（PR4 目标）

```text
runExecutionFrame(scope)                         ← public interface（不变）
  ├─ runTopLevelSteps()                          ← thin sequence（~20 行）
  └─ runNestedSteps()                            ← thin sequence（~25 行）
        ↑ 调用共享 step 函数（anonymous namespace）
          routeMessage          （已有 RouteMessage.cpp）
          guardDelegatePrecompile
          tryPrecompile
          prepareNestedMessage    （Nested only：setCallerAddress + prepareMessage）
          bindCreateForInit       （bindCreateMessageForInit 包装）
          checkpointFrame
          initializeCreateAccount
          transferFrameValue      （已有 FrameValueTransfer.h）
          runVm                    （resolveExecutionCode + vm.execute）
          finalizeFrame(scope)    （commit/nonce 差异内聚）
```

**Seam 纪律：** 改动限于 `bcos-evm/eth/execution/ExecutionFrame.cpp`；不 `#include` `bcos/` / `opstack/`；不修改 `eth/orchestration/`。

### 4.2 Early-return 协议

```cpp
// 可 early-return 的 step
std::optional<FrameResult> tryPrecompile(...);
std::optional<FrameResult> guardDelegatePrecompile(...);
std::optional<FrameResult> transferOrFail(...);  // insufficient balance

// sequence 模式
if (auto early = guardDelegatePrecompile(...)) return *early;
if (auto early = tryPrecompile(...)) return *early;
// ...
return finalizeFrame(scope, ...);  // 主路径终点，始终返回 FrameResult
```

### 4.3 TopLevel sequence（冻结，RR6/RR7/RR8）

```text
runTopLevelSteps(ctx, message, host):
  routed = routeMessage(TopLevel)
  if guardDelegatePrecompile → return
  code = resolveExecutionCode(...)                    // TopLevel：resolve 先于 precompile
  if empty code && !CREATE && !7702-delegated:
      if tryPrecompile(routed.precompileTarget) → return
  checkpointFrame()
  if CREATE:
      if transferOrFail(TopLevel) → return            // endowment
      bindCreateForInit + initializeCreateAccount     // RR7：⑤ 先于 ④
  else:
      if transferOrFail(TopLevel) → return
  result = runVm(code)
  return finalizeFrame(TopLevel, ...)                 // 不 commit；fixNonceInit 在 scope 内
```

> **RR7 注：** TopLevel CREATE 顺序为 checkpoint → transfer(endowment) → bindCreate → initAccount → vm。`bindCreateMessageForInit` 无 state mutation，但顺序影响 revert 边界，须冻结。

> **RR8 注：** TopLevel precompile target 优先使用 `routed.precompileTarget`（`routeMessage` 标记）；fallback 为 code_address/recipient。

### 4.4 Nested sequence（冻结，RR6）

```text
runNestedSteps(ctx, message, host):
  routed = routeMessage(Nested)
  if guardDelegatePrecompile → return
  if tryPrecompile(code_address/recipient target) → return   // RR6：③ 先于 ②
  prepareNestedMessage()                                      // setCallerAddress + prepareMessage
  if CREATE: bindCreateForInit                                 // RR7：④ 先于 ⑤
  checkpointFrame()
  if CREATE: initializeCreateAccount
  if transferOrFail(Nested) → return
  code = resolveExecutionCode(...)
  result = runVm(code)
  fr = finalizeFrame(Nested, ...)                             // success 时 commit
  if CREATE attempt: bump sender nonce (+ bumpContractCreateNonce)
  return fr
```

> **RR6 注：** Nested ③（tryPrecompile）先于 ②（prepareMessage），因 `prepareMessage` 会 mutate `callMessage` 而 precompile target 须在其之前求值。

### 4.5 `finalizeFrame(scope, …)` — 步骤 ⑨ scope 分支

| 行为 | TopLevel | Nested |
| --- | --- | --- |
| codeDepositGas + installCode | Run | Run |
| markCreatedInTx | Run | Run |
| CREATE address 解析 | `resolveCreateAddress()` | `callMessage.recipient` |
| failure → revert | Run | Run |
| success → commit | **Skip**（adapter 负责） | **Run** |
| fixNonceInit | Run when `ctx.fixNonceInit` | Skip |
| executionAddress 更新 | Skip | 非 CREATE success 时 Run |
| sender nonce bump | Skip（adapter 负责） | CREATE attempt 时 Run（不依赖 success） |
| bumpContractCreateNonce | Skip | CREATE attempt 且 sender ≠ tx_origin |

Nested 的 sender nonce bump / bumpContractCreateNonce **保留在 sequence 末尾**（`finalizeFrame` 返回之后），与现状 `ExecutionFrame.cpp:294-303` 一致；或内聚进 `finalizeFrame(Nested)` 末尾——实现时二选一，**gate 测试必须通过**。

---

## 5. Step 函数清单

| Step | 函数名（建议） | 文件 | Early-return |
| --- | --- | --- | --- |
| ① | `routeMessage` | `RouteMessage.cpp` | — |
| ② | `prepareNestedMessage` | ExecutionFrame.cpp | — |
| guard | `guardDelegatePrecompile` | ExecutionFrame.cpp | optional |
| ③ | `tryPrecompile` | ExecutionFrame.cpp | optional |
| ④ | `bindCreateForInit` | ExecutionFrame.cpp（包装 `bindCreateMessageForInit`） | — |
| ⑤ | `checkpointFrame` | ExecutionFrame.cpp | — |
| ⑥ | `initializeCreateAccount` | ExecutionFrame.cpp（包装 `initializeCreateTargetAccount`） | — |
| ⑦ | `transferOrFail` | ExecutionFrame.cpp（包装 `transferFrameValue`） | optional |
| ⑧ | `runVm` | ExecutionFrame.cpp | — |
| ⑨ | `finalizeFrame` | ExecutionFrame.cpp | — |

**共享 frame state（sequence 内传递）：**

```cpp
struct FrameWork {
    FrameContext& ctx;
    evmc_message const& originalMsg;
    RoutedMessage routed;
    evmc::bytes code;           // TopLevel：resolve 后填充；Nested：runVm 前填充
    state::EthHost& host;
};
```

`FrameWork` 仅存在于 anonymous namespace；不进入 public header。

---

## 6. 与父 spec 的关系

| 父 spec 决议 | PR4 处理 |
| --- | --- |
| RR6 Nested ③ 先于 ② | **保留** — `runNestedSteps` 显式顺序 |
| RR7 TopLevel/Nested CREATE ④/⑤ 相反 | **保留** — 两个 sequence 分别表达 |
| RR4 Nested 忽略 `fr.gasRefund` | **保留** — adapter 行为不变 |
| Q5 CREATE sender nonce | **保留** — TopLevel adapter / Nested sequence 分工不变 |
| §9 PR3「Cleanup + docs」 | PR4 **替代** PR3 的 production refactor 部分；PR3 文档 hygiene 可合并进 PR4 |

父 spec §9 追加：

```text
### PR4 — Pipeline 结构重组（internal dual-track merge）
- 重构 ExecutionFrame.cpp：命名 step + 两个 sequence；删除 runTopLevelExecutionFrame。
- 零 public interface / 零 adapter / 零语义变更。
- Gate：§7 测试矩阵全绿。
```

---

## 7. Test gate

### 7.1 必跑（PR4 merge blocker）

| Target | 验证点 |
| --- | --- |
| `ExecutionFrameTest` | TopLevel/Nested parity；RR7 CREATE order；TopLevel 不 commit；precompileHit |
| `PrecompileRouterEnvelopeTest` | depth0/depth1 envelope |
| `PrecompileRouterCharacterizationTest` |  characterization oracle |
| `PrecompileRouterEquivalenceTest` | Router 等价 |

### 7.2 建议跑（smoke）

| Target | 说明 |
| --- | --- |
| `RouteMessageTest` | routeMessage 未改 public 行为 |
| `ResolveExecutionCodeTest` | resolve 路径未改 |
| `FrameValueTransferTest` | transfer scope 分支未改 |

### 7.3 不新增测试（除非 gate 失败）

现有 characterization test 已是 semantic oracle；PR4 不预先添加 step 级单测。

---

## 8. Implementation plan

### Task 1 — Baseline gate

- [ ] 构建并运行 §7.1 全部 target；记录 baseline 全绿。

### Task 2 — 引入 `FrameWork` + step 函数骨架

- [ ] 在 `ExecutionFrame.cpp` anonymous namespace 添加 `FrameWork`、step 函数声明。
- [ ] 从现有 `runTopLevelExecutionFrame` / Nested 分支 **逐块剪切** 到对应 step（行为不变）。

### Task 3 — 实现 `runTopLevelSteps` / `runNestedSteps`

- [ ] 按 §4.3 / §4.4 组装 sequence。
- [ ] 删除 `runTopLevelExecutionFrame`。
- [ ] `runExecutionFrame` 仅保留 scope dispatch。

### Task 4 — Gate + 清理

- [ ] §7.1 全绿。
- [ ] 确认 `ExecutionFrame.cpp` 无 duplicated vm/create/finalize 块。
- [ ] 更新 `2026-06-24-execution-frame-design.md` §9 追加 PR4 条目。

### Task 5 — 文档（可选，可与 Task 4 合并）

- [ ] `architecture-overview.md` §3.1 补一句：「PR4：Frame 内部九步 pipeline 结构重组，adapter 双轨已在 PR1–2 闭合。」

---

## 9. Success criteria

1. `runTopLevelExecutionFrame` **已删除**；`ExecutionFrame.cpp` 仅一个 `runExecutionFrame` 对外路径 + 两个 internal sequence。
2. §7.1 测试矩阵 **零回归**。
3. `ExecutionFrame.h` **无 diff**（public interface 不变）。
4. `executeMessage.cpp` / `EthHost.cpp` **无 diff**（adapter 不变）。
5. RR6/RR7 执行序在 `runTopLevelSteps` / `runNestedSteps` 中 **可一眼核对**。
6. **Deletion test 通过：** 删除任一 step 函数，复杂度 reappear 在 sequence 中（而非另一条平行路径）。

---

## 10. Out of scope

- RR6/RR7 **语义合一**（对齐 geth 金标准执行序）
- `PrecompileRouter` checkpoint→transfer envelope 重构
- `FiscoAddressDerivation` 统一
- `OrchestrationErrorPolicy` 三链 error taxonomy
- step 级独立单测文件
- `Transition.cpp` tx-level bypass

---

## 11. Risks

| 风险 | 缓解 |
| --- | --- |
| 剪切时 subtly 改变 TopLevel resolve→precompile 顺序 | §4.3 顺序表 + `ExecutionFrameTest` top_level_* cases |
| CREATE RR7 顺序在 refactor 中被统一 | sequence 函数注释 + `top_level_create_checkpoint_before_bind_order` |
| Nested nonce bump 位置漂移 | `ExecutionFrameTest` nested cases + 对照 `ExecutionFrame.cpp:294-303` |
| `finalizeFrame` scope 分支遗漏 fixNonceInit | `ExecutionFrameTest` + FISCO fixNonceInit 路径 smoke |

---

## 12. Grilling resolutions (2026-06-25)

| # | Question | Resolution |
| --- | --- | --- |
| Q1 | 合并边界：结构 vs 语义 | **A** — 结构合并；RR6/RR7 冻结 |
| Q2 | step 组织方式 | **A** — anonymous namespace 命名 step |
| Q3 | 两个 scope 执行序表达 | **A** — `runTopLevelSteps` / `runNestedSteps` |
| Q4 | 回归 gate | **A** — 现有测试全绿 |
| Q5 | 步骤 ⑨ 组织 | **A** — 单一 `finalizeFrame(scope)` |
| Q6 | early-return 协议 | **A** — `std::optional<FrameResult>` |

No blocking TBDs for implementation.

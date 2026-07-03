# bcos-evm 架构审查 — 编排迁移后

**日期:** 2026-06-23  
**分支:** feat-evm-refactor  
**方法:** improve-codebase-architecture（codebase-design 词汇：module / interface / depth / seam / adapter / leverage / locality）  
**配套:** [architecture-overview.md](architecture-overview.md) · [architecture-known-gaps.md](architecture-known-gaps.md) · [2026-06-23-orchestration-pipeline-design.md](../../docs/superpowers/specs/2026-06-23-orchestration-pipeline-design.md)

---

## 1. 已完成：三链编排收敛

三条 `executeVia*` 均已迁入 `runTxPipeline`（ADR-019，见 `.superpowers/sdd/orch-progress.md` Task 1A–4 complete）。

| 项 | 状态 |
| --- | --- |
| `runTxPipeline` 固定 12 步管线 | ✅ |
| `deductIntrinsicGas` / `adoptEvmcResult` / `buildExecuteMessageInput` 单点 | ✅ |
| OpStack intrinsic 与 `ctx.message` 同步 | ✅ 结构性修复 |
| 三份 `adoptResult` 匿名副本 | ✅ 合并为 `AdoptEvmcResult.h` |
| `ExecuteMessageInput` 13 字段三处手工填充 | ✅ 合并为 `BuildExecuteMessageInput.h` |

### 1.1 迁移前的问题（归档）

**浅 module + locality 失效：** 三条链各自 inline ~150–400 行编排，同一不变量无单点 enforcement。

| 重复块 | Eth | Fisco | OpStack（迁移前） |
| --- | --- | --- | --- |
| null 校验 + `State` 构建 + `txProps` | ✅ | ✅ | ✅ |
| `ExecuteMessageInput` 13 字段逐字段填充 | ✅ | ✅ | ✅×2 |
| `adoptResult` 匿名副本 | ✅ | ✅ | ✅ |
| EIP-7623 intrinsic 预扣 | `message.gas -=` | `message.gas -=` | `txData.m_message.gas -=` |
| 传入 `executeMessage` 的 message | 已扣减 | 已扣减 | **`input.message`（未扣减）** |

**OpStack 正确性缺陷：** `executeEntryChecks` 在 `txData.m_message` 上扣 intrinsic，但 `executeMessage` 传入 `input.message`——进内核的 gas 与扣减对象分裂。修复方式：`TxPipelineContext::message` 作为唯一可变 message owner；回归测试 `test/opstack/OpStackIntrinsicGasSyncTest.cpp`。

**收敛后形态：**

```text
executeViaEth    ──hooks──► runTxPipeline ──► executeMessage
executeViaHost   ──hooks──► runTxPipeline ──► executeMessage
opStackExecute   ──hooks──► runTxPipeline ──► executeMessage
                              ↑ ctx.message 唯一所有权
```

---

## 2. 下一杠杆：8 个加深候选

编排收敛完成后，最高 friction 转入**内核帧语义**、**Revision 消费一致性**、**错误处理 seam** 与 **Port 迁移收尾**。

### 优先级总览

```text
P0  Strong — 内核帧 / precompile（2026-06-25 已闭合）
  ├─ 1. ExecutionFrame 统一 executeMessage + EthHost::call     ✅ Done (PR1–4)
  ├─ 2. ActivePrecompileSet 统一 warm + dispatch               ✅ Done
  ├─ 3. PrecompileRouter checkpoint 信封                       ✅ Done
  └─ 4. StateTransitionErrorPolicy                               ✅ Done（三链 adapter + 对称测试）

P1  Worth exploring
  ├─ 5. AuthPort 全生命周期
  ├─ 6. FiscoAddressDerivation 单 module                       ✅ Done (ADR-022 PR-A)
  └─ 7. OpStackSettlementContext                               ⚠️ bcos-evm 内 largely closed (ADR-021/023)

P2  Speculative → Done
  └─ 8. Typed OrchestrationProfiles                            ✅ Done
```

---

### 候选 1 — ExecutionFrame：统一 executeMessage 与 EthHost::call

**Status:** ✅ Done (ExecutionFrame PR1–4, `ea1e4f2dc`..`063366771`). See `architecture-overview.md` §3.1.

**强度:** Strong · **类别:** in-process

**文件:** `eth/ExecuteMessage.cpp` · `eth/state/EthHost.cpp` · `eth/precompiled/PrecompileRouter.cpp` · `test/eth/PrecompileRouterEnvelopeTest.cpp`

**问题（无 locality）：** 同一「EVM 帧」语义拆成两条实现路径：

- **depth=0：** `executeMessage` 直接 warm → 空 code 时 `dispatchPrecompile` → checkpoint → value transfer → CREATE → `vm->execute`
- **depth>0：** evmone 回调 `EthHost::call` → `routeCall` → 再次 `dispatchPrecompile` → 另一套 value / CREATE / checkpoint

`PrecompileRouterEnvelopeTest` 分别构造 `runDepth0` / `runDepth1` 手工对齐，说明团队已知两条路径需同步——编排收敛**未消除**此内核 friction。

**方案:** 抽出 `ExecutionFrame` module（warm → precompile route → checkpoint → value → execute → finalize）；`executeMessage` 顶层与 `EthHost::call` 嵌套共用同一 implementation；PrecompileRouter 只被一处调用。

**收益:**

- locality：帧语义 bug 集中在一处
- leverage：修一次，所有 depth 受益
- interface 即 test surface

**ADR:** 与 ADR-005 不冲突；ADR-019 未触及此层。

---

### 候选 2 — ActivePrecompileSet：warm 与 dispatch 单源

**Status:** ✅ Done (`070434886` 单源 rollout；`e1106638c` `Eip2929Access` TE gate)。`Eip2929PrecompileWarm.h` 已删除。

**强度:** Strong · **类别:** in-process

**文件:** `eth/precompiled/PrecompileActive.h` · `eth/kernel/execution/PrepareState.h` · `eth/precompiled/PrecompileRouter.cpp` · `eth/kernel/execution/RouteMessage.cpp` · `bcos/FiscoPolicy.h` · `test/eth/EipPrecompileRevisionGateTest.cpp` · `test/bcos/BcosPrecompileRevisionGateTest.cpp`

**问题（已修复，归档）：** ADR-018 要求 gated EIP 消费 `RevisionConfig` bool；dispatch 已遵守（`PrecompileActive` 读 `cfg.eip2537`），但 tx-entry warm 曾按 `evmc_revision >= PRAGUE/OSAKA` 硬编码。FISCO 场景 `revision=PRAGUE` + `eip2537=false` 时，0x0b–0x11 曾被 warm 却不会 dispatch——与 geth `ActivePrecompiles(rules)` 分叉。

**落地:** `isActivePrecompile` / `forEachActivePrecompile` 为唯一集合；`PrepareState` warm 与 `PrecompileRouter` / `RouteMessage` dispatch 共用。FISCO `FISCO_GATED_FLAG_MAP` 一次 mask `eip2537` / `eip2929`。

**测试:** `EipPrecompileRevisionGateTest`（含 `fisco_mask_bls_not_warmed_when_eip2537_off`）、`BcosPrecompileRevisionGateTest`、`PrecompileRouterCharacterizationTest` C6。

**ADR 张力（已文档化）:** ADR-004 Scheme A — `eip2929` 由 `Eip2929Gate.h` 消费；FISCO `feature_evm_eip2929=OFF` 为相对 geth 的有意偏离。Gap 37 台账见 `architecture-known-gaps.md`（`eip2929` / `eip3651` 已闭合；`eip1559` 仍 profile-only；`prague_post_execution` 已删除）。

**可选后续:** `fiscoExecute(PRAGUE, eip2537=false)` + CALL 0x0b 端到端 gas characterization（warm 单测已覆盖）。

---

### 候选 3 — PrecompileRouter envelope：checkpoint 先于 transfer

**Status:** ✅ Done。`PrecompileRouter.cpp` 顺序为 `checkpoint → transfer → dispatch`；`finalizeEnvelope` 失败 `revert`。`PrecompileRouterEnvelopeTest` 含转账后 precompile 失败余额回滚（`3797aceca`）。

**强度:** Strong · **类别:** in-process

**文件:** `eth/precompiled/PrecompileRouter.cpp` · `test/eth/PrecompileRouterEnvelopeTest.cpp` · [error-handling-parity-design](../../docs/superpowers/specs/2026-06-23-eth-evm-error-handling-parity-design.md) §1.1 P0

**问题（已修复，归档）：** 错误顺序 transfer → checkpoint → dispatch 时，precompile 失败无法撤销已转 value，影响 stateRoot。

**落地:** envelope 在 `dispatchPrecompile` 内；`ExecutionFrame` precompile 命中路径委托 Router，不在 frame 层重复 transfer。

**可选后续:** 将 `finalizeEnvelope` 具名化为独立 `PrecompileEnvelope` module（locality，非行为变更）；chain precompile 失败 + value transfer 测试。

---

### 候选 4 — StateTransitionErrorPolicy：跨链错误 typed seam

**Status:** ✅ Done (`58bfb9db6` policy 抽取；`738f30e8b` 三链 `OrchestrationProfile::bind`；`1ec290a81` 对称单测）。

**强度:** Strong · **类别:** in-process

**文件:** `eth/ExecuteViaEth.cpp` · `eth/kernel/state-transition/IncludedTxVmerrNormalize.h` · `bcos/ExecuteViaHost.cpp` · `opstack/OpStackExecuteViaHost.cpp` · ADR-015

**问题（无 locality）：** 管线共享 gas 数学，但错误语义仍分散在三个 wrapper 的 `mapException` / `postAdopt` / `postSettle` lambda（各 ~100–180 行）。Eth 有 included-tx vmerr；Fisco 有 `fixErrorHandling` 矩阵；OpStack 用 `postExecuteGasSettlement`。理解「同一 `EVMC_REVERT` 在三链含义」需读三份 cpp。

**方案:** 引入 `StateTransitionErrorPolicy` interface；Eth / Fisco / Op 各一个 adapter；`runTxPipeline` 只调用 policy 方法。内核修复（候选 3）自动惠及三链。

**收益:**

- interface 即 test surface（每链 error taxonomy 单测）
- ADR-015 边界保留在 Eth adapter

**ADR:** 不可把 ADR-015 normalize 无差别套到 BCOS/OP，除非新 ADR 明确产品决策。

---

### 候选 5 — AuthPort 全生命周期

**强度:** Worth exploring · **类别:** ports & adapters

**文件:** `bcos/ports/AuthPort.h` · `bcos/ExecuteViaHost.cpp` · `bcos/FiscoHostExtension.cpp` · `bcos/FiscoPolicy.h`

**问题:** Auth 拆在编排 seam（`checkAuth`）与 HostExtension seam（`createAuthTable`）；`FiscoPolicy` 仍 `#include` executor adapter（`AuthCheck.h`），与 ADR-017「bcos-evm 零 executor include」不一致。TE 可能混用 `FiscoPolicy::checkAuth` 与 `AuthPort` 双 implementation。

**方案:** Auth 全生命周期收进 `AuthPort`（precheck + table creation）；`FiscoPolicy` 只产 `RevisionConfig`；executor 适配器单一实现 Port。

**ADR:** 与 ADR-017 方向一致；architecture-overview §7.3 已标注此张力。若 TE 仍混用两路径则升为 Strong。

---

### 候选 6 — FiscoAddressDerivation：合并 CREATE 地址逻辑

**Status:** ✅ Done (ADR-022 PR-A, `8a14a497f`).

**强度:** Worth exploring · **类别:** in-process

**文件:** `bcos/FiscoTxAdapter.h` · `bcos/ExecuteViaHost.cpp` · `bcos/FiscoPolicy.h` · `bcos/FiscoHostExtension.cpp`

**问题:** 顶层 CREATE 在 orchestration `prepareMessage`；嵌套 CREATE 在 `HostExtension::prepareMessage`；`FiscoPolicy::deriveMessageImpl` 几乎相同副本；`postAdopt` 还有 CREATE address 修补——理解 FISCO 合约地址需 bounce 4 个 module。

**方案:** 单一 `FiscoAddressDerivation` module（top-level vs nested frame）；Policy 与 HostExtension delegate；删除 `FiscoPolicy::deriveMessageImpl` 重复。

**ADR:** 与 ADR-005（CREATE nonce 在 HostExtension）部分重叠，需明确 top-level vs nested 边界文档。

---

### 候选 7 — OpStackSettlementContext：消除 txData 影子帧

**Status:** ⚠️ Partially closed in `bcos-evm`：`OpStackSettlement` + ADR-021；外圈 `runOpStackTxLifecycle`（ADR-023）取代 bridge 内联。TE 层 `txData` 是否仍双轨需单独审计。`feeCtx.m_evmcResult` 已移除（buyGas 失败写 `ctx.evmcResult`）；`OpStackTxExecutionData` 别名已删除。

**强度:** Worth exploring · **类别:** in-process

**文件:** `opstack/OpStackExecuteViaHost.cpp` · `opstack/OpStackPreDebitEntry.cpp` · `test/opstack/OpStackIntrinsicGasSyncTest.cpp` · `test/opstack/OpStackExecuteViaHostSmokeTest.cpp`

**问题:** intrinsic 已统一到 `ctx.message`，但外圈仍维护 20+ 字段的 `OpStackTxExecutionData` 影子帧（`m_gasLimit` 来自原始 input、`m_message` 仍拷贝）。`applySettlement` / `refundGas` / receipt meta 读 `txData` 而非 `ctx`——下一类 drift 会出在 settlement 环。ADR-019 Q14「移除 dual-track message」在 `txData.m_message` 上未完全落地。

**方案:** `OpStackSettlementContext` adapter 从 `TxPipelineContext` 只读投影 gas/settlement 字段；buyGas/refund 环只 mutates `ctx` + 少量 fee 侧车；补 normal 路径 `buyGas→runTxPipeline→refundGas→build_diff` 集成测试。

**ADR:** ADR-019 Q7/Q19 明确 fee 必须在 wrapper——不冲突。

---

### 候选 8 — Typed OrchestrationProfiles

**Status:** ✅ Done (`738f30e8b` Eth/Fisco/Op `OrchestrationProfile::bind`).

**强度:** Speculative → Worth exploring（在 P0 之后）· **类别:** in-process

**文件:** `eth/kernel/state-transition/TxPipelineHooks.h` · `eth/ExecuteViaEth.cpp` · `bcos/ExecuteViaHost.cpp` · `test/eth/TxPipelineTest.cpp`

**问题:** `runTxPipeline` 已是深 module，但链差异仍通过 10 个 default-noop `std::function` 注入；每个 wrapper 内联完整策略，无 `EthStateTransitionBindings` / `FiscoStateTransitionBindings` 具名 implementation。新增 hook 需改 3 个 cpp + 默认值 + 测试矩阵。

**方案:** 链侧提供 named profile struct；wrapper 仅 `runTxPipeline(ctx, EthProfile::hooks(input))`；或与候选 4（ErrorPolicy）合并。

**备注:** `eth-layer-design-review.md` §3.6 仍写「仅 Eth 用 pipeline」——文档 stale，应更新。

---

## 3. 测试覆盖缺口

| 生产路径 | 现有测试 | 缺口 |
| --- | --- | --- |
| `runTxPipeline` 步序 | `TxPipelineTest.cpp` | 无链 profile 快照 |
| `executeViaHost` 编排 hook | `BcosAuthOrchestratorHookTest`, smoke | 无 full Fisco orchestration matrix |
| OpStack normal 外圈 | `OpStackTxLifecycleCharacterizationTest`（7 cases） | Isthmus operator fee 边界可选 |
| PrecompileRouter | `PrecompileRouterEnvelopeTest`（3 cases，含失败回滚） | chain precompile 失败 + value 可选补 |
| Active precompile warm/dispatch | `EipPrecompileRevisionGateTest`, `BcosPrecompileRevisionGateTest` | FISCO `eip2537=false` 全路径 gas E2E 可选 |
| Eth included-vmerr | `EthIncludedTxVmerrTest.cpp` | Fisco/Op 无对称 error taxonomy 测试 |
| OpStack intrinsic sync | `OpStackIntrinsicGasSyncTest.cpp` | deposit 路径 only；normal 路径无 spy |

---

## 4. Top recommendation

**P0 内核候选（1–4）与 Typed Profiles（8）已闭合（2026-06-25）。**

**下一刀：候选 5（AuthPort 全生命周期）** — `FiscoPolicy` 仍 include TE adapter，与 ADR-017 张力；或 **候选 7 TE 层审计**（`txData` 影子帧是否在 executor 残留）。

**不建议再开:** 三路径编排收敛、ExecutionFrame 双轨、warm/dispatch 双轨、PrecompileRouter envelope 顺序。

**随后（合规）:** EEST smoke manifests、三链 orchestration profile 快照测试。

---

## 5. 词汇对照

| 术语 | 含义 |
| --- | --- |
| module | 有 interface 与 implementation 的单元 |
| depth | interface 背后 behaviour 的 leverage |
| seam | 可替换 behaviour 而不改调用方的位置 |
| adapter | 在 seam 上满足 interface 的具体实现 |
| locality | 变更/bug/知识集中在一处 |
| leverage | 一次 implementation 惠及 N 个 call site |
| shallow module | interface 复杂度 ≈ implementation |
| deep module | 小 interface + 大 implementation |

---

## 6. 变更记录

| 日期 | 说明 |
| --- | --- |
| 2026-06-25 | 候选 1–4、6、8 标 Done；候选 2/3 测试与 commit 引用；候选 7 标 bcos-evm partial；更新 Top recommendation |
| 2026-06-23 | 初版：编排迁移后架构审查（subagent explore + improve-codebase-architecture） |

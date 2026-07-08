# OrchestrationProfile — 具名链编排策略 deep module

**Status:** Accepted (post-grilling)  
**Date:** 2026-06-24  
**Supersedes:** `2026-06-24-evm-unified-executor-api-design.md`（公开 facade 方案已否决，见 §12）  
**Related:** ADR-005, ADR-019, `architecture-review-post-orchestration-2026-06-23.md` 候选 8, `2026-06-24-execution-frame-design.md`

---

## 1. Problem

ADR-019 将三链 tx 级编排收敛到 `runOrchestration()`，但链差异仍通过 wrapper 内 **10 个 default-noop `std::function`** 逐行 inline 注入。每个 `ExecuteVia*.cpp` 内联完整 hook 策略（~80–150 行 lambda），无具名 implementation module。

**Deletion test 部分失败：** 删除任一 wrapper 的 hook 块，复杂度不会消失，而是散落到测试与 call site；新增 hook 需改 3 个 cpp + `OrchestrationHooks` 默认值 + 测试矩阵。

**architecture-review 定位：** 候选 8 — Typed OrchestrationProfiles（P2 Speculative，在 P0 内核帧工作之后）。

---

## 2. Decision

引入 **`XxxOrchestrationProfile` 具名 module**：链侧提供 `Session` + `buildHooks(session)`；wrapper 仅负责 extension 生命周期、ctx 构建、output 映射，hook 策略迁入 Profile。

### 2.1 明确不做（grilling 否决项）

| 否决方案 | 原因 |
| --- | --- |
| 公开 `executor.hpp` facade + `EvmExecutor<Profile>` | P2 优先级过低；与 ExecutionFrame 并行 rebase 风险；集成方已通过 TE 选路径 |
| `ExecutionCoreInput/Output` 基类 | 三链字段类型不一致；真交集仅 env 句柄，约束力弱 |
| logs/gasUsed 上提对齐 Output | ADR-019 范围外额外 refactor；Fisco logs 在 `executionContext` 内 |
| TE 双层泛型 `TransactionExecutorImpl<TxExec, ExecProfile>` | 超出候选 8 杠杆；TE 仍直接调 `executeVia*` |
| 本 spec 合并候选 4 ErrorPolicy | scope 膨胀；error hook 独立 follow-up |

### 2.2 分层（与 ADR-019 / ExecutionFrame 正交）

```text
wrapper (ExecuteVia*.cpp)     Profile (新增)              kernel (不变)
─────────────────────────     ──────────────              ───────────────
extension 构造                Session { input, output,    runOrchestration
ctx 构建                        extension, ... }
output 映射                   buildHooks(session)
error hook inline (暂留)          → OrchestrationHooks
buyGas/refundGas (OpStack)                                executeMessage
                                                          ExecutionFrame (候选1, 独立)
```

---

## 3. Public interface

### 3.1 Files

```text
bcos-evm/bcos/
  FiscoOrchestrationProfile.h    # Session + buildHooks
  FiscoOrchestrationProfile.cpp  # hook 实现体（从 ExecuteViaHost.cpp 迁出）

bcos-evm/eth/
  EthOrchestrationProfile.h/.cpp       # P3

bcos-evm/opstack/
  OpStackOrchestrationProfile.h/.cpp   # P2
```

**Seam 纪律：** Profile 头文件不得 `#include` 其他链目录（守 ADR-005）。`eth/` 的 Eth Profile 不得 include `bcos/` / `opstack/`。

### 3.2 Fisco Profile 类型（P1 模板）

```cpp
namespace bcos::evm {

struct FiscoOrchestrationProfile {
    struct Session {
        ExecuteViaHostInput const& input;
        ExecuteViaHostOutput& output;
        FiscoHostExtension& extension;
        bool fixErrorHandling{false};
        bool eip7623Enabled{false};
    };

    static OrchestrationHooks buildHooks(Session& session);
};

}  // namespace bcos::evm
```

**Invariants:**

- `Session` 由 wrapper 在栈上构造；Profile 不拥有 extension 生命周期。
- `buildHooks` 返回按值 `OrchestrationHooks`（含 capturing lambdas）；wrapper 不 inline hook 体。
- **`mapException` / `mapIntrinsicFailure` 不在 Profile 范围** — 留 wrapper inline，标注 `// TODO: OrchestrationErrorPolicy (candidate 4)`。
- Input/Output 类型**不变** — 仍用现有 `ExecuteViaHostInput` / `ExecuteViaHostOutput`；不引入第三套 struct、无 mapper。

### 3.3 Wrapper 收敛形态（Fisco 示例）

```cpp
task::Task<ExecuteViaHostOutput> executeViaHost(ExecuteViaHostInput input) {
    // validate, trace, ctx 构建, extension 构造 — 不变
    FiscoOrchestrationProfile::Session session{
        input, output, extension, fixErrorHandling, eip7623Enabled};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
    // mapException / mapIntrinsicFailure — 仍在此 inline（candidate 4）
    runOrchestration(ctx, hooks);
    // output 映射 — 不变
}
```

---

## 4. Profile 负责的 hook（Fisco）

| Hook | 迁入 Profile | 留 wrapper |
| --- | --- | --- |
| `prepareMessage` | ✅ `deriveMessage` | |
| `preExecute` | ✅ auth check | |
| `intrinsicPolicy` 配置 | ✅ mode + fields | |
| `preKernel` | ✅ value transfer + 21000 | |
| `postAdopt` / `postSettle` | ✅（若有） | |
| `mapIntrinsicFailure` | | ✅ → candidate 4 |
| `mapException` | | ✅ → candidate 4 |
| `tuneKernelInput` | ✅（若有） | |

Eth / OpStack Profile 按各自 wrapper 同构拆分；OpStack Session 额外持有 `OpStackTxExecutionData& txData`（影子帧消减留候选 7）。

---

## 5. Migration（分链分期）

| Phase | 链 | 前置 | 交付 |
| --- | --- | --- | --- |
| **P1** | Fisco | 无 | `FiscoOrchestrationProfile` + wrapper 瘦身 + `FiscoOrchestrationProfileTest` |
| **P2** | OpStack | 无（可与候选 7 协调） | `OpStackOrchestrationProfile`；Session 含 `txData&` |
| **P3** | Eth | **软依赖 ExecutionFrame PR2** | `EthOrchestrationProfile`；`postAdopt` vmerr hook 可能随帧重构调整 |

**与 ExecutionFrame 关系：** 文件级无交叉（Frame 改内核，Profile 改 wrapper）。Eth Profile **软依赖** ExecutionFrame PR2，避免 `postAdopt` vmerr 语义被帧重构推翻后重复劳动。

**与候选 4 / 7 关系：**

- 候选 4（OrchestrationErrorPolicy）：独立 follow-up；Profile 不动 error hook 位。
- 候选 7（OpStackSettlementContext）：P2 OpStack Profile 可与候选 7 合并或顺序执行。

---

## 6. Testing

### 6.1 P1: `test/bcos/FiscoOrchestrationProfileTest.cpp`

| Case | 断言 |
| --- | --- |
| EIP-7623 enabled | `buildHooks(session).intrinsicPolicy.mode == Eip7623` |
| auth early-exit | mock `AuthPort` 返回 error → 调用 `preExecute` hook 后 `earlyExit == true` |
| prepareMessage CREATE | hook 调用后 `ctx.message.recipient` 符合 `deriveMessage` 预期 |

**Regression gate（每 PR）：** 现有 `ExecuteViaHostSmokeTest`、`BcosAuthOrchestratorHookTest`、FISCO smoke 全绿。

### 6.2 不做

- E2E characterization 快照（本 refactor 范围不需要）
- specs-tests adapter 改动（P1 不碰）

---

## 7. Success criteria

1. Fisco wrapper hook inline 体迁入 `FiscoOrchestrationProfile.cpp`；wrapper 仅 Session 构造 + error hook + output 映射。
2. 零语义变更：全量 FISCO smoke / orchestration 测试无回归。
3. `FiscoOrchestrationProfileTest` 覆盖 ≥3 个 hook wiring case。
4. 不新增公开 include；不修改 `ExecuteViaHostInput`/`Output` 布局。
5. `eth/` 无 BCOS/OP include（ADR-005 延续）。

---

## 8. Out of scope

- 公开 `executor.hpp` / `EvmExecutor<Profile>` facade
- `ExecutionCoreInput/Output` / concept / TE 双模板
- Input/Output 基类重构、logs 上提
- 候选 4 ErrorPolicy、候选 7 OpStackSettlementContext（独立 spec）
- specs-tests 生产路径对齐

---

## 9. Grilling resolutions (2026-06-24)

| # | Question | Resolution |
| --- | --- | --- |
| Q1 | Profile 是否引入第三套 Input + mapper | **否决**（随 Q5 收窄）；Input 保持现有 `ExecuteVia*Input` |
| Q2 | ExecutionCoreInput 基类 | **否决**（Q5）；不引入基类 |
| Q3 | Output logs/gasUsed 上提 | **否决**（Q5） |
| Q4 | 消除 executionContext 重复 logs | **否决**（Q5）；维持 B2 最小动作 |
| Q5 | 范围对齐 architecture-review 候选 8 | **A** — 仅具名 OrchestrationProfile；砍掉公开 facade |
| Q6 | Profile API 签名 | **A** — `Session` + `buildHooks(session)` |
| Q7 | 与 ExecutionFrame 时序 | **A** — 分链分期；Fisco P1 先行；Eth 软依赖 Frame PR2 |
| Q8 | 是否含 error hook | **A** — 留给候选 4；Profile 只抽链策略 hook |
| Q9 | P1 测试策略 | **B** — Profile 单测 + 现有 smoke gate |
| Q10 | 旧 evm-unified-executor-api spec | **A** — 本 spec 取代；旧文件留 redirect |

Prior brainstorming decisions (facade / concept / TE integration) **explicitly rejected** — documented in §2.1 for future readers.

No unresolved TBDs remain for P1 implementation planning.

# Design Spec: EvmHostHooks 承载 FISCO Legacy SSTORE / CREATE Nonce 语义

**Status:** Accepted  
**Date:** 2026-06-30  
**Priority:** P0（链特有逻辑从 `eth/` 内核移除）  
**Related:** ADR-005, ADR-027, `eth/state/EvmHostHooks.h`, `eth/state/EthHost.hpp`, `bcos/FiscoVmHostPolicy.h`, `bcos/FiscoRevisionConfig.h`, capability-matrix `bugfix_evm_storage_status` / CREATE nonce rows

**Prerequisite:** P2 已完成 — `makeIsthmusRevisionConfig()` 已迁出 `eth/RevisionConfig.h` 至 `opstack/OpStackIsthmusRevision.h`。

---

## 1. Problem Statement

### 1.1 现状

FISCO 治理层 bugfix 开关 `bugfix_evm_storage_status` 与 `bugfix_nonce_initialize` 以 **bool 字段** 侵入 eth 共享内核，并在内核热路径产生链特有分支：

| 字段 | 来源 | eth 消费点 | 语义 |
| --- | --- | --- | --- |
| `fixStorageStatus` | `FiscoRevisionConfig.fix_storage_status` | `EthHost::set_storage()` | ON：EIP-2200/3529 精确 EVMC 状态 + 完整 refund；OFF：2-state 简化 + 简化 refund |
| `fixNonceInit` | `FiscoRevisionConfig.fix_nonce_init` | `ExecutionFrame::finalizeFrame()` top-level CREATE 成功 | ON：补设 `createAddr` nonce=1 |

传播链（违反 ADR-005 Rule 1 精神）：

```text
FiscoPolicy
  → FiscoExecutionBundle / FiscoPrecheckPolicy
  → EvmTxContextView / ExecuteMessageInput          // Tier-2 字段
  → TxExecutionRunner
  → EthHost(m_fixStorageStatus) / FrameExecutionEnv(fixNonceInit)
  → if (bool) { FISCO legacy ... }
```

ADR-027 将 `fixStorageStatus` / `fixNonceInit` 列为 **Tier-2 execution infra**，与 `extension*` 并列——但二者本质是 **VmHostPolicy 行为**，不是中性 infra。

### 1.2 目标

1. **eth 内核** 仅实现标准 geth/evmone 语义（等价于 today `fixStorageStatus=true` 路径）。
2. **FISCO legacy**（`fix_storage_status=false` / `fix_nonce_init=true`）通过 `EvmHostHooks` 虚方法 override，集中在 `bcos/FiscoVmHostPolicy`。
3. **零行为变更**：现有 `SstoreStatusTest`、`FiscoExecute*` characterization、EEST smoke 全绿。
4. **删除** eth 层 bool 字段及 ADR-027 Tier-2 中对应项。

### 1.3 Non-Goals

- 合并 `FiscoVmHostPolicy::applyCreateNonceSemantics`（nested CREATE）与 top-level `finalizeTopLevelCreateNonce`——可后续 follow-up。
- 修改 `FiscoRevisionConfig` 字段名或 Features 映射。
- OpStack / Eth reference 行为变更（二者使用默认 hook）。
- P1（`IntrinsicDebitMode::OpStackEntry`）、P3（注释清理）不在本 spec 范围。

---

## 2. Approaches Considered

### Approach A — 扩展 `EvmHostHooks` 多方法切片（推荐）

在现有 `EvmHostHooks` 上增加 SSTORE / CREATE nonce 相关虚方法；默认实现 = 标准 Ethereum；`FiscoVmHostPolicy` 按 `RevisionFlags` override。

**优点：** 与 ADR-005 §3 VmHostPolicy 分工一致；改动面可控；Eth/OpStack 零配置。  
**缺点：** 虚方法 +3~4，热路径多一次 indirect call（与现有 `allowSelfdestruct` 等同量级）。

### Approach B — 单一 `commitSstore()` 大 hook

`EthHost::set_storage` 将全部 SSTORE 副作用委托给 extension 一个方法。

**优点：** 接口最少。  
**缺点：** FISCO/Eth 重复 journal/original 快照逻辑；测试需 mock 整段 SSTORE；与现有 `EthHost` 职责边界模糊。**不推荐。**

### Approach C — `FiscoEthHost` 子类

eth 基类 + bcos 子类 override `set_storage`。

**优点：** 无 vtable 在 Eth 默认路径（若不用子类）。  
**缺点：** 破坏 `EthHost` 单类型假设；nested `EthHost::call` 需链侧工厂；与 ADR-027 Bundle 注入模型冲突。**不推荐。**

**Decision: Approach A.**

---

## 3. Architecture

### 3.1 目标分层

```text
┌─────────────────────────────────────────────────────────┐
│ eth/  kernel                                              │
│   EthHost::set_storage  ──► EvmHostHooks (defaults)       │
│   ExecutionFrame        ──► extension->finalizeTopLevel…  │
│   无 fixStorageStatus / fixNonceInit 字段                  │
└─────────────────────────────────────────────────────────┘
         ▲ extension* (Tier-1 only)
         │
┌────────┴──────────────────────────────────────────────────┐
│ bcos/  FiscoVmHostPolicy                                  │
│   RevisionFlags.fix_storage_status / fix_nonce_init        │
│   override SSTORE + top-level CREATE nonce hooks          │
└───────────────────────────────────────────────────────────┘
```

### 3.2 ADR-027 Tier 变更

| Tier | Before | After |
| --- | --- | --- |
| 1 — injection | `chainPort*`, `extension*` | 不变 |
| 2 — execution infra | `vm*`, `blockHashes`, **`fixStorageStatus`**, **`fixNonceInit`** | `vm*`, `blockHashes` only |
| 3 — tx mutable | 不变 | 不变 |

`FiscoRevisionConfig.fix_*` **保留在 bcos 层**；不再投影到 `EvmTxContextView`。

---

## 4. API Design

### 4.1 `EvmHostHooks` 新增方法

文件：`eth/state/EvmHostHooks.h` + 新 TU `eth/state/EvmHostHooks.cpp`

```cpp
class State;

struct EvmHostHooks
{
    // ... existing virtuals unchanged ...

    /// Pre-write gas refund. Default: EIP-3529 (applySstoreRefundEip3529).
    virtual void applySstoreRefund(State& state, evmc_bytes32 const& current,
        evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept;

    /// Post-write EVMC status for evmone. Default: EIP-2200 precise mapping.
    virtual evmc_storage_status classifyStorageStatus(evmc_bytes32 const& original,
        evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept;

    /// Legacy path only: simplified refund after DELETED when precise mode off.
    /// Default: no-op.
    virtual void applyLegacySstoreDeletedRefund(
        State& state, evmc_storage_status status) const noexcept;

    /// Top-level CREATE success nonce fix. Default: no-op.
    virtual void finalizeTopLevelCreateNonce(
        State& state, evmc_address const& createAddr) noexcept;
};
```

**命名说明：** `applyLegacySstoreDeletedRefund` 显式标记 FISCO OFF 路径专用，避免 Eth 默认误用。

### 4.2 默认实现位置

将 today `EthHost.cpp` 中以下逻辑迁入 `EvmHostHooks.cpp`：

- 匿名 namespace `applySstoreRefundEip3529`
- `EthHost::classifyStorageStatus(..., fixStorageStatus=true)` 分支

`EthHost` 保留：

- `m_storageOriginalValues` 快照（tx 级 original committed value）
- unchanged-slot 早退 `EVMC_STORAGE_ASSIGNED`
- `m_state.set_storage` 调用

### 4.3 `EthHost::set_storage` 编排（不变序）

```text
1. try_emplace original (first touch per slot)
2. current = get_storage
3. if current == newValue → return ASSIGNED
4. hooks.applySstoreRefund(state, current, original, newValue)
5. state.set_storage
6. status = hooks.classifyStorageStatus(original, current, newValue)
7. hooks.applyLegacySstoreDeletedRefund(state, status)
8. return status
```

`extension == nullptr` 时调用 **非虚** 自由函数或 `EvmHostHooks` 静态默认实现（与 today `extension=null, fixStorageStatus=true` 等价）。

### 4.4 `FiscoVmHostPolicy` override 语义

`RevisionFlags` 新增：

```cpp
bool fix_storage_status{false};  // 来自 FiscoRevisionConfig
bool fix_nonce_init{false};      // 已有
```

| 方法 | `fix_storage_status=true` | `fix_storage_status=false` |
| --- | --- | --- |
| `applySstoreRefund` | 委托基类 / EIP-3529 | no-op |
| `classifyStorageStatus` | 委托基类 / 精确 4-state+ | 2-state: zero→DELETED, else→MODIFIED |
| `applyLegacySstoreDeletedRefund` | no-op | DELETED 时 `add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529)` |

| 方法 | `fix_nonce_init=true` | `fix_nonce_init=false` |
| --- | --- | --- |
| `finalizeTopLevelCreateNonce` | `set_nonce(createAddr, 1)` if non-zero addr | no-op |

**Nested CREATE：** `applyCreateNonceSemantics` 在 `prepareMessage` 中保留现状（本 spec 不合并）。

### 4.5 `ExecutionFrame` 变更

```cpp
// finalizeFrame, top-level success + CREATE:
if (scope == FrameScope::TopLevel && isCreateKind(kind) && ctx.extension != nullptr)
{
    auto addr = resolveCreateAddress(...);
    ctx.extension->finalizeTopLevelCreateNonce(ctx.state, addr);
}
```

删除 `FrameExecutionEnv::fixNonceInit` 及构造函数参数。

---

## 5. File Change List

### 5.1 eth/（内核）

| File | Change |
| --- | --- |
| `eth/state/EvmHostHooks.h` | 新增 4 个 virtual + forward declare `State` |
| `eth/state/EvmHostHooks.cpp` | **新增** — 默认 SSTORE 实现 |
| `eth/state/EthHost.hpp` | 删除 `fixStorageStatus` 构造参数、`m_fixStorageStatus`、`classifyStorageStatus` static |
| `eth/state/EthHost.cpp` | `set_storage` 调 hooks；删除 FISCO 注释 |
| `eth/kernel/execution/EvmCallFrame.h` | 删除 `fixNonceInit` |
| `eth/kernel/execution/ExecutionFrame.cpp` | 调 `finalizeTopLevelCreateNonce` |
| `eth/kernel/execution/TxExecutionRunner.cpp` | 简化 `EthHost` / `FrameExecutionEnv` 构造 |
| `eth/kernel/execution/InnerExecute.h` | 删除 `fixStorageStatus`, `fixNonceInit` |
| `eth/kernel/state-transition/EvmTxContextView.h` | 删除同上 + `toExecuteMessageInput` 拷贝 |
| `eth/CMakeLists.txt`（或等效） | 添加 `EvmHostHooks.cpp` |

### 5.2 bcos/

| File | Change |
| --- | --- |
| `bcos/FiscoVmHostPolicy.h` | `RevisionFlags.fix_storage_status`；declare overrides |
| `bcos/FiscoVmHostPolicy.cpp` | implement overrides |
| `bcos/FiscoExecutionBundle.h` | 删除 `m_view.fixStorageStatus/fixNonceInit`；deps 注入 `fix_storage_status` |
| `bcos/FiscoPrecheckPolicy.cpp` | 删除 `executeInput.fixStorageStatus/fixNonceInit` |

### 5.3 test/

| File | Change |
| --- | --- |
| `test/state/SstoreStatusTest.cpp` | legacy OFF 矩阵改用 `FiscoVmHostPolicy` 或 test helper hook |
| `test/state/SstoreRefundTest.cpp` | 删除 `input.fixStorageStatus` |
| `test/eth/EvmTxContextViewTest.cpp` | 删除 fix* 断言 |
| `test/bcos/FiscoVmHostPolicyTest.cpp` | 可选：增加 SSTORE / top-level nonce 覆盖 |
| 建议新增 `test/bcos/FiscoSstoreStatusTest.cpp` | 从 eth test 迁 FISCO 特有矩阵 |

### 5.4 docs/

| File | Change |
| --- | --- |
| `docs/adr/027-execution-session-injection.md` | Tier-2 删除 fix* 字段（follow-up ADR patch 或本 PR 同更） |
| `docs/eth-layer-design-review.md` | 删除 `fixStorageStatus` 提及 |

---

## 6. Data Flow (After)

```text
FiscoPolicy::computeRevisionConfig
  → FiscoRevisionConfig.fix_storage_status / fix_nonce_init

FiscoExecutionBundle
  → FiscoVmHostPolicy(deps.revisionFlags.{fix_storage_status, fix_nonce_init})
  → EvmTxContextView.extension = &policy   // Tier-1 only

runTxPipeline → executeMessage
  → TxExecutionRunner
  → EthHost(state, ..., extension, chainPort)   // 无 bool
  → set_storage → extension->applySstoreRefund / classify / legacyRefund

ExecutionFrame (top-level CREATE ok)
  → extension->finalizeTopLevelCreateNonce
```

Eth / OpStack：`EthVmHostPolicy` / `OpStackVmHostPolicy` 不 override → 标准语义。

---

## 7. Testing Strategy

### 7.1 必须全绿（零行为变更 gate）

| Test | 验证点 |
| --- | --- |
| `SstoreStatusTest` | fix ON 矩阵（默认 hook）；OFF 矩阵迁 bcos + FiscoVmHostPolicy |
| `SstoreRefundTest` | EIP-3529 refund 与 extension 路径 |
| `FiscoVmHostPolicyTest` | 现有 + 新增 hook 覆盖 |
| `FiscoExecute*` / `Bcos*` characterization | 无回归 |
| `EvmTxContextViewTest` | 更新字段断言 |
| `RevisionConfigProfileTest` | 不受影响（FiscoRevisionConfig 字段保留） |

### 7.2 新增测试建议

1. **`FiscoSstoreStatusTest`** — OFF 2-state 矩阵（自 `SstoreStatusTest` 迁出）。
2. **`FiscoTopLevelCreateNonceHookTest`** — `fix_nonce_init=true` + top-level CREATE success → nonce=1。

### 7.3 构建验证

```bash
cmake --build build --target SstoreStatusTest FiscoVmHostPolicyTest FiscoExecuteSmokeTest -j
ctest -R 'SstoreStatus|SstoreRefund|FiscoVmHost|FiscoExecute|EvmTxContextView' --output-on-failure
```

---

## 8. Migration Plan（单 PR，推荐顺序）

| Step | 内容 | 可独立编译 |
| --- | --- | --- |
| 1 | 添加 `EvmHostHooks.cpp` + virtual 声明 + 默认实现 | ✅ |
| 2 | `EthHost::set_storage` 改调 hooks（extension=null 行为不变） | ✅ |
| 3 | `FiscoVmHostPolicy` overrides + `RevisionFlags.fix_storage_status` | ✅ |
| 4 | `ExecutionFrame` + `finalizeTopLevelCreateNonce` | ✅ |
| 5 | 删除 eth 层 bool 字段 + bcos 传播 + 测试迁移 | ✅ |
| 6 | ADR-027 / design-review 文档同步 | ✅ |

**Rollback：**  revert 单 PR；无 schema / on-chain 变更。

---

## 9. Risks & Mitigations

| Risk | Mitigation |
| --- | --- |
| 虚调用热路径开销 | 与现有 `allowSelfdestruct` 同级；FISCO 已 always 有 extension |
| `extension=null` 测试路径行为漂移 | Step 2 明确等价 today `fixStorageStatus=true`；SstoreStatusTest 先不删 |
| nested vs top-level nonce 双路径混淆 | spec §4.4 文档化；不合并 scope |
| ADR-027 与代码漂移 | 同 PR 更新 ADR Tier 表 |

---

## 10. Open Questions

1. **Test 归属：** FISCO OFF 矩阵留在 `test/state/` 用 mock hook，还是迁 `test/bcos/FiscoSstoreStatusTest.cpp`？  
   **建议：** 迁 bcos，eth test 只保留标准 Ethereum 语义。

2. **`applyLegacySstoreDeletedRefund` 是否长期保留？**  
   **建议：** 保留至 FISCO 全链启用 `bugfix_evm_storage_status` 后可 deprecate（另开 cleanup spec）。

---

## 11. Success Criteria

- [ ] `eth/` 源码中无 `fixStorageStatus` / `fixNonceInit` 标识符
- [ ] `EthHost` 构造函数无 FISCO 相关参数
- [ ] `FiscoExecutionBundle` 不再设置 view.fix*
- [ ] 上述 ctest 全绿
- [ ] Gap 38 审计：`eth/` 仍无 `bcos/` / `opstack/` include

---

## Appendix A — Behavior Matrix (Reference)

与 today `SstoreStatusTest` 一致：

| fix_storage_status | existing | new | expected status |
| --- | --- | --- | --- |
| ON | zero | zero | ASSIGNED |
| ON | non-zero | zero | DELETED |
| ON | zero | non-zero | ADDED |
| ON | non-zero | non-zero | ASSIGNED |
| OFF | zero | zero | ASSIGNED |
| OFF | non-zero | zero | DELETED |
| OFF | zero | non-zero | MODIFIED |
| OFF | non-zero | non-zero | ASSIGNED |

---

## Appendix B — Related Completed Work

- **P2（2026-06-30）：** `makeIsthmusRevisionConfig()` → `opstack/OpStackIsthmusRevision.h`

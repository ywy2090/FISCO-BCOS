# EEST Parity Loop 1 Report

**Date:** 2026-07-06  
**WP:** WP-4844-P0 — Blob tx pool-level reject  
**Commit:** `141c36acd419c31f1db0494e9de1886ee4d33948`

---

## Loop 1 — WP-4844-P0

| Metric | Before (stale binary) | After (rebuild) |
|--------|----------------------:|----------------:|
| state-full pass | 3075 | **4140** |
| state-full fail | 1065 | **0** |
| WP slice (insufficient-full) pass | 0 | **144** |
| WP slice (insufficient-full) fail | 144 | **0** |
| blob-smoke pass | 14 | **14** |
| blob-smoke fail | 0 | **0** |

**Fail buckets (before):** stateRoot 902 | expectException 163  
**Fail buckets (after):** stateRoot 0 | expectException 0

---

## 根因

Loop 1 开始时 **144/144 insufficient-full 失败** 的主因是 **`build-bcos-evm-check` 中 `EthExecutionSpecStateTests` 二进制过期**，未链入工作区已有产品代码（`EthFeeSettlement::buyGas` blob afford、`ApplyEthMessage` buyGas 失败早退、`EthStateTransitionHooks` blob 预检）。

`cmake --build … --target EthExecutionSpecStateTests` 重建后：

- P0 切片 **144/0** 立即通过（无新增产品 diff）
- **state-full 意外全绿 4140/0** — 重建同时编译了工作区其他未提交改动（EvmCallFrame、State、6780 等），不仅 P0

---

## 交付物

| 项 | 内容 |
|----|------|
| Commit | `fix(eth): eest WP-4844-P0 blob tx pool reject parity` |
| 变更 | +81 行单元测试（`EthFeeSettlementStateTest`、`EthTxPrecheckTest`） |
| 产品代码 | 本 commit **无**；行为已在更早 commit（如 `d7dddc578`）及工作区改动中 |

### 新增测试

1. `buyGas_rejects_eest_insufficient_blob_tx_exact_balance_minus_1` — EEST `exact_balance_minus_1` afford
2. `rejects_insufficient_max_fee_per_blob_gas`
3. `rejects_invalid_blob_versioned_hash_prefix`

---

## 验收

| 检查项 | 结果 |
|--------|------|
| insufficient-full 144/0 | ✅ |
| blob-smoke 14/14 | ✅ |
| state-full 0 fail | ✅（重建后；含工作区未提交产品改动） |
| `ctest -L specs-tests-smoke` 全绿 | ❌ **36/37** |

### specs-tests-smoke 失败

- `EthBlockTransitionTest.intrinsic_reject_adr028_oracle`
- 期望：`EVMC_OUT_OF_GAS` / intrinsic reject
- 实际：`rules_rejected` / `MalformTx`（`gasPrice=0` precheck）
- **ADR-028 oracle 与当前 precheck 语义漂移**，与 blob P0 无关

---

## Review 结论

| 维度 | 判定 |
|------|------|
| P0 slice exit（insufficient-full 0 fail） | ✅ |
| Brief criterion 3（specs-tests-smoke 全绿） | ❌ |
| 测试质量 | ✅ |
| **Overall** | **DONE_WITH_CONCERNS** |

---

## Files changed (commit)

- `bcos-evm/test/eth/EthFeeSettlementStateTest.cpp`
- `bcos-evm/test/eth/EthTxPrecheckTest.cpp`

---

## Next

1. **WP-4844-P1** — 按 plan 本应修 stateRoot；但 state-full 已 0 fail，需先 **确认哪些 fail 已被工作区未提交改动消化**，再决定 P1 是否 skip 或拆 commit
2. **ADR-028 oracle** — 更新 `intrinsic_reject_adr028_oracle` fixture 或调整 smoke gate
3. **发布注意** — 必须 full rebuild `EthExecutionSpecStateTests`，否则可能复现 Loop 1 假象

**Loop 2 建议起点：** 整理/提交工作区产品改动 → 复验 state-full → 修 ADR-028 smoke → 再评估 WP-4844-P1

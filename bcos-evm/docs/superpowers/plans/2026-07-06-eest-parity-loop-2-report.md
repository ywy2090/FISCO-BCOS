# EEST Parity Loop 2 Report

**Date:** 2026-07-07  
**WP:** WP-ADR028-SMOKE — Block transition test oracle + fixture drift  
**Commit:** `d48468c`

---

## Loop 2 — WP-ADR028-SMOKE

| Metric | Before | After |
|--------|-------:|------:|
| state-full pass | 4140 | **4140** |
| state-full fail | 0 | **0** |
| specs-tests-smoke | 36/37 | **37/37** |
| EthBlockTransitionTest (4 fixtures) | 1/4 pass | **4/4 pass** |

---

## 根因

1. **ADR-028 oracle** — `gasPrice=0` 触发 `RulesRejected/Malformed`，fixture 仍期望 `EVMC_OUT_OF_GAS/OutOfGasLimit`
2. **其余 block fixture** — `gasPrice=0/1` 低于 `blockInfo.baseFee=7`，EIP-1559 precheck 拒绝
3. **multi_tx_success_revert** — ADR-015 归一化后 top-level REVERT → `settlementStatus=SUCCESS` + `includedTxVmError=true`，fixture 仍期望 `EVMC_REVERT`

---

## 变更

| 文件 | 改动 |
|------|------|
| `EthBlockTransitionTest.cpp` | 添加 `MalformTx`/`Malformed` receipt 解析 |
| `intrinsic_reject_adr028_oracle.json` | 期望 `EVMC_FAILURE` + `MalformTx` |
| `simple_transfer.json` | `gasPrice` 0x01 → 0x0a (≥ baseFee) |
| `multi_tx_*.json` | `gasPrice` 0x0 → 0x0a |
| `multi_tx_success_revert.json` | tx2 期望 ADR-015 归一化语义 |

---

## STOP 条件检查（Master prompt）

| 条件 | 状态 |
|------|------|
| state-full **0 fail** (4140 pass) | ✅ |
| `ctest -L specs-tests-smoke` 全绿 | ✅ **37/37** |
| 已绿目录无回归 | ✅ |

**→ EEST parity loop STOP 条件已满足。**

---

## Commits（Loop 1–2）

| Loop | Commit | 内容 |
|------|--------|------|
| 1 | `141c36acd` | WP-4844-P0 单元测试 |
| 2 | `d48468c` | Block fixture + ADR-028 oracle |

---

## Next

Loop 已达成 STOP。可选后续（非 loop 范围）：

- 将 Loop 1/2 报告合入 PR 描述
- 提交/拆分工作区其余未跟踪文档（matrix、loop prompt 等）
- 启动 statetest harness Phase 1（见 statetest integration spec）

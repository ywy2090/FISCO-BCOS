# Task 1 — Revision Profile 审计笔记

**日期：** 2026-06-20  
**范围：** `EthChainPolicy::computeRevisionConfig` vs matrix / ADR-004 / `RevisionConfigProfileTest`

---

## Step 1 — EthChainPolicy 逐 fork 快照

来源：`bcos-evm/eth/vm/EthChainPolicy.h:27-41`

| blockNum | revision | EthChainPolicy 显式赋值 true | 保持 default false |
|----------|----------|-------------------------|-------------------|
| 19,426,587 | CANCUN | `warm_access`, `eip1153`, `eip4844`, `eip5656`, `eip6780` | `eip2537`, `eip7623`, `eip7212`, `eip7823`, `eip1559`, `eip3651`, `eip7702`, `prague_post_execution` |
| 22,000,000 | PRAGUE | 上述 + `eip2537`, `eip7623`; `calldata_floor_per_token=10` | `eip7212`, `eip7823`, `eip1559`, `eip3651`, **`eip7702`**, `prague_post_execution` |
| 25,000,000 | OSAKA | 上述 + `eip7212`, `eip7823` | `eip1559`, `eip3651`, **`eip7702`**, `prague_post_execution` |

### eip7702 重点

- `RevisionConfig` 有 `eip7702` 字段（`RevisionConfig.h:27`），`ExecuteMessage.cpp:173` 以该 flag 门控 `applyAuthorizations`。
- **`EthChainPolicy::computeRevisionConfig` 从未赋值 `eip7702`** → PRAGUE/OSAKA 区块仍为 `false`。
- Matrix 声明：`EIP-7702 revision enable | inherited (EthChainPolicy at PRAGUE+)`（`capability-matrix.md:53`）——与实现不符。
- 对照：`FiscoPolicy` 在 `feature_evm_prague` 时设 `eip7702=true`（`FiscoPolicy.h:66`）；`makeIsthmusRevisionConfig` 亦设 true（`RevisionConfig.h:67`）。
- 测试：`RevisionConfigProfileTest` ETH 路径 PRAGUE/OSAKA 期望 `eip7702=false`（与 EthChainPolicy 一致，但与 matrix 声明冲突）。

**判定：🔴** — matrix 声称 PRAGUE+ inherited，EthChainPolicy 未启用；7702 kernel 行 baseline 不可达。

---

## Step 2 — RevisionConfigProfileTest

```bash
cd build && ./bcos-evm/test/RevisionConfigProfileTest --log_level=test_suite
```

**结果：** PASS（4 cases，无 errors detected）

测试断言与 Step 1 表一致；ETH policy 行未期望 `eip7702=true`（PRAGUE/OSAKA block 22M/25M）。

---

## Step 3 — ADR-004 profile-only grep（`bcos-evm/eth/`）

| 字段 | EthChainPolicy 赋值 | TE consumer（`bcos-evm/eth/`） | ADR-004 分类 |
|------|---------------|-------------------------------|-------------|
| `warm_access` | `>= BERLIN` → true | `ExecuteMessage.cpp:140,147,177` 传入 `warmTransactionEntry` / `EthHost` | profile-only（语义门控为 revision；flag 仍被读取） |
| `eip1559` | 未赋值 | **无** grep 命中 | profile-only |
| `eip3651` | 未赋值 | **无** grep 命中 | profile-only |
| `prague_post_execution` | 未赋值 | **无** grep 命中 | profile-only |
| `eip7823` | OSAKA+ → true | `ModexpGas.h:30-32` 有 `modexpEip7823Enabled`；**无** `shouldRejectModexpEip7823` 调用点 | profile-only until wired |

额外 consumed 字段（非 profile-only）：

| 字段 | consumer |
|------|----------|
| `eip7702` | `ExecuteMessage.cpp:173` |
| `eip7623` | `ExecuteViaEth.cpp:64,80` + `calldata_floor_per_token` |
| `eip1153/5656/6780/4844` | evmone via `revision`（`VMInstance.cpp:23-24`） |
| `eip2537` | `EthPrecompiles` via `revision`（flag 仅 FISCO manager） |

---

## Step 4 — geth / Besu 对照摘要

| EIP | geth | Besu |
|-----|------|------|
| 1153/5656/6780/4844 | `newCancunInstructionSet` enable* (`jump_table.go:115-122`) | `CancunGasCalculator extends ShanghaiGasCalculator` |
| 7702 | `newPragueInstructionSet` → `enable7702` (`jump_table.go:109-112`) | `PragueGasCalculator extends CancunGasCalculator` |
| 2537/7623 | `evm.go:158` Prague rules + precompiles | `PragueGasCalculator` |
| 7212/7823 | Osaka rules / modexp bounds | `OsakaGasCalculator`; `BigIntegerModularExponentiationPrecompiledContract` EIP-7823 |

---

## 缺口汇总

1. **🔴 eip7702**：EthChainPolicy 未在 PRAGUE+ 设 flag；matrix 声称 inherited；7702 auth apply 在 reference 路径不可达。
2. **🟡 eip7212**：profile 在 OSAKA+ 设 true，但 `EthPrecompiles` 无 0x0100 实现（kernel unsupported）。
3. **📋 eip7823/eip1559/eip3651/prague_post_execution**：profile-only，Eth reference 无 TE consumer（或仅有 header 未 wired）。

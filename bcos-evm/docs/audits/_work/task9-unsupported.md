# Task 9 — Unsupported / Feature-Gated / ⚪ 行审计笔记

**初审计日期：** 2026-06-20  
**复审计 commit：** `54e17a62c`（2026-06-21）  
**范围：** inventory #11、#17、#22–25 + BCOS ⚪ 四行；`makeIsthmusRevisionConfig()`、OPStack TE 路径未激活确认  
**参考：** `capability-matrix.md` OPStack 列；ADR-004 profile-only 分类；`inventory.md`  
**交叉引用：** Task 8 inherited smoke（#1–6、#10、#12、#14–15、#18–21）

---

## Step 1 — `makeIsthmusRevisionConfig()` 稀疏 profile（@ `54e17a62c`）

**源码：** `bcos-evm/eth/RevisionConfig.h:62-72`

| 字段 | Isthmus helper 赋值 | 默认（未赋值） |
|------|---------------------|----------------|
| `revision` | `EVMC_PRAGUE` | — |
| `warm_access` | **`true`**（`revision >= EVMC_BERLIN`） | — |
| `eip7623` | `true` | — |
| `eip7702` | `true` | — |
| `eip4844` | `true` | — |
| `prague_post_execution` | `false`（显式） | — |
| `calldata_floor_per_token` | `10` | — |
| `eip2537` | — | `false`（kernel 经 `revision>=PRAGUE`） |
| `eip7212` | — | `false` |
| `eip7823` | — | `false` |
| `eip1153` / `eip5656` / `eip6780` | — | `false`（evmone 经 `revision`） |
| `eip1559` / `eip3651` | — | `false` |

**变更（OP-09b）：** `warm_access` 由 implicit false → **显式 true**，与 `EthPolicy` Berlin+ 对齐。

**测试锚点：**

- `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` — 期望 `warm_access=true`
- `OpStackTxPropsTest::isthmus_revision_profile_enables_warm_access`
- `IsthmusPostExecutionPolicyTest::isthmus_revision_config_disables_prague_post_execution`

**生产注入：** `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()` `input.revisionConfig = makeIsthmusRevisionConfig()`（`:197`）

---

## Step 2 — opstack 路径 grep（eip7212 / eip7823 / BCOS 钩子）

```bash
grep -rn "eip7212\|eip7823\|BALANCE_TRANSFER\|prague_post" bcos-evm/opstack/
```

| 模式 | `bcos-evm/opstack/` | 其他命中 |
|------|---------------------|----------|
| `eip7212` | **0** | 仅 `RevisionConfig.h` 字段定义 |
| `eip7823` | **0** | 仅 `RevisionConfig.h` 字段定义 |
| `BALANCE_TRANSFER` | **0** | `ExecuteViaHost.cpp`（BCOS 路径） |
| `prague_post_execution` | **0** | `RevisionConfig.h:70` 显式 `false` |

**BCOS-only orchestration grep：**

| 钩子 | BCOS `ExecuteViaHost` | OP `OpStackExecuteViaHost` / `OpHostExtension` |
|------|----------------------|-----------------------------------------------|
| `authChecker` | ✅ `ExecuteViaHost.cpp` | **无** |
| `enable_balance_transfer` / `maybeTransferValue` | ✅ BCOS 路径 | **无**（OP 用 `canTransfer`） |
| `BALANCE_TRANSFER_GAS` (21000) | ✅ BCOS 路径 | **无** |
| `persistContractCreateNonce` | ✅ → `FiscoHostExtension` | `OpHostExtension::bumpContractCreateNonce` 空实现 |

---

## Step 3 — inventory 行逐项核对

### #11 EIP-7212 precompile (0x0100) — ⚪ unsupported

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| `eip7212` 未设 | `false` | `makeIsthmusRevisionConfig` 未赋值 | ✅ |
| revision 门槛 | PRAGUE（非 OSAKA） | `config.revision = EVMC_PRAGUE` | ✅ |
| 0x0100 不可达 | `revision >= OSAKA && eip7212` | `PrecompileActive.h:44-46` | ✅ |
| opstack 无 7212 引用 | 0 命中 | grep opstack | ✅ |
| 测试（ETH/BCOS，非 OP Isthmus） | kernel 在 OSAKA+ 可达 | `Eip7212KernelTest`、`EipPrecompileRevisionGateTest` | ✅ 隔离 |

**Part 1 状态：** ✅ — Isthmus OP 路径 0x0100 未激活。

---

### #17 RevisionConfig `warm_access` — profile-only（**已设 true**）

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper | **`true`** | `RevisionConfig.h:66`；`assertIsthmusHelperProfile` | ✅ |
| TE 消费者 | `EthHost`、`warmTransactionEntry` | `warm_access=true` 时 full 2929 | ✅ |
| 与 Task 8 张力 | 初审计 false 导致 2929 弱化 | **OP-09b 已闭合** | ✅ |

**Part 1 状态：** ✅ — Isthmus 显式启用；与 EthPolicy Berlin+ 一致。

---

### #22 RevisionConfig `eip1559` — profile-only feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| TE consumer | 无 | grep 仅 policy 赋值 + 测试 | ✅ |
| OP 1559 orchestration | 独立路径 | `OpStackPreCheck`；`DepositTxPreCheckTest` | ✅ |

**Part 1 状态：** ✅

---

### #23 RevisionConfig `eip3651` — profile-only feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| coinbase warm 实际路径 | `txProps.warmCoinbase` + `warm_access=true` | `WarmTransactionEntry.h:67-70` | ✅ |

**Part 1 状态：** ✅ — coinbase warm 不经 `eip3651` flag；`warm_access` 已启用。

---

### #24 RevisionConfig `prague_post_execution` — ⚪ unsupported

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus 显式 false | `false` | `RevisionConfig.h:70`；`IsthmusPostExecutionPolicyTest` | ✅ |
| TE consumer | 无 | grep 仅定义 + 测试 | ✅ |
| opstack 无引用 | 0 命中 | grep opstack | ✅ |

**Part 1 状态：** ✅ — 显式 false；无 post-execution Prague 钩子。

---

### #25 RevisionConfig `eip7823` — ⚪ Isthmus feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| modexp reject 门控 | `rev.eip7823` | `ModexpGas.h`；`EthPrecompiles.cpp` | ✅ |
| `eip7823=false` 时 0x05 | 不执行 7823 长度拒绝 | `shouldRejectModexpEip7823` 首行 | ✅ |
| opstack 无 7823 引用 | 0 命中 | grep opstack | ✅ |

**Part 1 状态：** ✅ — Isthmus 未设 flag；0x05 modexp 无 7823 拒绝。

---

## Step 4 — BCOS ⚪ 行（OP 路径未激活）

| inventory | 能力 | matrix OPStack | 验证 | 判定 |
|-----------|------|----------------|------|------|
| ⚪ | BCOS fixed 21000 gas debit | unsupported | 仅 `ExecuteViaHost.cpp` `BALANCE_TRANSFER_GAS` | ✅ |
| ⚪ | BCOS auth check | unsupported | `authChecker` 仅 BCOS input | ✅ |
| ⚪ | BCOS value transfer | unsupported | `enable_balance_transfer` 仅 BCOS | ✅ |
| ⚪ | BCOS CREATE nonce persist | unsupported | `FiscoHostExtension` only | ✅ |

---

## Part 1 汇总表（Task 9 负责行 @ `54e17a62c`）

| 能力 | 清单 # | Matrix OPStack | 深度 | 状态 | FB 实现要点 | FB 测试 | 缺口 |
|------|--------|----------------|------|------|-------------|---------|------|
| EIP-7212 precompile (0x0100) | 11 | unsupported | ⚪ | ✅ | OSAKA+`eip7212` 门控；Isthmus PRAGUE + flag false | ETH `EipPrecompileRevisionGateTest` | OP 否定性测试可选 |
| RevisionConfig `warm_access` | 17 | explicit on Isthmus | profile | ✅ | helper `warm_access=true` | `RevisionConfigProfileTest`；`OpStackTxPropsTest` | — |
| RevisionConfig `eip1559` | 22 | feature-gated (profile-only) | profile-only | ✅ | 未设；无 TE consumer | sparse profile | — |
| RevisionConfig `eip3651` | 23 | feature-gated (profile-only) | profile-only | ✅ | 未设；coinbase 经 `txProps` | sparse profile | — |
| RevisionConfig `prague_post_execution` | 24 | unsupported | ⚪ | ✅ | 显式 `false` | `IsthmusPostExecutionPolicyTest` | — |
| RevisionConfig `eip7823` | 25 | feature-gated (Isthmus 未设) | ⚪ | ✅ | 未设；modexp reject OFF | ETH `Eip7823ModexpRejectTest` | 无 OP Isthmus modexp 7823 否定测试 |
| BCOS fixed 21000 gas debit | ⚪ | unsupported | ⚪ | ✅ | 仅 `ExecuteViaHost` | `Bcos21000GasDeviationTest` | — |
| BCOS auth check | ⚪ | unsupported | ⚪ | ✅ | 仅 BCOS orchestrator | `BcosAuthOrchestratorHookTest` | — |
| BCOS value transfer | ⚪ | unsupported | ⚪ | ✅ | 仅 BCOS | — | — |
| BCOS CREATE nonce persist | ⚪ | unsupported | ⚪ | ✅ | 仅 `FiscoHostExtension` | — | — |

### 状态统计（Task 9 行）

| 符号 | 行数 |
|------|------|
| ✅ | **10** |
| 🟡 | **0** |
| 🔴 | **0** |

---

## 验证清单（Task 9 要求 @ `54e17a62c`）

| 验证项 | 结果 |
|--------|------|
| 7212 unsupported | ✅ `eip7212` 未设；0x0100 需 OSAKA+flag |
| 7823 not set on Isthmus | ✅ helper 未设；modexp reject OFF |
| `prague_post_execution` false | ✅ 显式 false + 测试 |
| BCOS 行 OP 路径不活跃 | ✅ 四行 ⚪ 均无 OP 接线 |
| `warm_access` Isthmus | ✅ 显式 true（OP-09b；#17 由 🟡→✅） |

**Task 9 状态：** **DONE**

---

## 后续动作（非阻断）

1. **P2（可选）：** OP Isthmus 否定性测试——CALL 0x0100 失败、超大 modexp 在 `eip7823=false` 时不拒绝（记录 baseline）。

---

## Wave 3 复审计附录（@ `52dda0921`）

**判定：** ✅ DONE — 无 Wave 3 状态变更；7212/7823/BCOS 钩子仍隔离。

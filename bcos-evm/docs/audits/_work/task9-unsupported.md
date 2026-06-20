# Task 9 — Unsupported / Feature-Gated / ⚪ 行审计笔记

**日期：** 2026-06-20  
**范围：** inventory #11、#17、#22–25 + BCOS ⚪ 四行；`makeIsthmusRevisionConfig()`、OPStack TE 路径未激活确认  
**参考：** `capability-matrix.md` OPStack 列；ADR-004 profile-only 分类；`inventory.md`  
**交叉引用：** Task 8 inherited smoke（#1–6、#10、#12、#14–15、#18–21）；ETH audit 7212/7823 kernel 行

---

## Step 1 — `makeIsthmusRevisionConfig()` 稀疏 profile

**源码：** `bcos-evm/eth/RevisionConfig.h:62-72`

| 字段 | Isthmus helper 赋值 | 默认（未赋值） |
|------|---------------------|----------------|
| `revision` | `EVMC_PRAGUE` | — |
| `eip7623` | `true` | — |
| `eip7702` | `true` | — |
| `eip4844` | `true` | — |
| `prague_post_execution` | `false`（显式） | — |
| `calldata_floor_per_token` | `10` | — |
| `warm_access` | — | `false` |
| `eip2537` | — | `false` |
| `eip7212` | — | `false` |
| `eip7823` | — | `false` |
| `eip1153` / `eip5656` / `eip6780` | — | `false`（evmone 经 `revision`） |
| `eip1559` / `eip3651` | — | `false` |

**测试锚点：**

- `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` — 仅断言上表显式字段
- `IsthmusPostExecutionPolicyTest::isthmus_revision_config_disables_prague_post_execution` — `!prague_post_execution`

**生产注入：** `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()` `input.revisionConfig = makeIsthmusRevisionConfig()`（`OpStackTransactionExecutorImpl.h:197`）

---

## Step 2 — opstack 路径 grep（eip7212 / eip7823 / BCOS 钩子）

```bash
rg -n "eip7212|eip7823|BALANCE_TRANSFER|prague_post" bcos-evm/opstack/ bcos-evm/eth/RevisionConfig.h bcos-evm/bcos/ExecuteViaHost.cpp
```

| 模式 | `bcos-evm/opstack/` | 其他命中 |
|------|---------------------|----------|
| `eip7212` | **0** | 仅 `RevisionConfig.h` 字段定义 |
| `eip7823` | **0** | 仅 `RevisionConfig.h` 字段定义 |
| `BALANCE_TRANSFER` | **0** | `ExecuteViaHost.cpp:259-263`（BCOS 路径） |
| `prague_post_execution` | **0** | `RevisionConfig.h:69` 显式 `false` |

**BCOS-only orchestration grep：**

| 钩子 | BCOS `ExecuteViaHost` | OP `OpStackExecuteViaHost` / `OpHostExtension` |
|------|----------------------|-----------------------------------------------|
| `authChecker` | ✅ `ExecuteViaHost.cpp:219-221` | **无** |
| `enable_balance_transfer` / `maybeTransferValue` | ✅ `:254-256` | **无**（OP 用 `canTransfer` 余额检查，非 BCOS 21000 debit） |
| `BALANCE_TRANSFER_GAS` (21000) | ✅ `:259-263` | **无** |
| `persistContractCreateNonce` | ✅ `:284` → `FiscoHostExtension` | `OpHostExtension::bumpContractCreateNonce` 空实现 |

---

## Step 3 — inventory 行逐项核对

### #11 EIP-7212 precompile (0x0100) — ⚪ unsupported

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| `eip7212` 未设 | `false` | `makeIsthmusRevisionConfig` 未赋值；稀疏 profile 测试未期望 true | ✅ |
| revision 门槛 | PRAGUE（非 OSAKA） | `config.revision = EVMC_PRAGUE` | ✅ |
| 0x0100 不可达 | `revision >= OSAKA && eip7212` | `PrecompileActive.h:44-46` `isP256Precompile` + `isActivePrecompile` | ✅ |
| opstack 无 7212 引用 | 0 命中 | grep opstack | ✅ |
| 测试（ETH/BCOS，非 OP Isthmus） | kernel 在 OSAKA+ 可达 | `Eip7212KernelTest`、`EipPrecompileRevisionGateTest::p256_requires_osaka_and_eip7212` | ✅ 隔离 |

**Part 1 状态：** ✅ — Isthmus OP 路径 0x0100 预编译未激活；与 matrix `unsupported` 一致。

---

### #17 RevisionConfig `warm_access` — profile-only feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile 默认 | ✅ |
| ADR-004 分类 | profile-only（文档称 runtime 应 `rev>=BERLIN`） | ADR-004 §2 消费表 | 📋 |
| 实际 TE 消费者 | `EthHost::access_account/storage`、`warmTransactionEntry`、`executeMessage` 读 `warm_access` | `EthHost.cpp:383-396`；`warmTransactionEntry.h:55-58` | 🟡 见注 |

**注：** `warm_access=false` 时 `warmTransactionEntry` 早退、`EthHost` 恒报 COLD——与 EthPolicy/FiscoPolicy（`rev>=BERLIN` 时设 `true`）行为分裂。属 ADR-004 / Isthmus sparse profile 已知张力；**非 Task 9「意外激活 unsupported 行」范畴**，但 Part 1 标 🟡（profile 与 2929 消费未对齐）。

**Part 1 状态：** 🟡 — 未激活 profile-only 字段 ✅；OP Isthmus 上 2929 warm 语义可能偏弱（Task 8 inherited 行交叉引用）。

---

### #22 RevisionConfig `eip1559` — profile-only feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| TE `revisionConfig.eip1559` 消费者 | 无 | grep 仅 `FiscoPolicy.h` 赋值 + 测试 `CanTransferTest` 手工设 flag | ✅ |
| OP 1559 **orchestration** | 独立路径 | `OpStackPreCheck` gas cap 校验；`DepositTxPreCheckTest::non_deposit_rejects_invalid_eip1559_caps` | ✅ 非 profile 字段 |

**Part 1 状态：** ✅ — profile-only 字段未设、无 TE consumer；1559 交易形状由 orchestration 处理。

---

### #23 RevisionConfig `eip3651` — profile-only feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| TE consumer | 无 | grep 仅 `RevisionConfig.h` + profile 测试 | ✅ |
| coinbase warm 实际路径 | `txProps.warmCoinbase` + `rev>=SHANGHAI` | matrix 脚注；`warmTransactionEntry.h:67-70`（需 `warm_access=true` 才执行） | 📋 与 #17 同张力 |

**Part 1 状态：** ✅ — profile-only 未激活；coinbase warm 不经 `eip3651` flag。

---

### #24 RevisionConfig `prague_post_execution` — ⚪ unsupported

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus 显式 false | `false` | `RevisionConfig.h:69`；`IsthmusPostExecutionPolicyTest` | ✅ |
| TE consumer | 无 | grep 仅定义 + 测试 | ✅ |
| matrix 语义 | OP unsupported（Isthmus 恒关） | `capability-matrix.md` 行 72 | ✅ |

**Part 1 状态：** ✅ — 显式 false；无 post-execution Prague 钩子接线。

---

### #25 RevisionConfig `eip7823` — ⚪ Isthmus feature-gated

| 检查项 | 期望 | 证据 | 判定 |
|--------|------|------|------|
| Isthmus helper 未设 | `false` | 稀疏 profile | ✅ |
| modexp reject 门控 | `modexpEip7823Enabled` → `rev.eip7823` | `ModexpGas.h:30-32`；`EthPrecompiles.cpp:569` | ✅ |
| `eip7823=false` 时 0x05 | 不执行 EIP-7823 长度拒绝 | `shouldRejectModexpEip7823` 首行 `!modexpEip7823Enabled` → false | ✅ |
| opstack 无 7823 引用 | 0 命中 | grep opstack | ✅ |
| OSAKA+ 对照 | EthPolicy `eip7823=true` | `EthPolicy.h:40`；`Eip7823ModexpRejectTest` | ✅ ETH 路径隔离 |

**Part 1 状态：** ✅ — Isthmus 未设 flag；0x05 modexp 无 7823 拒绝；与 matrix `feature-gated (not set on Isthmus helper)` 一致。

---

## Step 4 — BCOS ⚪ 行（OP 路径未激活）

| inventory | 能力 | matrix OPStack | 验证 | 判定 |
|-----------|------|----------------|------|------|
| ⚪ | BCOS fixed 21000 gas debit | unsupported | 仅 `ExecuteViaHost.cpp` `BALANCE_TRANSFER_GAS`；`OpStackExecuteViaHost` 无等价逻辑 | ✅ |
| ⚪ | BCOS auth check | unsupported | `authChecker` 仅 BCOS input；OP executor 未注入 | ✅ |
| ⚪ | BCOS value transfer | unsupported | `enable_balance_transfer` / `maybeTransferValue` 仅 BCOS；OP 用标准 `canTransfer` | ✅ |
| ⚪ | BCOS CREATE nonce persist | unsupported | `persistContractCreateNonce` 仅 BCOS；`OpHostExtension::bumpContractCreateNonce` 空 override | ✅ |

**共享内核：** OP 路径经 `executeMessage` + `OpHostExtension`，无 `FiscoHostExtension` / `ExecuteViaHost` BCOS orchestration 层。

---

## Part 1 汇总表（Task 9 负责行）

| 能力 | 清单 # | Matrix OPStack | 深度 | 状态 | FB 实现要点 | FB 测试 | 缺口 |
|------|--------|----------------|------|------|-------------|---------|------|
| EIP-7212 precompile (0x0100) | 11 | unsupported | ⚪ | ✅ | `PrecompileActive.h` OSAKA+`eip7212`；Isthmus PRAGUE + flag false | `EipPrecompileRevisionGateTest`（ETH）；无 OP Isthmus 用例 | OP 路径否定性测试可选 |
| RevisionConfig `warm_access` | 17 | feature-gated (profile-only) | profile-only | 🟡 | 未设；ADR-004 profile-only；EthHost 仍读 flag | `RevisionConfigProfileTest` sparse | Isthmus `warm_access=false` vs 2929 inherited 张力 |
| RevisionConfig `eip1559` | 22 | feature-gated (profile-only) | profile-only | ✅ | 未设；无 TE consumer | sparse profile | — |
| RevisionConfig `eip3651` | 23 | feature-gated (profile-only) | profile-only | ✅ | 未设；coinbase warm 经 `txProps` | sparse profile | 与 #17 同 warm 路径张力 |
| RevisionConfig `prague_post_execution` | 24 | unsupported | ⚪ | ✅ | 显式 `false` | `IsthmusPostExecutionPolicyTest` | — |
| RevisionConfig `eip7823` | 25 | feature-gated (Isthmus 未设) | ⚪ | ✅ | 未设；`shouldRejectModexpEip7823` 门控 OFF | `RevisionConfigProfileTest`；ETH `Eip7823ModexpRejectTest` | 无 OP Isthmus modexp 7823 否定测试 |
| BCOS fixed 21000 gas debit | ⚪ | unsupported | ⚪ | ✅ | 仅 `ExecuteViaHost` | `Bcos21000GasDeviationTest`（BCOS） | — |
| BCOS auth check | ⚪ | unsupported | ⚪ | ✅ | 仅 BCOS orchestrator | `BcosAuthOrchestratorHookTest` | — |
| BCOS value transfer | ⚪ | unsupported | ⚪ | ✅ | 仅 BCOS `enable_balance_transfer` | — | — |
| BCOS CREATE nonce persist | ⚪ | unsupported | ⚪ | ✅ | 仅 `FiscoHostExtension` | — | — |

### 状态统计（Task 9 行）

| 符号 | 行数 |
|------|------|
| ✅ | 8 |
| 🟡 | 1（#17 warm_access / 2929 张力） |
| 🔴 | 0 |
| 📋 | 0 |

---

## 验证清单（Task 9 要求）

| 验证项 | 结果 |
|--------|------|
| 7212 unsupported | ✅ `eip7212` 未设；0x0100 需 OSAKA+flag |
| 7823 not set on Isthmus | ✅ helper 未设；modexp reject OFF |
| `prague_post_execution` false | ✅ 显式 false + 测试 |
| profile-only 字段 ADR-004 | ✅ `eip1559`/`eip3651`/`prague_post_execution` 无 consumer；`warm_access`/`eip7823` 有 consumer 但 Isthmus 未激活 flag |
| BCOS 行 OP 路径不活跃 | ✅ 四行 ⚪ 均无 OP 接线 |

---

## 后续动作（非阻断）

1. **P1（🟡）：** 评估 Isthmus helper 是否应设 `warm_access=true`（与 EthPolicy PRAGUE 对齐），或 ADR-004 改为「consumed」并更新 matrix #17。
2. **P2（可选）：** OP Isthmus 否定性测试——CALL 0x0100 失败、超大 modexp 在 `eip7823=false` 时不拒绝（记录 baseline）。

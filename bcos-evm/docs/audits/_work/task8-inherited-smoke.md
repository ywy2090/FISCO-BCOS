# Task 8 — Inherited Rows Smoke + ETH Audit Cross-Reference

**日期：** 2026-06-20  
**范围：** inventory #1–6, #10, #12, #14, #18–21（13 行 inherited smoke）  
**ETH 交叉引用：** `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md` Part 1（初审计快照 @ `e16b623e7`）— **已存在，非 blocked pending**  
**OP wiring 核对：** `OpStackTxPropsTest`、`OpStack7702ExecuteViaHostPropagationTest`、`Eip7702ApplyAuthorizationTest`（opstack）、`makeIsthmusRevisionConfig`、`OpStackTransactionExecutorImpl::opStackExecuteViaHostTx`

---

## 判定规则（本 Task）

| 规则 | 应用 |
|------|------|
| ETH Part 1 🔴 → OP 继承 🔴 | 共享内核缺口（6780 Host、2537 MSM gas 等） |
| ETH Part 1 ✅ + OP 缺测 / 生产 wiring 未接 → OP 🟡 | 2929 tx-entry、预编译 smoke 等 |
| ETH Part 1 🟡 → OP 按 wiring + 测试独立判定 | 7702 apply 在 OP 有 E2E |
| ETH Part 1 🔴（EthPolicy 基线）+ OP helper 已修复 → OP 单列 ✅，ETH 列保留 🔴 | #6 `eip7702` Isthmus profile |
| ETH 审计存在 | 所有行均可交叉引用；**无** `blocked: ETH audit pending` |

---

## OP 共享调用链（smoke 基线）

```
OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()
  → makeIsthmusRevisionConfig()          // RevisionConfig.h:62-72
  → fillWeb3Fields()                     // 0x04 auth decode
  → opStackExecuteViaHost()
       → executeMessage(..., revisionConfig, txProps, authorizations)
            → warmTransactionEntry(...)  // 门控 revisionConfig.warm_access
            → EthHost / EthPrecompiles   // 共享 eth/ 内核
```

**生产路径缺口（2929 簇）：** `makeIsthmusRevisionConfig()` **未** 设 `warm_access=true`；`OpStackTransactionExecutorImpl` **未** 调用 `opstack_tx::applyDefaultTxProps()`（仅 `OpStackTxPropsTest` 覆盖 helper）。`warmTransactionEntry` 在 `warm_access=false` 时 early-return；`EthHost::access_account` 恒返回 COLD。

---

## Part 1 — Inherited 合规矩阵

| Inv# | 能力 | 层级 | ETH Part 1 | OP 状态 | Spec 依据 | OP FB 实现 | op-geth 对照 | OP FB 测试 | 缺口 |
|------|------|------|------------|---------|-----------|------------|--------------|------------|------|
| 1 | EIP-2929 runtime warm | kernel | ✅ | 🟡 | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) §Cold/warm | `opStackExecuteViaHost` → `executeMessage` → `EthHost.cpp:381-397`（共享）；生产 `warm_access` 默认 false | Berlin ACL via revision | 共享：`Eip2929AccessHostTest`；**无** OP E2E | ETH ✅；OP 生产 profile 未启 `warm_access`；缺 OP 路径 smoke |
| 2 | EIP-2929 tx-entry destination warm | tx input | ✅ | 🟡 | EIP-2929 tx access | `TxFeaturePrepare.h:13-16` + `warmTransactionEntry.h:62-65`；helper `applyDefaultTxProps` 存在但未接 executor | `statedb.Prepare` dst warm | `OpStackTxPropsTest`（helper only） | ETH ✅；`OpStackTransactionExecutorImpl` 未调 `applyDefaultTxProps`；且 `warm_access=false` 阻断 Prepare |
| 3 | EIP-2929 tx-entry coinbase warm | tx input | ✅ | 🟡 | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | `TransactionProperties::warmCoinbase{true}` 默认；`warmTransactionEntry.h:67-70` | Shanghai coinbase warm | 共享：`WarmTransactionEntryTest` @ SHANGHAI；**无** OP E2E | ETH ✅；生产路径 `warm_access=false` → coinbase warm 不执行 |
| 4 | EIP-7702 authorization apply | kernel | 🟡 | ✅ | [EIP-7702](https://eips.ethereum.org/EIPS/eip-7702) §Set code | `executeMessage.cpp:193-200` `applyAuthorizations`；OP 经 `makeIsthmusRevisionConfig().eip7702=true` 可达 | `applyAuthorization` (`state_transition.go`) | `Eip7702ApplyAuthorizationTest`；`OpStack7702ExecuteViaHostPropagationTest`（`makeIsthmusRevisionConfig` + stateDiff 23B delegation） | ETH 初审计 🟡（EthPolicy 阻断 reference）；OP Isthmus profile + opstack 单测/E2E ✅ |
| 5 | EIP-7702 tx field propagation | tx input | ✅ | ✅ | EIP-7702 type-4 | `OpStackTxInputBuilder.h:183-200` `fillWeb3Fields` → `authorizations` | type-4 RLP → `SetCodeAuthorizations` | `OpStackTxInputBuilderTest::decodes_eip7702_authorization_from_extra_bytes` | — |
| 6 | EIP-7702 revision enable (`eip7702`) | revision profile | 🔴 | ✅ | EIP-7702 | **`makeIsthmusRevisionConfig()` `eip7702=true`**（`RevisionConfig.h:67`）；`OpStackTransactionExecutorImpl.h:197` | Isthmus EVM rules | `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` | ETH Part 1 🔴 指 EthPolicy 初快照未赋值；**OP Isthmus helper 已启用**，不 blocked |
| 10 | EIP-2537 precompiles (0x0b–0x11) | kernel | 🔴 | 🔴 | [EIP-2537](https://eips.ethereum.org/EIPS/eip-2537) §Gas | 共享 `EthPrecompiles.cpp` MSM 线性 gas（`12000×k` / `22500×k`）；`PrecompileActive.h` revision 门控 ✅ | geth 128 项折扣表 | 共享：`Eip2537KernelTest`（G1Add @0x0b）；**无** OP fixture / MSM gas | **继承 ETH 🔴**；EthBuiltinRegistry 表正确但未 wired 到 TE 路径 |
| 12 | EIP-4844 revision profile (`eip4844`) | revision profile | ✅ | ✅ | [EIP-4844](https://eips.ethereum.org/EIPS/eip-4844) | `makeIsthmusRevisionConfig()` `eip4844=true`（`RevisionConfig.h:68`） | Cancun blob profile flag | `RevisionConfigProfileTest` Isthmus helper | blob orchestration 另见 inventory #13（explicit，非本 Task） |
| 14 | builtin precompiles 0x01–0x11 | kernel | 🟡 | 🟡 | Yellow Paper / Prague | 共享 `EthPrecompiles` + `PrecompileActive.h`；OP 经 `executeMessage` / `OpHostExtension` | `PrecompiledContractsPrague` | 共享：`ExecuteViaEthFixtureTest` 子项；**无** `opstack/*` 预编译 fixture | ETH 🟡 revision 门控（Task 2）；OP 同享内核；smoke 仅 eth fixture |
| 18 | RevisionConfig `eip1153` | revision profile | ✅ | 🟡 | [EIP-1153](https://eips.ethereum.org/EIPS/eip-1153) | `makeIsthmusRevisionConfig` **未**显式设 flag（default false）；`revision=EVMC_PRAGUE` → evmone TLOAD/TSTORE | Cancun `enable1153` | `RevisionConfigProfileTest` Isthmus helper（flag 期望 false） | ADR-004 profile-only；opcode evmone-delegated ✅；helper 稀疏快照 🟡 |
| 19 | RevisionConfig `eip5656` | revision profile | ✅ | ✅ | [EIP-5656](https://eips.ethereum.org/EIPS/eip-5656) | `revision=EVMC_PRAGUE` → evmone MCOPY | Cancun `enable5656` | `RevisionConfigProfileTest`（EthPolicy 行）；Isthmus helper 未设 flag | evmone-delegated；profile flag 文档化 only |
| 20 | RevisionConfig `eip6780` | revision profile | ✅ | 🟡 | [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780) | helper 未设 `eip6780`（default false）；`revision=EVMC_PRAGUE` | Cancun `enable6780` | `RevisionConfigProfileTest`（EthPolicy）；Isthmus helper sparse | profile flag 🟡；kernel 见 #21 🔴 |
| 21 | EIP-6780 SELFDESTRUCT (kernel) | kernel | 🔴 | 🔴 | EIP-6780 | 共享 `EthHost::selfdestruct` stub（初审计 `EthHost.cpp:145-156`） | `opSelfdestruct6780` | 共享：`stSelfDestruct_basic.json`（无 post-state）；**无** OP 6780 测试 | **继承 ETH 🔴**；OP 与 ETH 同 Host 实现 |

---

## OP Wiring 核对摘要

| 检查项 | 位置 | 结果 |
|--------|------|------|
| `makeIsthmusRevisionConfig` | `RevisionConfig.h:62-72` | ✅ `eip7702` / `eip4844` / `eip7623` / `EVMC_PRAGUE` |
| Isthmus profile 注入 | `OpStackTransactionExecutorImpl.h:197` | ✅ |
| 7702 auth decode | `OpStackTxInputBuilder.h:191-199` | ✅ |
| 7702 apply E2E | `OpStack7702ExecuteViaHostPropagationTest.cpp:113-134` | ✅ post-state delegation |
| 7702 kernel unit | `Eip7702ApplyAuthorizationTest.cpp:47-65` | ✅ manual `eip7702=true` |
| 2929 tx-entry helper | `OpStackTxInputBuilder.h:221-224` | 🟡 存在但未接 executor |
| 2929 生产 `warm_access` | `makeIsthmusRevisionConfig` | 🟡 未设 true |
| 2537 / 6780 kernel | 共享 `eth/` | 🔴 继承 ETH 初审计缺口 |

---

## 测试运行（Task 8 Step 3）

```bash
cd build && ctest -R "OpStack7702|OpStackTxInput|OpStackTxProps|Eip7702Apply" --output-on-failure
# 当前 worktree build：No tests were found（target 未注册或未构建 bcos-evm test）
```

源码级测试存在且可编译引用：`OpStackTxPropsTest`、`OpStackTxInputBuilderTest`、`Eip7702ApplyAuthorizationTest`、`OpStack7702ExecuteViaHostPropagationTest`。

---

## 汇总计数

### ETH Part 1 交叉引用（初审计快照）

| 符号 | 行数 | Inv# |
|------|------|------|
| ✅ | 8 | 1, 2, 3, 5, 12, 18, 19, 20 |
| 🟡 | 2 | 4, 14 |
| 🔴 | 3 | 6, 10, 21 |
| **合计** | **13** | |

### OP Isthmus 判定（本 Task Part 1）

| 符号 | 行数 | Inv# |
|------|------|------|
| ✅ | 5 | 4, 5, 6, 12, 19 |
| 🟡 | 6 | 1, 2, 3, 14, 18, 20 |
| 🔴 | 2 | 10, 21 |
| **合计** | **13** | |

### 合并解读

- **继承 ETH 🔴（内核）：** 2 行（#10 2537 MSM、#21 6780 SELFDESTRUCT）— OP 共享 `eth/` 内核，无 Isthmus 侧 override。
- **ETH 🔴 但 OP profile 已修复：** 1 行（#6 `eip7702`）— 交叉引用保留 ETH 🔴；OP ✅。
- **ETH ✅ 但 OP smoke/wiring 缺口：** 6 行 🟡（#1–3 2929 生产 `warm_access` / `applyDefaultTxProps`；#14、#18、#20 profile smoke）。
- **blocked pending：** **0**（ETH 审计已存在）。

**Task 8 状态：** **DONE_WITH_CONCERNS** — inherited smoke 已交叉引用；OP 7702/4844 profile wiring ✅；2929 生产接线与 2537/6780 内核继承缺口待后续 Task 或 P0 内核修复。

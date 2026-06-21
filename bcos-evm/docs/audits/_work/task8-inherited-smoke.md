# Task 8 — Inherited Rows Smoke + ETH Audit Cross-Reference

**初审计日期：** 2026-06-20  
**复审计 commit：** `54e17a62c`（2026-06-21）  
**范围：** inventory #1–6, #10, #12, #14, #18–21（13 行 inherited smoke）  
**ETH 交叉引用：** `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit-reaudit.md` @ P0 闭合（**0×🔴**；6780/2537/7702 profile 已修复）  
**OP wiring 核对：** `OpStack67802537KernelSmokeTest`、`OpStackTxPropsTest`、`OpStack7702ExecuteViaHostPropagationTest`、`RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields`、`OpStackTransactionExecutorImpl::opStackExecuteViaHostTx`

---

## 判定规则（本 Task）

| 规则 | 应用 |
|------|------|
| ETH Part 1 🔴 → OP 继承 🔴 | 共享内核缺口（6780 Host、2537 MSM gas 等） |
| ETH Part 1 ✅ + OP 缺测 / 生产 wiring 未接 → OP 🟡 | 2929 tx-entry、预编译 smoke 等 |
| ETH Part 1 🟡 → OP 按 wiring + 测试独立判定 | 7702 apply 在 OP 有 E2E |
| ETH Part 1 🔴（EthPolicy 基线）+ OP helper 已修复 → OP 单列 ✅，ETH 列保留初审计 🔴 | #6 初快照；**复审计 ETH 亦 ✅** |
| ETH 审计存在 | 所有行均可交叉引用；**无** `blocked: ETH audit pending` |

---

## OP 共享调用链（smoke 基线 @ `54e17a62c`）

```
OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()
  → makeIsthmusRevisionConfig()          // RevisionConfig.h:62-72 — warm_access=true
  → fillWeb3Fields()                     // 0x04 auth decode
  → applyDefaultTxProps()                // tx-entry warm destination
  → opStackExecuteViaHost()
       → isIsthmusOrchestrationProfile → m_isIsthmus=true
       → executeMessage(..., revisionConfig, txProps, authorizations)
            → warmTransactionEntry(...)  // revisionConfig.warm_access=true
            → EthHost / EthPrecompiles   // 共享 eth/ 内核（P0 已闭合）
```

**OP-08 / OP-09 闭合（work-list）：** 2537/6780 经共享内核验证 + `OpStack67802537KernelSmokeTest`；2929 经 `warm_access=true` + executor `applyDefaultTxProps` + `isIsthmusOrchestrationProfile` 自动接线。

---

## Part 1 — Inherited 合规矩阵（复审计 @ `54e17a62c`）

| Inv# | 能力 | 层级 | ETH 复审计 | OP 状态 | Spec 依据 | OP FB 实现 | op-geth 对照 | OP FB 测试 | 缺口 |
|------|------|------|------------|---------|-----------|------------|--------------|------------|------|
| 1 | EIP-2929 runtime warm | kernel | ✅ | ✅ | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) §Cold/warm | `makeIsthmusRevisionConfig` `warm_access=true`（`:66`）；`EthHost::access_account` 读 flag | Berlin ACL via revision | 共享 `Eip2929OpcodeGasTest`；OP Isthmus profile `OpStackTxPropsTest::isthmus_revision_profile_enables_warm_access` | — |
| 2 | EIP-2929 tx-entry destination warm | tx input | ✅ | ✅ | EIP-2929 tx access | `OpStackTransactionExecutorImpl.h:206` `applyDefaultTxProps`；`warmTransactionEntry.h:62-65` | `statedb.Prepare` dst warm | `OpStackTxPropsTest`；TE `executor_input_build_applies_warm_destination_for_call` | — |
| 3 | EIP-2929 tx-entry coinbase warm | tx input | ✅ | ✅ | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | `TransactionProperties::warmCoinbase{true}` + `warm_access=true` | Shanghai coinbase warm | 共享 `WarmTransactionEntryTest`；Isthmus profile 启用 warm | — |
| 4 | EIP-7702 authorization apply | kernel | ✅ | ✅ | [EIP-7702](https://eips.ethereum.org/EIPS/eip-7702) §Set code | `executeMessage.cpp` `applyAuthorizations`；Isthmus `eip7702=true` | `applyAuthorization` (`state_transition.go`) | `Eip7702ApplyAuthorizationTest`；`OpStack7702ExecuteViaHostPropagationTest` | — |
| 5 | EIP-7702 tx field propagation | tx input | ✅ | ✅ | EIP-7702 type-4 | `OpStackTxInputBuilder.h` `fillWeb3Fields` → `authorizations` | type-4 RLP → `SetCodeAuthorizations` | `OpStackTxInputBuilderTest::decodes_eip7702_authorization_from_extra_bytes` | — |
| 6 | EIP-7702 revision enable (`eip7702`) | revision profile | ✅ | ✅ | EIP-7702 | `makeIsthmusRevisionConfig()` `eip7702=true`（`:68`） | Isthmus EVM rules | `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` | 初审计 ETH 🔴 已闭合 |
| 10 | EIP-2537 precompiles (0x0b–0x11) | kernel | ✅ | ✅ | [EIP-2537](https://eips.ethereum.org/EIPS/eip-2537) §Gas | 共享 `BlsGas.h` + `EthPrecompiles` 128 项折扣；`PrecompileActive.h` `rev>=PRAGUE` | geth 128 项折扣表 | 共享 `Eip2537KernelTest`；**OP** `OpStack67802537KernelSmokeTest::g1msm_k2_gas_matches_geth_isthmus`（22776） | — |
| 12 | EIP-4844 revision profile (`eip4844`) | revision profile | ✅ | ✅ | [EIP-4844](https://eips.ethereum.org/EIPS/eip-4844) | `makeIsthmusRevisionConfig()` `eip4844=true`（`:69`） | Cancun blob profile flag | `RevisionConfigProfileTest` Isthmus helper | blob orchestration 另见 inventory #13 |
| 14 | builtin precompiles 0x01–0x11 | kernel | 🟡 | 🟡 | Yellow Paper / Prague | 共享 `EthPrecompiles` + `PrecompileActive.h`；OP 经 `executeMessage` / `OpHostExtension` | `PrecompiledContractsPrague` | 共享 `ExecuteViaEthFixtureTest` 子项；OP 6780/2537 smoke only | 无 OP 0x01–0x0a 专项 fixture |
| 18 | RevisionConfig `eip1153` | revision profile | ✅ | 🟡 | [EIP-1153](https://eips.ethereum.org/EIPS/eip-1153) | helper 未设 flag（default false）；`revision=EVMC_PRAGUE` → evmone TLOAD/TSTORE | Cancun `enable1153` | `RevisionConfigProfileTest` Isthmus helper（flag 期望 false） | ADR-004 profile-only；opcode evmone-delegated ✅ |
| 19 | RevisionConfig `eip5656` | revision profile | ✅ | ✅ | [EIP-5656](https://eips.ethereum.org/EIPS/eip-5656) | `revision=EVMC_PRAGUE` → evmone MCOPY | Cancun `enable5656` | `RevisionConfigProfileTest` | evmone-delegated |
| 20 | RevisionConfig `eip6780` | revision profile | ✅ | 🟡 | [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780) | helper 未设 `eip6780`（default false）；kernel 经 `revision>=CANCUN` | Cancun `enable6780` | `RevisionConfigProfileTest`；OP 6780 smoke 测 kernel 非 flag | profile flag 🟡；kernel ✅（#21） |
| 21 | EIP-6780 SELFDESTRUCT (kernel) | kernel | ✅ | ✅ | EIP-6780 | 共享 `EthHost::selfdestruct`（`:166-201`）same-tx CREATE 清除 | `opSelfdestruct6780` | 共享 `Bcos6780SelfdestructTest`；**OP** `OpStack67802537KernelSmokeTest::created_in_tx_selfdestruct_clears_code_isthmus` | — |

---

## OP Wiring 核对摘要（@ `54e17a62c`）

| 检查项 | 位置 | 结果 |
|--------|------|------|
| `makeIsthmusRevisionConfig` | `RevisionConfig.h:62-72` | ✅ `warm_access=true`；`eip7702` / `eip4844` / `eip7623` / `EVMC_PRAGUE` |
| Isthmus profile 注入 | `OpStackTransactionExecutorImpl.h:197` | ✅ |
| 2929 tx-entry helper | `OpStackTransactionExecutorImpl.h:206` | ✅ `applyDefaultTxProps` 已接 executor |
| Isthmus operator / warm 自动启用 | `OpStackExecuteViaHost.cpp:79-82` | ✅ `isIsthmusOrchestrationProfile` → `m_isIsthmus=true` |
| 7702 auth decode | `OpStackTxInputBuilder.h` `fillWeb3Fields` | ✅ |
| 7702 apply E2E | `OpStack7702ExecuteViaHostPropagationTest` | ✅ post-state delegation + intrinsic 25000×n |
| 2537 / 6780 kernel OP smoke | `OpStack67802537KernelSmokeTest.cpp` | ✅ G1MSM k=2 gas 22776；same-tx SELFDESTRUCT 清 code |

---

## 测试锚点（OP-08 / OP-09）

| 测试 | 覆盖 |
|------|------|
| `OpStack67802537KernelSmokeTest` | OP-08：2537 MSM gas + 6780 same-tx selfdestruct via Isthmus `makeIsthmusRevisionConfig` |
| `OpStackTxPropsTest` | OP-09：`applyDefaultTxProps`；Isthmus `warm_access=true` |
| `TestOpStackTransactionExecutorFixture` | OP-09 E2E：`executor_input_build_*_warm_destination` |
| `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` | profile 字面量含 `warm_access=true` |

---

## 汇总计数

### ETH 复审计交叉引用（`2026-06-20-eth-reference-cancun-plus-audit-reaudit.md`）

| 符号 | 行数 | Inv# |
|------|------|------|
| ✅ | 11 | 1–6, 10, 12, 19, 21 |
| 🟡 | 2 | 14, 18/20（profile 文档化） |
| 🔴 | 0 | — |
| **合计** | **13** | |

### OP Isthmus 判定（本 Task Part 1 @ `54e17a62c`）

| 符号 | 行数 | Inv# |
|------|------|------|
| ✅ | 10 | 1, 2, 3, 4, 5, 6, 10, 12, 19, 21 |
| 🟡 | 3 | 14, 18, 20 |
| 🔴 | 0 | — |
| **合计** | **13** | |

### 合并解读

- **继承 ETH 🔴（内核）：** **0** — P0 闭合后 OP 共享 `eth/` 无 Isthmus override 缺口。
- **ETH ✅ + OP smoke 仍偏弱：** 3 行 🟡（#14 预编译 fixture 深度；#18/#20 profile flag 稀疏文档化）。
- **OP-08 / OP-09：** **DONE** — `OpStack67802537KernelSmokeTest` + warm_access/applyDefaultTxProps 生产接线 ✅。

**Task 8 状态：** **DONE** — inherited smoke 已交叉引用 ETH 复审计；2537/6780/2929 OP 路径验证闭合。

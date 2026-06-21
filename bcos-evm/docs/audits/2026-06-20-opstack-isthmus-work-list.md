# OPStack Isthmus 审计 — Remediation 工作列表

**审计报告：** [`2026-06-20-opstack-isthmus-audit.md`](2026-06-20-opstack-isthmus-audit.md)  
**设计 spec：** [`docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-remediation-design.md`](../../../docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-remediation-design.md)  
**执行细则：** [`docs/superpowers/plans/2026-06-20-opstack-isthmus-fix-plan.md`](../../../docs/superpowers/plans/2026-06-20-opstack-isthmus-fix-plan.md)  
**Normative matrix：** [`capability-matrix.md`](../../capability-matrix.md)  
**并行 tracker：** [`inheritance-work-tracker.md`](inheritance-work-tracker.md)

**Done 规则：** 每项 = 代码/fix + 验收测试 PASS + matrix（若勾选）→ 方可 `[x]`。

Legend: `[x]` done · `[~]` partial · `[ ]` open

---

## 进度摘要

| 优先级 | 总数 | 完成 | 进行中 | 待办 |
|--------|------|------|--------|------|
| P0 | 9 | 9 | 0 | 0 |
| P1 | 6 | 6 | 0 | 0 |

**目标：** P0 全部 `[x]` 后复跑 Isthmus 审计 → 主判定 ≥ ⚠️

---

## P0 — 必须先闭合

| ID | 轨道 | 标题 | 审计引用 | 主要文件 | 验收测试 | fix | test | matrix | 状态 |
|----|------|------|----------|----------|----------|-----|------|--------|------|
| OP-01 | orchestration | Isthmus operator fee 生产接线 `m_isIsthmus=true` | W1, S3 | `OpStackTransactionExecutorImpl.h` | `TestOpStackTransactionExecutorFixture` | ✓ | ✓ | — | [x] |
| OP-02 | orchestration | Rollup L1 cost 用 signed RLP tx 字节 | R1 | `OpStackTxInputBuilder.h`, `Web3SignedTxEncoder.h` | `OpStackTxInputBuilderTest` + `OpStackFeeTest` | ✓ | ✓ | ✓ | [x] |
| OP-03 | orchestration | Deposit 成功路径 sender nonce +1 | task4 成功 | `OpStackExecuteViaHost.cpp` | `DepositMintTest` | ✓ | ✓ | — | [x] |
| OP-04 | orchestration | Deposit EVM REVERT → actual gasUsed | D1 | `OpStackExecuteViaHost.cpp` | `DepositNoFeeRoutingTest` 修正 | ✓ | ✓ | — | [x] |
| OP-05 | orchestration | Deposit entry 失败 nonce+1 + gasLimit | D2 | `OpStackExecuteViaHost.cpp` | entry-failure 新用例 | ✓ | ✓ | — | [x] |
| OP-06 | orchestration | Blob buyGas 扣款 + type-0x03 字段传播 | B1 | `OpStackTxExecutor.cpp`, `OpStackTxInputBuilder.h` | `BlobGasBalanceTest` | ✓ | ✓ | — | [x] |
| OP-07 | orchestration | 7702 intrinsic 25000×n + existence refund | A1 | `OpStackExecuteViaHost.cpp`, `Eip7702.h` | `OpStack7702ExecuteViaHostPropagationTest` | ✓ | ✓ | — | [x] |
| OP-08 | kernel-verify | OP 路径验证 6780 + 2537（ETH P0 已闭合） | task8 #10,#21 | 共享内核（只读验证） | Isthmus smoke + `Eip2537KernelTest` | — | ✓ | — | [x] |
| OP-09 | kernel-verify | OP 路径 2929 tx-entry（warm_access / txProps） | task8 #1-3 | `RevisionConfig.h`, `OpStackTransactionExecutorImpl.h` | `OpStackTxPropsTest` + executor E2E | ✓ | ✓ | — | [x] |

**OP-09b（2026-06-20）：** 决策 A — `makeIsthmusRevisionConfig()` 现设 `warm_access=true`（对齐 `EthPolicy` Berlin+），启用 tx-entry warm 与 `EthHost` runtime 2929。

---

## P1 — Matrix 与质量

| ID | 轨道 | 标题 | 审计引用 | 主要文件 | 验收 | fix | test | matrix | 状态 |
|----|------|------|----------|----------|------|-----|------|--------|------|
| OP-10 | docs-matrix | capability-matrix 增补 4 行 | Part 4 patch | `capability-matrix.md` | CI `check-capability-matrix.sh` | — | — | ✓ | [x] |
| OP-11 | quality | Fjord min-bound / parity 向量 | task2 | `OpStackFeeTest` | op-geth 对照用例 | ✓ | ✓ | — | [x] |
| OP-12 | quality | L1 attributes 精确 fee literal | task5 | `L1AttributesDepositTest` | literal 断言 | ✓ | ✓ | — | [x] |
| OP-13 | quality | Receipt operatorFeeScalar/Constant | task3 🟡 | `OpStackReceiptMeta.h` | meta 断言（协议层 known gap） | ✓ | ✓ | — | [x] |
| OP-14 | quality | L1Block 元数据 slot 完整仿真 | task5 D5-1 | `L1BlockStorage.cpp`, `L1BlockPredeploy.cpp` | `L1BlockPredeployTest`, `L1BlockGetterTest` | ✓ | ✓ | — | [x] |
| OP-15 | quality | 清理测试冗余 `m_isIsthmus=true` | task10 | opstack 测试 | `isIsthmusOrchestrationProfile` 自动启用 | ✓ | ✓ | — | [x] |

**OP-14 备注（2026-06-20 闭合）：** `setL1BlockValuesIsthmus` 写入 Ecotone metadata slots（0/2/4/5/6/9）+ fee slots（1/3/7/8）；`IL1Block` 全量 view/pure getter + `l1BaseFee`/`l1BlobBaseFee` legacy alias + `isFeatureEnabled` mapping。**Out of scope：** GPO predeploy `0x4200…000F`、`setFeature`、`proxyAdmin*`、Bedrock/Jovian setter、`setIsthmus()` 升级迁移。计费路径（`loadOpStackFeeParams` slot 1/3/7/8）未改动。

---

## Matrix 增补预览（OP-10）

| 能力 | Layer | OPStack 列 | Test ref |
|------|-------|------------|----------|
| OPStack operator fee (Isthmus) | orchestration | explicit | `RefundIsthmusTest`, executor E2E |
| L1 attributes system deposit | orchestration | explicit | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest` |
| Isthmus executor integration | executor-integration | explicit | `TestOpStackTransactionExecutorFixture` |
| Rollup L1 cost tx bytes (Fjord) | executor-integration | explicit | `OpStackFeeTest` + signed-tx E2E |

---

## 执行阶段（推荐顺序）

1. **OP-01** → 解锁 operator fee 全链  
2. **OP-02, OP-06, OP-07** → tx 费用模型  
3. **OP-03, OP-04, OP-05** → deposit  
4. **OP-08, OP-09** → kernel verify  
5. **OP-10** + **OP-11–15**

---

**Last updated:** 2026-06-20

### OP-08 verification note (2026-06-20)

- **Status:** DONE — no OP wiring changes required.
- **Shared kernel:** `EthHost::selfdestruct` gates on `revision >= EVMC_CANCUN`; EIP-2537 MSM gas via `BlsGas.h` + `EthPrecompiles::dispatch`; `PrecompileActive.h` gates 0x0b–0x11 on `revision >= EVMC_PRAGUE`.
- **Isthmus profile:** `makeIsthmusRevisionConfig()` sets `revision=EVMC_PRAGUE` (sparse flags: `eip6780`/`eip2537` profile bits default false; kernel uses revision gates, not those flags).
- **OP path:** `opStackExecuteViaHost` → `executeMessage(..., revisionConfig, extension=OpHostExtension)` — `OpHostExtension` only intercepts L1Block predeploy; eth precompiles unchanged.
- **Tests PASS:** `Eip2537KernelTest` (2), `Bcos6780SelfdestructTest` (2), `Bcos2537MsmGasTest` (+ Isthmus profile case), `RevisionConfigProfileTest`, `OpStack7702ExecuteViaHostPropagationTest`, `OpStackExecuteViaHostSmokeTest`, **new** `OpStack67802537KernelSmokeTest` (G1 MSM k=2 gas 22776 + same-tx CREATE/SELFDESTRUCT via Isthmus config).

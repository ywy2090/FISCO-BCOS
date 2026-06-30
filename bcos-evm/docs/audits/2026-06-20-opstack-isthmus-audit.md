# OPStack TE Baseline（Isthmus）规范合规审计报告

**日期：** 2026-06-20（初审计）；**复审计：** 2026-06-21 @ `54e17a62c`；**Wave 2 sign-off：** 2026-06-21；**Wave 3 严格 op-geth 复审计：** 2026-06-21 @ `52dda0921`  
**分支/commit：** `worktree-feat-evm-refactor` / HEAD `52dda0921`（Wave 3 复审计基线）  
**Wave 3 详细报告：** `bcos-evm/docs/audits/2026-06-21-opstack-isthmus-reaudit-wave3.md`  
**op-geth：** v1.101702.2 @ `e8800cffe`  
**geth：** v1.17.3  
**Besu：** tag 26.6.0  
**optimism/specs：** `689a96f6d3aad7cf7b26525e2d7e0b5d581ae057`  
**范围：** OpStack TE baseline 端到端（`transaction-executor` + `bcos-evm`），Isthmus profile  
**设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-isthmus-audit-design.md`  
**执行计划：** `docs/superpowers/plans/2026-06-20-opstack-isthmus-audit.md`（Subagent-Driven 2026-06-20；Task 0–11 复跑 2026-06-21）

**详细矩阵 / 断言表：** `bcos-evm/docs/audits/_work/task*.md`、`test-inventory-opstack.md`

---

## Part 0 — 执行摘要

### 审计时间线

| 阶段 | Commit | 主判定 |
|------|--------|--------|
| 初审计 | `f989f073f` | ❌ 不通过（≥8 🔴） |
| Remediation | OP-01～15 | P0 全闭合 |
| 复审计 | `54e17a62c` | ⚠️ 有条件通过 |
| **Wave 2 sign-off** | **2026-06-21 本地** | **✅ 通过**（远程 CI 待首跑） |
| **Wave 3 严格 op-geth 复审计** | **`52dda0921`** | **⚠️ 有条件通过**（3×🔴 4844 preCheck；Wave 2 P0 无回归） |

### 可裁决行统计（深审 + inherited smoke，不含纯 ⚪）

| 指标 | 初审计 | 复审计 | Wave 2 | Wave 3 |
|------|--------|--------|--------|--------|
| 审计行数（可裁决） | ~30 | ~33 | ~33 | **~36** |
| ✅ 一致 | ~18 | **~26** | **~30** | **~31** |
| 🟡 警告 | ~10 | **~7** | **~3** | **~8** |
| 🔴 阻断 | **≥8** | **0** | **0** | **3**（4844 preCheck） |
| 📋 设计选择 | 若干 | 若干 | 若干 | 若干 |
| **主判定** | **❌ 不通过** | **⚠️ 有条件通过** | **✅ 通过** | **⚠️ 有条件通过** |

### Wave 3 新增发现（严格 op-geth @ `52dda0921`）

| ID | 严重度 | 摘要 |
|----|--------|------|
| R3-4844-1/2/3 | 🔴 | `OpStackPreCheck` 缺 blob CREATE 拒绝、空 hash 拒绝、KZG version 校验（`state_transition.go:421-433`） |
| R3-DEP-1 | ~~🟡~~ **CLOSED** | CREATE deposit nonce — `DepositCreateNonceTest` |
| R3-ORCH-1/2 | ~~🟡~~ **CLOSED** | header OPF1 baseFee / blobBaseFee |
| R3-7623-1 | 🟡 | 非 deposit entry 失败仍 settlement+refundGas |

**Wave 2 闭合项再验证：** OP-01～09、FIX-01～07/09～12 均 ✅ 无回归；CTest 23/23 + TE 13/13 PASS @ `52dda0921`。

### 待决行统计

| 类型 | 行数 |
|------|------|
| blocked: ETH audit pending | **0** |
| matrix-patch-pending（增补 S1–S3） | **0**（已合入 `capability-matrix.md` OP-10） |

### 初审计 Top 5 阻断项 — 复审计闭合状态

| # | 初审计 🔴 | 复审计 | 证据 |
|---|-----------|--------|------|
| 1 | `m_isIsthmus` 生产未接线 | **✅ 闭合** OP-01 | `OpStackTransactionExecutorImpl.h:210-211`；`TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` |
| 2 | Rollup L1 cost 字节源错误 | **✅ 闭合** OP-02 | `OpStackTxInputBuilder.h:119-124`；`OpStackTxInputBuilderTest::buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` |
| 3 | Deposit gas/nonce 语义 | **✅ 闭合** OP-03～05 | `OpStackExecuteViaHost.cpp:135-189`；`DepositNoFeeRoutingTest` |
| 4 | Blob buyGas + 0x03 传播 | **✅ 闭合** OP-06 | `OpStackTxExecutor.cpp:67-75`；`OpStackTxInputBuilder.h:218-226`；`BlobGasBalanceTest` |
| 5 | 7702 intrinsic 12500×n | **✅ 闭合** OP-07 | `calcAuthTupleIntrinsicGas` = 25000×n；`OpStack7702ExecuteViaHostPropagationTest` |

**继承 ETH 内核 🔴 → ✅：** EIP-2537 MSM 折扣表（`BlsGas.h`）；EIP-6780 `selfdestruct`（`EthHost.cpp:190-197`）；`OpStack67802537KernelSmokeTest`

### 测试断言审计（Part 3 摘要）— Wave 2 sign-off

| 指标 | 初审计 | 复审计 | Wave 2 |
|------|--------|--------|--------|
| 测试文件 | 29 | **30** | **30** |
| 用例 | 65 | **86** | **96** |
| ✅ / 🟡 / 🔴 断言 | 49 / 15 / **1** | **74 / 12 / 0** | **88 / 8 / 0** |

### 放行条件 — Wave 2 闭合状态

1. HEAD 重建并跑通 opstack + `TestOpStackTransactionExecutorFixture` 全量 CTest — **✅ 本地 PASS（2026-06-21 darwin arm64）；`capability-gate` `opstack-ctest` job（ADR-010）已入分支 workflow，GitHub 远程 CI 待首跑**
2. 协议层 `TransactionReceipt` 暴露 `operatorFeeScalar` / `operatorFeeConstant` — **✅ FIX-01（ADR-008，TE sidecar + fixture 断言；RPC/tars 仍 out of scope）**
3. Fjord Solidity parity 精确向量 105484/2463 — **✅ FIX-04（ADR-012，`FIX04_FjordL1CostSolidityParity_matchesOpGeth`）**
4. signed RLP → L1 fee TE E2E — **✅ FIX-05（ADR-012，`FIX05_signed_rlp_rollup_execute_e2e`）**
5. L1 attributes system deposit TE E2E（`depositNonce`/nonce） — **✅ FIX-06（ADR-011，`l1_attributes_deposit_via_te`）**
6. TE revert/hard-fail operator fee 断言 — **✅ FIX-07（`revert_keeps_l1_fee_and_operator_fee` + `hard_failure` operator literal）**
7. 4844 full executor E2E — **已取消 FIX-08（ADR-009）**；4844 preCheck 形状 — **✅ FIX-09（6 用例）**
8. 7702 fixture 文档 + existence refund — **✅ FIX-10/11（ADR-013）**
9. inherited Isthmus profile 文档 — **✅ FIX-12（矩阵脚注 + task8 闭合）**

---

## Part 1 — 合规矩阵

完整 Part 1 行见各 Task 笔记（可直接粘贴）：

| Task | 域 | 文件 |
|------|-----|------|
| 1 | Isthmus profile + executor wiring (S3) | `_work/task1-executor-wiring.md` |
| 2 | Fjord L1 cost + rollup | `_work/task2-fjord-l1-cost.md` |
| 3 | Operator fee (S1) | `_work/task3-operator-fee.md` |
| 4 | Deposit tx | `_work/task4-deposit.md` |
| 5 | L1Block + L1 attributes (S2) | `_work/task5-l1block-attributes.md` |
| 6 | 7623 floor + receipt | `_work/task6-floor-gas-receipt.md` |
| 7 | 7702 precheck + 4844 blob | `_work/task7-7702-4844.md` |
| 8 | Inherited smoke | `_work/task8-inherited-smoke.md` |
| 9 | unsupported / profile-only | `_work/task9-unsupported.md` |

### 增补能力（已合入 matrix OP-10）— 摘要 @ `54e17a62c`

| 能力 | 状态 |
|------|------|
| S1 Operator fee (Isthmus) | ✅ explicit（wiring + 公式 + buy/refund/route） |
| S2 L1 attributes system deposit | ✅ explicit（OP-14 IL1Block + OP-12 literal fee E2E） |
| S3 Executor Isthmus wiring | ✅ explicit（`m_isIsthmus` + `applyDefaultTxProps` + `eip2929`） |

---

## Part 2 — 偏离项详情（Wave 2 全部 CLOSED，无 🔴）

详见各 `task*.md` Part 2。初审计 🔴 均已闭合；**Wave 2 前开放 🟡 均已 CLOSED：**

| ID | 现象 | 严重度 | Task | Wave 2 | 状态 |
|----|------|--------|------|--------|------|
| D3-2 | `OpStackReceiptMeta` scalar/constant；`makeReceipt` 暴露 sidecar | 🟡→✅ | 6 | FIX-01 | **CLOSED**（ADR-008；RPC/tars out of scope） |
| D2-1 | 无 Fjord Solidity parity 精确向量（105484/2463） | 🟡→✅ | 2 | FIX-04 | **CLOSED**（ADR-012） |
| D2-2 | signed RLP → L1 fee 无 TE E2E 精确链 | 🟡→✅ | 2 | FIX-05 | **CLOSED**（ADR-012） |
| D7-1 | 4844 preCheck 缺形状校验；full executor E2E | 🟡→✅ | 7 | FIX-08/09 | **CLOSED**（FIX-08 取消 ADR-009；FIX-09 preCheck 6 用例） |
| D5-4 | L1 attributes deposit 未断言 `depositNonce`/nonce | 🟡→✅ | 5 | FIX-06 | **CLOSED**（ADR-011 TE E2E） |
| D1-1 | TE fixture revert/hard-fail 未断言 operator fee | 🟡→✅ | 1/3 | FIX-07 | **CLOSED** |
| D8-1 | inherited #14/#18/#20 profile flag 稀疏 | 🟡→✅ | 8 | FIX-12 | **CLOSED**（ADR-004 文档化；非 wiring 缺口） |

**Wave 3 开放 🔴：** R3-4844-1/2/3 — EIP-4844 preCheck 与 op-geth 三处形状校验不一致（见 Wave 3 报告 Part 4 P0）。

**非阻断残余 🟡：** Inv#14 无 OP 0x01–0x0a precompile fixture；sealer OPF1 生产写路径 follow-up。见 `2026-06-21-opstack-isthmus-reaudit-wave3.md`。

**Out of scope（⚪，非阻断）：** GPO `0x4200…000F`、`setFeature`、`proxyAdmin*`、Bedrock/Jovian setter、`setIsthmus()` 升级迁移、`L1GasUsed` deprecated receipt 字段

---

## Part 3 — 测试断言审计

完整 96 行表见 `_work/task10-assertions.md` 与 `_work/test-inventory-opstack.md`。

**Wave 2 sign-off 要点：**
- `DepositNoFeeRoutingTest` — REVERT `gasUsed=21'000` ✅
- `TestOpStackTransactionExecutorFixture::revert_keeps_l1_fee_and_operator_fee` — TE revert operator fee literal + recipient balance ✅（FIX-07）
- `TestOpStackTransactionExecutorFixture::FIX05_signed_rlp_rollup_execute_e2e` — signed RLP → L1 fee TE E2E ✅（FIX-05）
- `TestOpStackTransactionExecutorFixture::l1_attributes_deposit_via_te` — `depositNonce` + depositor nonce ✅（FIX-06）
- `OpStackFeeTest::FIX04_FjordL1CostSolidityParity_matchesOpGeth` — 105484/2463 ✅（FIX-04）
- `BlobGasBalanceTest` + `DepositTxPreCheckTest` — 4844 preCheck 形状 6 用例 ✅（FIX-09）
- `operator_fee_recipient_gets_fee_on_success` — `operatorFeeScalar`/`operatorFeeConstant` sidecar ✅（FIX-01）

---

## Part 4 — 后续动作

### P0 代码修复（🔴）— 全部闭合 @ `54e17a62c`

| OP ID | 项 | 状态 |
|-------|-----|------|
| OP-01 | `m_isIsthmus` 生产接线 | ✅ |
| OP-02 | signed RLP rollup bytes | ✅ |
| OP-03～05 | deposit nonce/gas 语义 | ✅ |
| OP-06 | blob buyGas + 0x03 传播 | ✅ |
| OP-07 | 7702 intrinsic 25000×n | ✅ |
| OP-08 | 2537/6780 内核 | ✅ |
| OP-09 | 2929 eip2929 + applyDefaultTxProps | ✅ |

### P1 补测 / 协议 parity（🟡）— Wave 2 闭合

| 项 | FIX | 状态 |
|----|-----|------|
| 协议 `TransactionReceipt` 暴露 `operatorFeeScalar` / `operatorFeeConstant` | FIX-01 | ✅ TE sidecar（RPC/tars out of scope） |
| `TestFjordL1CostSolidityParity` 精确向量；signed RLP → L1 fee TE E2E | FIX-04/05 | ✅ |
| type-0x03 blob full executor E2E | FIX-08 | **已取消**（ADR-009） |
| 4844 preCheck 形状校验补全 | FIX-09 | ✅ 6 用例 |
| L1 attributes deposit `depositNonce`/nonce 断言；TE revert operator fee 断言 | FIX-06/07 | ✅ |
| HEAD 重建 + 全量 opstack CTest CI gate | FIX-02/03 | ✅ 本地 PASS；workflow 已入分支，**远程 CI 待首跑** |
| inherited Isthmus profile 文档 sign-off | FIX-12～15 | ✅ |

### Matrix — 已合入（OP-10）

见 `bcos-evm/capability-matrix.md` 行：OPStack operator fee、L1 attributes deposit、Isthmus executor integration、Rollup L1 cost tx bytes (Fjord)。

### ETH 交叉引用

- inherited 行已引用 `2026-06-20-eth-reference-cancun-plus-audit.md`；无 `blocked: ETH audit pending`
- 2537/6780 OP 行随 ETH P0 闭合 → OP smoke ✅

### 放行证据 / Wave 2 完成记录（2026-06-21）

**日期 / 平台：** 2026-06-21 · darwin arm64（本地）  
**主判定：** **✅ 通过**（Wave 2 FIX-01～07/09～15 全部闭合；**远程 GitHub CI 待首跑**）

#### R3 — HEAD CTest 验证记录（FIX-03，Wave 2 全量）

| 项 | 命令 / 范围 | 结果 |
|----|-------------|------|
| bcos-evm opstack 过滤 ctest | `ctest -R 'OpStack\|L1Block\|Deposit\|Blob\|7702\|RefundIsthmus\|L1Attributes'`（`build/bcos-evm/test`） | **23/23 PASS** |
| transaction-executor TE fixture | `ctest -R OpStackTransactionExecutorFixture`（`build/transaction-executor/tests`） | **1/1 PASS**（13 BOOST 用例） |
| capability matrix lint | `bash bcos-evm/tools/ci/check-capability-matrix.sh` | **OK** |
| CI gate | `.github/workflows/capability-gate.yml` → `opstack-ctest` job（ADR-010） | **workflow 已入分支；GitHub 远程 CI 待首跑** |

#### Wave 2 全量闭合项（FIX-01～15）

| FIX | Task | 内容 | ADR / 证据 |
|-----|------|------|------------|
| FIX-01 | 1 | `TransactionReceipt` sidecar 暴露 `operatorFeeScalar` / `operatorFeeConstant` | ADR-008；闭合 D3-2 |
| FIX-02 | 2 | `capability-gate.yml` 增补 `opstack-ctest` | ADR-010 |
| FIX-03 | 3 | 本节 R3 HEAD CTest 验证记录 | — |
| FIX-04 | 4 | `FIX04_FjordL1CostSolidityParity_matchesOpGeth`（105484/2463） | ADR-012；闭合 D2-1 |
| FIX-05 | 5 | `FIX05_signed_rlp_rollup_execute_e2e` TE E2E | ADR-012；闭合 D2-2 |
| FIX-06 | 6 | `l1_attributes_deposit_via_te`（`depositNonce`/nonce） | ADR-011；闭合 D5-4 |
| FIX-07 | 7 | `revert_keeps_l1_fee_and_operator_fee` + hard-fail operator literal | 闭合 D1-1 |
| FIX-08 | 8 | **已取消** blob type-0x03 full TE E2E | ADR-009 |
| FIX-09 | 9 | `BlobGasBalanceTest` + `DepositTxPreCheckTest` 4844 preCheck 6 用例 | ADR-009；闭合 D7-1（preCheck 范围） |
| FIX-10 | 10 | `stEIP7702_delegation.json` hand-crafted smoke 文档 | ADR-013 |
| FIX-11 | 11 | 7702 authority existence refund 专用用例 | ADR-013 |
| FIX-12 | 12 | inherited Isthmus profile 矩阵脚注 + task8 闭合 | ADR-004 |
| FIX-13 | 12 | 本报告主判定 ⚠️→✅、Part 2 CLOSED | — |
| FIX-14 | 12 | `task10-assertions.md` + `test-inventory-opstack.md` 同步 | — |
| FIX-15 | 12 | `inheritance-work-tracker.md` #6 `[x]`（CI 待首跑） | ADR-010 |

---

### Wave 3 CTest 验证（@ `52dda0921`）

| 项 | 结果 |
|----|------|
| bcos-evm opstack 过滤 ctest | **23/23 PASS** |
| TE `OpStackTransactionExecutorFixture` | **1/1 PASS**（13 用例） |

---

**审计状态：** 初版 + 复审计 + Wave 2 sign-off + **Wave 3 严格 op-geth 复审计**（2026-06-21 @ `52dda0921`）。  
**当前主判定：** **⚠️ 有条件通过** — Wave 2 P0 无回归；新增 3×🔴 4844 preCheck 缺口待 P0 闭合。  
**完整 Wave 3 产出：** `2026-06-21-opstack-isthmus-reaudit-wave3.md`。Part 1 域表见 `_work/task*.md` Wave 3 附录。

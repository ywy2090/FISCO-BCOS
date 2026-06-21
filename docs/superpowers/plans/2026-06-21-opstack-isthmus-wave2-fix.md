# OPStack Isthmus Wave 2 — ⚠️→✅ Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 OPStack Isthmus TE baseline 从复审计 **⚠️ 有条件通过** 提升为 **✅ 通过**——闭合 R1/R2 放行门、清零 Part 2 🟡、签收文档。

**Architecture:** 三 Wave 对齐设计 spec `docs/superpowers/specs/2026-06-21-opstack-isthmus-wave2-design.md`：Wave 1 补协议 receipt 字段与 CI；Wave 2 用 op-geth 钉死数值向量 + TE E2E 断言；Wave 3 更新审计与 inventory。协议侧沿用 `l1Fee`/`operatorFee` 的 **sidecar optional string** 模式（不进 tars hash / RPC；见 ADR-008），不碰 OP-14 L1Block 计费 orchestration。

**Tech Stack:** C++17、Boost.Test、CMake/CTest、`transaction-executor` + `bcos-evm/opstack/**`、op-geth `rollup_cost_test.go` 向量

## Global Constraints

- **基线 commit：** `593a32b88`（或当前 HEAD）；Remediation OP-01～15 已合
- **In scope：** `TransactionReceipt` scalar/constant、`makeReceipt`、opstack 测试 parity、CI gate、审计签收
- **Out of scope（⚪）：** GPO `0x4200…000F`、`setFeature`、Bedrock setter、BCOS/ETH 7702 intrinsic、legacy CompatEip7702、修改 `loadOpStackFeeParams`
- **命令前缀：** 使用 `rtk`（仓库 CLAUDE.md）
- **设计 spec：** `docs/superpowers/specs/2026-06-21-opstack-isthmus-wave2-design.md`
- **Normative matrix：** `bcos-evm/capability-matrix.md`（FIX-12 仅脚注，不改计费行）
- **验收 CTest：**
  ```bash
  ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' \
    --test-dir build/bcos-evm/test -C Debug --output-on-failure
  ctest -R OpStackTransactionExecutorFixture \
    --test-dir build/transaction-executor/tests -C Debug --output-on-failure
  bash bcos-evm/tools/ci/check-capability-matrix.sh
  ```

---

## Baseline 快照（计划编写时）

| FIX | 状态 | 备注 |
|-----|------|------|
| FIX-04 | **执行 Task 4** | ADR-012：按计划实现/加固，非 verify-only |
| FIX-05 | **执行 Task 5** | ADR-012：按计划实现/加固，非 verify-only |
| FIX-06 | **部分** | executeViaHost 有；需 **TE** `isthmus_l1_attributes.bin` E2E（ADR-011，grill C） |
| FIX-07 | **部分** | `revert_keeps_l1_fee_and_operator_fee` 有；缺 scalar/constant（依赖 FIX-01） |
| FIX-08 | **取消（ADR-009）** | orchestration 层 `BlobGasBalanceTest` 闭合 D7-1；不做 TE blob E2E |
| FIX-09 | **部分** | 需 **全覆盖** preCheck 形状（grill C）；`BlobGasBalanceTest` 为基底 |
| FIX-10 | **文档** | ADR-013 A：`source` + inventory 标注 hand-crafted |
| FIX-11 | **开放** | ADR-013 D：必须新增 existence refund case |
| FIX-01/02/03/12–15 | **开放** | 见下文 Task |

---

## File Map

| 文件 | Wave | 职责 |
|------|------|------|
| `bcos-framework/bcos-framework/protocol/TransactionReceipt.h` | 1 | 虚接口 + `AnyTransactionReceipt` 尺寸 |
| `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h` | 1 | sidecar `m_operatorFeeScalar/Constant` |
| `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.cpp` | 1 | getter/setter |
| `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.cpp` | 1 | `createReceipt` 复制新字段 |
| `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h` | 1 | `makeReceipt` 写入 scalar/constant |
| `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` | 1–2 | TE E2E 断言扩展 |
| `.github/workflows/capability-gate.yml` | 1 | opstack CTest job |
| `bcos-evm/test/opstack/OpStackFeeTest.cpp` | 2 | 验证/巩固 Fjord 向量 |
| `bcos-evm/test/opstack/L1AttributesDepositTest.cpp` | 2 | D5-4 nonce 断言 |
| `bcos-evm/test/opstack/BlobGasBalanceTest.cpp` | 2 | 4844 preCheck 补全（FIX-09）；D7-1 主证据 |
| ~~`transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`~~ | ~~2~~ | ~~blob TE E2E（FIX-08 已取消）~~ |
| `bcos-evm/test/opstack/OpStack7702ExecuteViaHostPropagationTest.cpp` | 2 | existence refund |
| `bcos-evm/test/fixtures/state/imported/stEIP7702_delegation.json` | 2 | 更名/注释 |
| `bcos-evm/capability-matrix.md` | 2 | inherited profile 脚注（FIX-12） |
| `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md` | 3 | 主判定 ✅ |
| `bcos-evm/docs/audits/_work/task10-assertions.md` | 3 | 🟡→✅ |
| `bcos-evm/docs/audits/_work/test-inventory-opstack.md` | 3 | 同步 |
| `bcos-evm/docs/inheritance-work-tracker.md` | 3 | #6 `[x]` |

---

## 并行策略

```
Task 1 (FIX-01 receipt) ──┬── Task 3 (FIX-03 CTest record)
Task 2 (FIX-02 CI)      ──┤
                          ├── Task 4–11 (Wave 2 tests, 可并行子任务)
                          └── Task 12 (FIX-12–15 docs) ← 依赖 1–11 绿
```

PR 切分（设计 spec §5）：
- **PR-1：** Task 1–3
- **PR-2：** Task 4–11
- **PR-3：** Task 12

---

### Task 1: FIX-01 — Receipt `operatorFeeScalar` / `operatorFeeConstant`

**Skills:** @fisco-evm-breaking-check（协议面变更）

**Files:**
- Modify: `bcos-framework/bcos-framework/protocol/TransactionReceipt.h`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.cpp`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.cpp`
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

- [ ] **Step 1: 写失败测试（TE fixture）**

在 `operator_fee_recipient_gets_fee_on_success` 同 harness 中扩展断言（或新 case `operator_fee_receipt_includes_scalar_and_constant`）：

```cpp
BOOST_REQUIRE(receipt->operatorFeeScalar().has_value());
BOOST_REQUIRE(receipt->operatorFeeConstant().has_value());
auto const params = seededFeeParams();
BOOST_CHECK_EQUAL(parseHexU256(receipt->operatorFeeScalar().value()), params.operatorFeeScalar);
BOOST_CHECK_EQUAL(parseHexU256(receipt->operatorFeeConstant().value()), params.operatorFeeConstant);
```

在 `revert_keeps_l1_fee_and_operator_fee` 中同样断言 scalar/constant 与 `seededFeeParams()` 一致。

- [ ] **Step 2: 运行测试确认失败**

```bash
cd build && cmake --build . --target test-opstack-transaction-executor-fixture -j8
ctest -R OpStackTransactionExecutorFixture --test-dir build/transaction-executor/tests -C Debug --output-on-failure
```

Expected: 编译失败（接口不存在）或 `operatorFeeScalar` 无值。

- [ ] **Step 3: 扩展 `TransactionReceipt` 接口**

在 `TransactionReceipt.h` 于 `operatorFee` 旁增加：

```cpp
virtual std::optional<std::string> operatorFeeScalar() const = 0;
virtual void setOperatorFeeScalar(std::string operatorFeeScalar) = 0;
virtual std::optional<std::string> operatorFeeConstant() const = 0;
virtual void setOperatorFeeConstant(std::string operatorFeeConstant) = 0;
```

- [ ] **Step 4: 实现 `TransactionReceiptImpl`**

`TransactionReceiptImpl.h` 增加 `m_operatorFeeScalar`、`m_operatorFeeConstant`；cpp 实现 getter/setter（照抄 `m_l1Fee` 模式）。**不改** `TransactionReceipt.tars`（ADR-008）。

更新 `static_assert(sizeof(TransactionReceiptImpl) <= N)` 与 `AnyTransactionReceipt` 的 `N`（编译后 `sizeof` 验证）。

`TransactionReceiptFactoryImpl::createReceipt(TransactionReceipt&)` 复制新字段。

- [ ] **Step 5: `makeReceipt` 接线**

在 `OpStackTransactionExecutorImpl.h` `makeReceipt()` 中，`setOperatorFee` 之后：

```cpp
if (m_data->m_receiptMeta.operatorFeeScalar.has_value())
{
    receipt->setOperatorFeeScalar(
        "0x" + m_data->m_receiptMeta.operatorFeeScalar->str(0, std::ios_base::hex));
}
if (m_data->m_receiptMeta.operatorFeeConstant.has_value())
{
    receipt->setOperatorFeeConstant(
        "0x" + m_data->m_receiptMeta.operatorFeeConstant->str(0, std::ios_base::hex));
}
```

- [ ] **Step 6: 运行测试确认通过**

```bash
ctest -R OpStackTransactionExecutorFixture --test-dir build/transaction-executor/tests -C Debug --output-on-failure
```

Expected: PASS

- [ ] **Step 7: Commit（用户要求时）**

```bash
rtk git add bcos-framework/... bcos-tars-protocol/... transaction-executor/...
rtk git commit -m "$(cat <<'EOF'
fix(opstack): expose operator fee scalar and constant on receipts

Wire OpStackReceiptMeta fields through TransactionReceipt and makeReceipt
so Isthmus TE receipts match optimism metadata expectations.
EOF
)"
```

---

### Task 2: FIX-02 — CI capability-gate + opstack CTest（ADR-010）

**Files:**
- Modify: `.github/workflows/capability-gate.yml`
- Modify: `bcos-evm/docs/inheritance-work-tracker.md`（#6 注记）

**策略：** 工作流 **触发即全量** — lint + configure + build + 两套 ctest（不单跑 lint）。

- [ ] **Step 1: 增补 workflow job `opstack-ctest`**

在现有 `matrix-lint` job 之后增加（或合并为单 job 多 step）：

```yaml
  opstack-ctest:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure and build opstack tests
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Debug
          cmake --build build --target \
            test-opstack-transaction-executor-fixture \
            -j$(nproc)
          cmake --build build/bcos-evm/test -j$(nproc)
      - name: Run opstack CTest
        run: |
          ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' \
            --test-dir build/bcos-evm/test -C Debug --output-on-failure
          ctest -R OpStackTransactionExecutorFixture \
            --test-dir build/transaction-executor/tests -C Debug --output-on-failure
```

路径触发已含 `bcos-evm/opstack/**`；确认 PR 改 `transaction-executor/**` 时也触发（增补 paths 若需要）。

- [ ] **Step 2: 本地模拟**

```bash
bash bcos-evm/tools/ci/check-capability-matrix.sh
# 同上 ctest 命令
```

- [ ] **Step 3: 更新 tracker #6 → `[x]`**

- [ ] **Step 4: Commit（用户要求时）**

---

### Task 3: FIX-03 — HEAD 全量 CTest 验证记录

**Files:**
- Modify: `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`（Part 4 R2 证据段）

- [ ] **Step 1: 在干净 build 跑全量验收命令**（Task 1–2 绿后）

记录：commit SHA、ctest 通过数、失败数、日期。

- [ ] **Step 2: 写入审计报告 Part 4「放行证据」小节**

---

### Task 4: FIX-04 — Fjord Solidity 精确向量（ADR-012：执行计划，非 verify-only）

**Files:**
- Modify: `bcos-evm/test/opstack/OpStackFeeTest.cpp`
- Modify: `bcos-evm/docs/audits/_work/task2-fjord-l1-cost.md`

- [ ] **Step 1: 按 Task 交付补全/扩展向量**（即使 `FjordL1_SolidityParity_matchesOpGeth105484` 已存在，仍执行 pin 注释、缺失子断言、额外 op-geth 字面量若计划要求）

- [ ] **Step 2: 运行 `ctest -R OpStackFee` 绿**

- [ ] **Step 3: 更新 task2 笔记 🟡→✅**

---

### Task 5: FIX-05 — signed RLP → L1 fee TE E2E（ADR-012：执行计划，非 verify-only）

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`
- Modify: `bcos-evm/docs/audits/_work/task2-fjord-l1-cost.md`

- [ ] **Step 1: 按 Task 交付加固 TE E2E**（扩展 `signed_rlp_rollup_execute_e2e` 或新增 case；消除 synthetic rollup smoke 依赖若笔记仍标 🟡）

- [ ] **Step 2: 确认 `l1_fee_recipient_gets_fee_on_success` 与 signed 路径关系在注释或实现上清晰**

- [ ] **Step 3: 更新 task2 RollupCost E2E 行**

---

### Task 6: FIX-06 — L1 attributes TE E2E（ADR-011）

**Files:**
- Modify: `bcos-evm/test/opstack/L1AttributesDepositTest.cpp`（orchestration 证据保留）
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`（**新增 TE case**）
- Fixture: `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin`

- [ ] **Step 1: 新增 TE case `l1_attributes_deposit_via_te`**

通过 `OpStackTransactionExecutorImpl::executeTransaction` 提交 system/L1 attributes deposit（fixture `isthmus_l1_attributes.bin` 或等价 deposit tx 构造）。

断言：
- `receipt->status() == 0`
- `receipt->depositNonce()` 有值且与预期一致
- `OP_DEPOSITOR_ACCOUNT` nonce 递增

- [ ] **Step 2: 保留/强化 `L1AttributesDepositTest` orchestration 断言**（meta + depositor nonce）

- [ ] **Step 3: 运行**

```bash
ctest -R 'L1Attributes|OpStackTransactionExecutorFixture' \
  --test-dir build -C Debug --output-on-failure
```

---

### Task 7: FIX-07 — TE revert operator fee meta（scalar/constant）

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

**依赖:** Task 1

- [ ] **Step 1: 在 `revert_keeps_l1_fee_and_operator_fee` 增加 scalar/constant 断言**（Task 1 Step 1 若未做）

- [ ] **Step 2: 断言 `OP_OPERATOR_FEE_RECIPIENT` balance == `expectedOperator`**

---

### Task 8: ~~FIX-08~~ — **已取消（ADR-009）**

TE blob type-0x03 E2E 不在 Wave 2 放行范围。D7-1 由 `BlobGasBalanceTest` + Task 9（FIX-09）闭合。

---

### Task 9: FIX-09 — 4844 preCheck 形状补全

**Files:**
- Modify: `bcos-evm/test/opstack/BlobGasBalanceTest.cpp` 或 `DepositTxPreCheckTest.cpp`
- Reference: `bcos-evm/opstack/OpStackPreCheck.cpp`

- [ ] **Step 1: 补 case — blob hashes 存在但 `blobGasFeeCap` 缺失 → Malformed**

- [ ] **Step 2: 补 case — 非 4844 tx 带 blob hashes → Malformed**

- [ ] **Step 3: 补 case — `maxFeePerBlobGas` < `blobBaseFee`（已有 `blob_gas_fee_cap_under_blob_base_fee_is_rejected`，确认 status 与 op-geth 一致）**

- [ ] **Step 4: `ctest -R Blob` 绿**

---

### Task 10: FIX-10 — hand-crafted 7702 fixture 文档化（ADR-013 A）

**Files:**
- Modify: `bcos-evm/test/fixtures/state/imported/stEIP7702_delegation.json`（`source` 字段）
- Modify: `bcos-evm/docs/audits/_work/test-inventory-opstack.md`

**不做：** 改名、从 fixture 流水线移除、断 smoke 引用。

- [ ] **Step 1: JSON `source` 标注 `hand-crafted (not ethereum/tests 7702 suite)`**

- [ ] **Step 2: inventory 标 📋 hand-crafted — 非 7702 EIP 合规证据**

- [ ] **Step 3: 保留 fixture 供 propagation smoke**

---

### Task 11: FIX-11 — 7702 existence refund（ADR-013 D，必做）

**Files:**
- Modify: `bcos-evm/test/opstack/OpStack7702ExecuteViaHostPropagationTest.cpp`
- Reference: `bcos-evm/eth/Eip7702.cpp`（refund 常量）

Intrinsic 25000 用例 **不能** 单独闭合 T6。

预置 authority 账户已有非空 code；提交 authorization；断言 `gasUsed` 或 `gas_left` 反映 `PER_EMPTY_ACCOUNT_COST` refund（与 intrinsic 测试对称）。

- [ ] **Step 2: 运行 `ctest -R 7702`**

---

### Task 12: FIX-12–15 — 文档签收

**Files:**
- Modify: `bcos-evm/capability-matrix.md`（inherited Isthmus profile 脚注，FIX-12）
- Modify: `bcos-evm/docs/audits/_work/task8-assertion-audit.md` 或 task8 笔记（FIX-12）
- Modify: `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-audit.md`（FIX-13）
- Modify: `bcos-evm/docs/audits/_work/task10-assertions.md`（FIX-14）
- Modify: `bcos-evm/docs/audits/_work/test-inventory-opstack.md`（FIX-14）
- Modify: `bcos-evm/docs/inheritance-work-tracker.md`（FIX-15）

- [ ] **Step 1: Part 2 🟡 表逐项标 CLOSED 或删除**

- [ ] **Step 2: 主判定改为 **✅ 通过**`，附 commit SHA + ctest 摘要**

- [ ] **Step 3: `task10-assertions.md` OPStack 行 🟡→✅**

- [ ] **Step 4: tracker #6 `[x]`**（若 Task 2 未做）

- [ ] **Step 5: Commit docs（用户要求时）**

---

## 全局验收（Task 12 前必绿）

```bash
bash bcos-evm/tools/ci/check-capability-matrix.sh

ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' \
  --test-dir build/bcos-evm/test -C Debug --output-on-failure

ctest -R OpStackTransactionExecutorFixture \
  --test-dir build/transaction-executor/tests -C Debug --output-on-failure
```

**文档：** `opstack-isthmus-audit.md` Part 2 无开放 🟡；主判定 ✅。

---

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| `AnyTransactionReceipt` 尺寸溢出 | 编译期 `static_assert`；必要时仅增 sidecar 不增 tars |
| CI 构建时间过长 | job 仅 PR 路径触发；并行 build target |
| FIX-04/05 已闭合重复劳动 | Baseline 表先 verify，仅补文档 |
| blob TE E2E 需 builder 改动 | **已取消 FIX-08**（ADR-009） |

---

**计划版本：** 2026-06-21  
**对应设计：** `docs/superpowers/specs/2026-06-21-opstack-isthmus-wave2-design.md`

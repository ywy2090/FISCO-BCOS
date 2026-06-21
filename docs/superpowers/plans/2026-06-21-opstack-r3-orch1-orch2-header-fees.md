# R3-ORCH-1/2 Header Fee 对齐 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** TE 仅从 header OPF1 读取 L2 baseFee / blobBaseFee，闭合 R3-ORCH-1/2；**无 Legacy fallback**。

**Architecture:** `OpStackBlockHeaderExtension` 编解码 + `requireOpStackHeaderFees` 严格解析；全量测试/出块写 OPF1；缺失 OPF1 抛异常。

**Spec:** `docs/superpowers/specs/2026-06-21-opstack-r3-orch1-orch2-header-fees-design.md`

## Global Constraints

- magic `OPF1` = `0x4F504631`; min extraData = 64 bytes — **mandatory on all OpStack blocks**
- **No** fallback to `ledgerConfig.gasPrice()` or L1Block slot 7 for execution fees
- Missing/invalid OPF1 → `std::invalid_argument` via `requireOpStackHeaderFees`
- Isthmus: `excessBlobGas == 0` → `blobBaseFee = 1`
- 命令前缀：`rtk`

---

### Task 1: OpStackBlockHeaderExtension

**Files:**
- Create: `bcos-evm/opstack/OpStackBlockHeaderExtension.h`

- [ ] encode/decode + `requireOpStackHeaderFees` + `calcOpStackBlobBaseFee`
- [ ] non-zero excessBlobGas → throw

---

### Task 2: OpStackBlockHeaderExtensionTest

- [ ] round-trip; tryParse nullopt without OPF1; require throws

---

### Task 3: Resolver 重构（严格）

**Files:**
- Modify: `OpStackTxInputBuilder.h`, `OpStackTransactionExecutorImpl.h`

- [ ] `resolveOpStackBaseFee(header)` — **仅** require OPF1，删除 ledgerConfig 参数
- [ ] `resolveOpStackBlobBaseFee(header)` — 删除 stateView 路径
- [ ] 更新 TE 所有调用点

---

### Task 4: 测试 harness 全量 OPF1

**Files:**
- Modify: `TestOpStackTransactionExecutorFixture.cpp` — `makeBlockHeader()` 写 OPF1
- Modify: 所有直连 `opStackExecuteViaHost` 且设 `blockInfo.baseFee/blobBaseFee` 的 opstack 测试 — 改用 header helper 或共享 `makeOpStackBlockInfo(baseFee)` fixture

- [ ] 审计 grep：`blockInfo.baseFee` / `resolveOpStackBlobBaseFee(state` 残留
- [ ] **删除** legacy fallback 相关测试用例

---

### Task 5: OpStackTxInputBuilderTest + BlobGasBalanceTest

- [ ] resolver OPF1 用例；无 OPF1 expect throw
- [ ] slot7 解耦断言

---

### Task 6: 出块 hook（P0 必做）

- [ ] OpStack 出块 `setExtraData` 写入 OPF1

---

### Task 7: 文档与审计闭合 + 全量回归

- [ ] R3-ORCH-1/2 CLOSED
- [ ] opstack CTest + TE fixture 全 PASS

---

## 验证

```bash
ctest -R 'OpStack|L1Block|Deposit|Blob|7702|RefundIsthmus|L1Attributes' --test-dir build/bcos-evm/test --output-on-failure
ctest -R OpStackTransactionExecutorFixture --test-dir build/transaction-executor/tests --output-on-failure
```

# OPStack Isthmus P1 质量项（OP-11–15）执行计划

> **Skill:** executing-plans  
> **设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-isthmus-p1-quality-design.md`

**Goal:** 完成 OP-11–15，强化测试 parity 与测试卫生。

**命令前缀：** `rtk`

---

## Task 1 — OP-15: 清理冗余 `m_isIsthmus`

- [ ] 从以下文件删除 `input.opTxExecutor.m_isIsthmus = true`（`opStackExecuteViaHost` 直连用例）：
  - `CanTransferTest.cpp`, `DepositMintTest.cpp`, `DepositNoFeeRoutingTest.cpp`
  - `L1AttributesDepositTest.cpp`（user tx 段）
  - `OpStack7702ExecuteViaHostPropagationTest.cpp`, `OpStackExecuteViaHostSmokeTest.cpp`
  - `OpStackSettlementTest.cpp`
- [ ] **保留** `RefundIsthmusTest.cpp` 的 `executor.m_isIsthmus`（单元测 `OpStackTxExecutor`）
- [ ] 运行上述测试 + `test-opstack-transaction-executor-fixture`

---

## Task 2 — OP-11: Fjord 边界向量

- [ ] 在 `OpStackFeeTest.cpp` 新增：
  - `FjordL1_minimumBounds_fastLz100_150_170` → cost = 3_203_000
  - `FjordL1_minimumBounds_fastLz171_increases` → cost > 3_203_000
  - `FjordL1_solidityParityVector` → fee = 105484（gas 2463 侧：仅 fee 若向量提供）
- [ ] 对照 op-geth `rollup_cost_test.go:67-117`
- [ ] `rtk test ./build/bcos-evm/test/OpStackFeeTest`

---

## Task 3 — OP-12: L1 attributes literal fee

- [ ] 扩展 `L1AttributesDepositTest.cpp`：
  - attributes deposit 后 user tx 断言 `receiptMeta.l1Fee`、`receiptMeta.operatorFee` 精确值
  - `OP_L1_FEE_RECIPIENT` / `OP_OPERATOR_FEE_RECIPIENT` 余额 literal
- [ ] 用 fixture `isthmus_l1_attributes.bin` + `makeTestParams()` 等价参数手算 golden
- [ ] `rtk test ./build/bcos-evm/test/L1AttributesDepositTest`

---

## Task 4 — OP-13: Receipt meta scalar/constant

- [ ] `OpStackReceiptMeta.h` 增加 `operatorFeeScalar`, `operatorFeeConstant` optional
- [ ] `opStackExecuteViaHost` 或 `OpStackTransactionExecutorImpl` 从 `loadOpStackFeeParams` 写入 meta（非零时）
- [ ] `TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` 断言 scalar/constant
- [ ] 文档注释：协议 `TransactionReceipt` 暂未扩展（known gap）
- [ ] 运行 fixture operator fee 测试

---

## Task 5 — OP-14: L1Block metadata slots

- [ ] 对照 op-geth 增 metadata slot 常量（number/timestamp/hash/sequence/batcherHash）
- [ ] `applySetterIsthmus` 写入 `parseIsthmusL1Attributes` 已有字段
- [ ] 扩展 `L1BlockPredeployTest` 断言 metadata slot 与 fixture 一致
- [ ] `rtk test ./build/bcos-evm/test/L1BlockPredeployTest`

---

## Task 6 — 收尾

- [ ] 更新 `2026-06-20-opstack-isthmus-work-list.md` OP-11–15 → `[x]`
- [ ] 运行 opstack 测试批次

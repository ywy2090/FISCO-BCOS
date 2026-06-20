# Task 1: Isthmus Profile + Executor Integration Wiring

## Part 1 rows

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| makeIsthmusRevisionConfig | revision profile | matrix inherited | inherited | smoke | ✅ | Isthmus PRAGUE EVM | `RevisionConfig.h:62-72` | op-geth Isthmus EVM rules | `RevisionConfigProfileTest` | — |
| Isthmus executor integration wiring (S3) | executor-integration | supplement | explicit | 深审 | 🔴 | Isthmus operator fee MUST | `OpStackTransactionExecutorImpl.h:197-210` | op-geth charges operator fee when Isthmus active | `TestOpStackTransactionExecutorFixture` | **`m_isIsthmus` 未设置** |

## Part 2 — 🔴 S3: `m_isIsthmus` 生产路径未接线

**现象：** `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()` 设置 `makeIsthmusRevisionConfig()`、`buildRollupCostData()`、`fillGasCaps()`，但 **未** 设置 `input.opTxExecutor.m_isIsthmus = true`。

**影响：**
- `OpStackTxExecutor::buyGas` 中 operator fee 门控：`if (m_isIsthmus && m_operatorCostFunc)` → 生产路径 **永不扣 operator fee**
- `opStackExecuteViaHost.cpp:237` refund 路径同样门控 `m_isIsthmus`

**对照：** 所有 `bcos-evm/test/opstack/*` 与 executor 测试手动 `m_isIsthmus = true`；生产 executor 未设。

**修复建议：** 在 `opStackExecuteViaHostTx()` 增加 `input.opTxExecutor.m_isIsthmus = true`（或 Isthmus fork 检测 helper）。

**严重度：** 🔴 — Isthmus operator fee 端到端失效。

## Part 3 — executor 测试

| 文件 | 用例 | 断言状态 | 备注 |
|------|------|----------|------|
| `TestOpStackTransactionExecutorFixture.cpp` | smoke | 🟡 | 未断言 `m_isIsthmus` / operator fee |
| `OpStackExecuteViaHostSmokeTest.cpp` | 各用例 | 🟡 | 手动 `m_isIsthmus=true`，非生产路径 |

## makeIsthmusRevisionConfig 核对

| 字段 | 值 | 期望 |
|------|-----|------|
| revision | EVMC_PRAGUE | ✅ |
| eip7623 | true | ✅ |
| eip7702 | true | ✅ |
| eip4844 | true | ✅ |
| prague_post_execution | false | ✅ matrix unsupported |

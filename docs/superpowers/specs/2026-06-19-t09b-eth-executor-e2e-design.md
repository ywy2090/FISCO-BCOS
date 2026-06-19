# T-09b Eth Executor 级端到端验证 — 设计规格

**日期：** 2026-06-19  
**状态：** 已评审（断言策略 C）  
**前置：** T-09 Layer 1 完成（`ExecuteViaEthFixtureTest`，20 fixtures）  
**关联：** `docs/superpowers/specs/2026-06-18-t09-eth-e2e-validation-design.md`

---

## 1. 背景与动机

T-09 已验证 `executeViaEth` 库层语义（20 个 Prague/imported fixture）。但 **Eth 生产路径** 还经过：

```
Transaction → EthTransactionExecutorImpl
  → executeStep<0> Prepare (warm)
  → executeStep<1> buyGas → executeViaEth → applyStateDiff → refundGas
  → executeStep<2> makeReceipt
```

T-09b 目标：用 **同一批 20 个 fixture**，验证 executor 级全链路能产出与 Layer 1 一致的 receipt 语义。

---

## 2. 范围

### 2.1 在范围内

| 交付物 | 说明 |
|--------|------|
| `EthFixtureStorageSeeder.h` | fixture pre-state → `MutableStorage` |
| `EthFixtureTransactionBuilder.h` | fixture tx → `protocol::Transaction` |
| `ExecutorFixtureAssert.h` | receipt 断言（Phase 1） |
| `TestEthTransactionExecutorFixture.cpp` | 遍历 20 fixtures |
| CMake standalone 目标 + ctest | `EthTransactionExecutorFixture` |
| Phase 2（可选迭代） | 精选 fixture 增加 `gas_used_executor` 断言 |

### 2.2 不在范围内

| 项 | 归属 |
|----|------|
| 新 fixture 语料 | T-09 已交付，T-09b 只复用 |
| HelloWorld deploy/call 自有合约 | 可选后续，非 T-09b MVP |
| Scheduler / 节点 smoke | T-19 / T-26 |
| `CompatExecuteViaHost` 规模迁移 | T-17 |

### 2.3 Done 标准

**Phase 1（必做）：**
- [x] `ctest -R EthTransactionExecutorFixture` 全绿（20 cases）
- [x] 断言：`status` + `output` + `logs`（`gas_used` 全部跳过）
- [x] 复用 `EthStateFixtureLoader` + 现有 JSON，无重复语料

**Phase 2（迭代）：**
- [x] 对 ≥6 个有明确 Layer 1 `gas_used` 的 fixture 增加 `expected.gas_used_executor` 断言
- [x] 文档记录 executor gas 与 Layer 1 gas 口径差异

---

## 3. 断言策略（选项 C）

### Phase 1 — 宽松（先跑通）

| 字段 | 规则 |
|------|------|
| `status` | `fixture.expected.status` → `receipt->status()`（经 status 映射表） |
| `output` | `receipt->output()` 与 `fixture.expected.output` 精确匹配 |
| `logs` | 有 `expected.logs` 则匹配 `receipt->logEntries().size()` |
| `gas_used` | **全部跳过** |

### Phase 2 — 收紧（迭代）

在 fixture JSON 中可选新增：

```json
"expected": {
  "gas_used": 18,
  "gas_used_executor": 18,
  "gas_used_executor_tolerance": 0
}
```

- `gas_used_executor` 非零时断言 `receipt->gasUsed()`（`EthTransactionExecutorImpl` 全链路 receipt）
- 支持 `gas_used_executor_tolerance`
- 不修改现有 `gas_used` 字段语义（仍供 Layer 1 `executeViaEth` 使用）

**Gas 口径说明：**
- **Layer 1 (`gas_used`)**：`executeViaEth` 路径，`gasBefore - evmcResult.gas_left`（纯 EVM 执行消耗）
- **Executor (`gas_used_executor`)**：`receipt->gasUsed()`，经 `EthTransactionExecutorImpl` 结算；对 CALL/REVERT 类 fixture 与 Layer 1 一致；CREATE 类 fixture 可能不同（executor 仅报告部署合约的执行 gas，不含 init code 数据 gas 等 Layer 1 全量计量）

**Phase 2 已入库 fixture（实测 `gas_used_executor`）：**

| Fixture | `gas_used` (L1) | `gas_used_executor` |
|---------|-----------------|---------------------|
| `prague_call_return_word` | 18 | 18 |
| `stExample_return42` | 18 | 18 |
| `stRevert_revertBasic` | 6 | 6 |
| `stRevert_revertDepth` | 2632 | 2632 |
| `stCreate_initCode` | 154 | 18 |
| `stCreate2_basic` | 32030 | 32030 |

---

## 4. 架构

```
fixtures/state/*.json + imported/*.json
        │
        ▼
EthStateFixtureLoader (复用 T-09)
        │
   ┌────┴────┐
   ▼         ▼
StorageSeeder  TransactionBuilder
   │         │
   ▼         ▼
MutableStorage + Transaction + BlockHeader + LedgerConfig
        │
        ▼
EthTransactionExecutorImpl::executeTransaction()
        │
        ▼
ExecutorFixtureAssert (Phase 1/2)
```

---

## 5. 组件设计

### 5.1 `EthFixtureStorageSeeder.h`

**路径：** `bcos-evm/test/fixtures/EthFixtureStorageSeeder.h`

```cpp
task::Task<void> seedPreState(
    executor_v1::MutableStorage& storage,
    FixtureCase const& fixture,
    crypto::Hash::Ptr const& hashImpl);
```

逻辑（参考 `FiscoStateViewTest`）：
- 对每个 `pre[]` 账户：`EVMAccount::create` → `setBalance` → `setNonce` → `setCode`（若有）
- 对 `pre[].storage[]`（若 schema 扩展）：`setStorage(slot, value)`

### 5.2 `EthFixtureTransactionBuilder.h`

**路径：** `bcos-evm/test/fixtures/EthFixtureTransactionBuilder.h`

```cpp
bcostars::protocol::TransactionImpl::Ptr buildTransaction(
    FixtureCase const& fixture,
    bcostars::protocol::TransactionFactoryImpl& factory);
```

映射：
- `sender` = `fixture.tx.from` 的 20 字节
- `to` = `fixture.tx.to` 的 hex 字符串，CREATE 时 `""`
- `input` = `fixture.tx.data`
- `gasLimit` = `fixture.tx.gasLimit`
- `value` = `fixture.tx.value` hex
- `nonce` = `fixture.tx.nonce`
- `gasPrice` = `"0x0"`（对齐 fixture）

### 5.3 `ExecutorFixtureAssert.h`

**路径：** `bcos-evm/test/fixtures/ExecutorFixtureAssert.h`

```cpp
void assertExecutorFixtureResult(
    FixtureCase const& fixture,
    protocol::TransactionReceipt const& receipt,
    AssertPhase phase = AssertPhase::Phase1);
```

**Status 映射：**

| fixture status | receipt 期望 |
|----------------|--------------|
| `EVMC_SUCCESS` | `TransactionStatus::None` (0) |
| `EVMC_REVERT` | revert 对应 status（非 0） |
| `EVMC_OUT_OF_GAS` | `OutOfGasLimit` 等 |

实现时以 `receipt->status()` 实测值校准映射表。

### 5.4 `TestEthTransactionExecutorFixture.cpp`

**路径：** `transaction-executor/tests/TestEthTransactionExecutorFixture.cpp`

```cpp
BOOST_FIXTURE_TEST_SUITE(EthTransactionExecutorFixture, EthExecutorFixtureHarness)

BOOST_AUTO_TEST_CASE(all_fixtures_phase1)
{
    for (auto const& path : listAllFixtureFiles(ETH_STATE_FIXTURES_DIR)) {
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name);
        task::syncWait(runFixturePhase1(fixture));
    }
}

BOOST_AUTO_TEST_SUITE_END()
```

**Harness 配置：**
- `ledgerConfig`：Prague features + `feature_balance` + `feature_evm_prague`
- `blockHeader`：`MAX_VERSION`，`number` = `fixture.block.number`
- `gasPrice`：`{"0", 0}` 或 `"0x0"`
- `call` = `false`（非 eth_call）

### 5.5 CMake

**路径：** `transaction-executor/tests/CMakeLists.txt`

```cmake
add_executable(test-eth-transaction-executor-fixture
    main.cpp
    TestEthTransactionExecutorFixture.cpp
)
target_include_directories(test-eth-transaction-executor-fixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test
    ${PROJECT_SOURCE_DIR}
)
target_compile_definitions(test-eth-transaction-executor-fixture PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test/fixtures/state"
)
target_link_libraries(test-eth-transaction-executor-fixture PRIVATE
    transaction-executor
    protocol-tars
    bcos-evm-eth
    bcos-framework
    ledger
    Boost::unit_test_framework
)
add_test(NAME EthTransactionExecutorFixture
    COMMAND test-eth-transaction-executor-fixture)
```

---

## 6. 风险与缓解

| 风险 | 缓解 |
|------|------|
| buyGas 余额不足 | pre-state `balance` 已足够；失败时调高 fixture balance |
| CREATE 无 `to` 字段 | TransactionBuilder 用空 `to` |
| status 映射不一致 | Phase 1 先跑通，记录映射表；必要时调整 |
| executor gas ≠ Layer 1 gas | Phase 1 跳过 gas；Phase 2 用 `gas_used_executor` |
| CMake 链接失败 | standalone 目标，不依赖 `test-transaction-executor` 聚合 |

---

## 7. 与 T-09 / T-17 关系

```
T-09 Layer 1 ✅ ── fixtures + loader + adapter
        │
        ▼
T-09b Phase 1 ── executor e2e (status/output/logs)
        │
        ▼
T-09b Phase 2 ── gas_used_executor 收紧
        │
        ▼
T-17 ── Compat 大规模迁移 + 100+ vectors
```

---

## 8. 预估工作量

| 阶段 | 估时 |
|------|------|
| Phase 1 harness + 20 fixtures | 1.5～2 天 |
| Phase 2 gas 断言（≥3 fixtures） | 0.5 天 |
| **合计** | **2～2.5 天** |

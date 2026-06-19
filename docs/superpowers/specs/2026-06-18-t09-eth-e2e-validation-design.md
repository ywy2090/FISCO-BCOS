# T-09 Eth 路径端到端验证 — 设计规格

**日期：** 2026-06-18  
**状态：** 已评审（Layer 2 暂缓）  
**范围选项：** B — 现有 5 个 Prague fixtures + 10～15 个从 `ethereum/tests` 转换的精选向量

---

## 1. 背景与动机

`execution_path=eth` 执行链（`EthTxInputBuilder` → `EthTransactionExecutorImpl` → `executeViaEth` → `EthHost`）已完成基础实现（T-08 覆盖 input 映射），但缺少可 CI 运行的端到端验证。

仓库内已有：

- `bcos-evm/test/fixtures/state/*.json` — 5 个简化 GeneralStateTest 向量
- `PragueStateTest` — 通过 `transition()` 跑通，**未经过** `executeViaEth`
- `EthTxInputBuilderTest` — 仅 input builder 单测

T-09 目标：建立 **fixture 驱动的 `executeViaEth` 库层验证**，引入以太坊官方向量语料，作为 Eth 路径语义正确性的第一道门禁。

---

## 2. 范围

### 2.1 在范围内（T-09）

| 交付物 | 说明 |
|--------|------|
| 共享 fixture loader | 从 `PragueStateTest` 抽取，供多处复用 |
| fixture adapter | fixture → `ExecuteViaEthInput` |
| `ExecuteViaEthFixtureTest` | 库层，跑全部 fixture（~20 cases） |
| 10～15 个 imported fixtures | 从 `ethereum/tests` cherry-pick 并转换 |
| `PragueStateTest` 重构 | 改用共享 loader，行为不变 |
| ctest 注册 | `ExecuteViaEthFixture` 目标 |

### 2.2 不在范围内（延后独立方案）

| 项 | 归属 |
|----|------|
| **`TestEthTransactionExecutor`**（HelloWorld deploy+call、fixture 驱动 executor e2e） | **独立后续 spec**（本文称 T-09b 或 T-17 前置） |
| 完整 `ethereum/tests` 子模块 + 通用解析器 | T-17 |
| Scheduler + `EthTransactionExecutorImpl` 出块 | T-19 |
| 节点 `fisco-bcos-air` + `execution_path=eth` smoke | T-26 |
| `EthTxGasSettlementExecutorTest` 失败项修复 | T-16 |

### 2.3 Done 标准

- [x] `ctest -R ExecuteViaEthFixture` 全绿（≥15 cases，目标 ~20）
- [x] 每个 imported fixture 含 `source` 字段标注 `ethereum/tests` 原始路径
- [x] `PragueStateTest` 重构后仍全绿（回归）
- [x] 设计文档与 fixture 清单可供 T-09b（executor e2e）直接复用

---

## 3. 架构

```
fixtures/state/*.json          ─┐
fixtures/state/imported/*.json ─┤
                                ▼
                    EthStateFixtureLoader.h
                                │
                    EthFixtureAdapter.h
                                │
                    InMemoryStateView (pre-state)
                                │
                                ▼
                         executeViaEth()
                                │
                    断言 status / output / logs / gas_used
```

**验证边界：** 覆盖 `executeViaEth` 编排层（`executeMessage` + `EthHostExtension` + gas settlement 前置），**不**覆盖 `EthTransactionExecutorImpl` 的 buyGas / refundGas / receipt 工厂。

---

## 4. 组件设计

### 4.1 `EthStateFixtureLoader.h`

**路径：** `bcos-evm/test/fixtures/EthStateFixtureLoader.h`

从 `PragueStateTest.cpp` 抽取：

- `ExpectedResult` — status, gasUsed, output, logs
- `FixtureCase` — name, source, tx, block, txProps, preState, expected
- `loadFixture(path)` — 解析 JSON
- `listFixtureFiles(rootDir)` — 枚举 `rootDir/*.json` 与 `rootDir/imported/*.json`

**JSON schema：**

```json
{
  "name": "string (required)",
  "source": "string (optional, imported fixtures required)",
  "revision": "prague",
  "tx": {
    "from": "0x...",
    "to": "0x... (optional for create)",
    "gas_limit": 21000,
    "gas_price": "0x0",
    "value": "0x0",
    "nonce": 0,
    "data": "0x"
  },
  "tx_props": {
    "warm_coinbase": true,
    "warm_destination": true,
    "is_static": false
  },
  "block": {
    "number": 1,
    "timestamp": 1,
    "gas_limit": 30000000,
    "coinbase": "0x...",
    "base_fee": "0x0",
    "chain_id": "0x1"
  },
  "pre": [
    {
      "address": "0x...",
      "balance": "0x...",
      "nonce": 0,
      "code": "0x",
      "storage": [{ "slot": "0x...", "value": "0x..." }]
    }
  ],
  "expected": {
    "status": "EVMC_SUCCESS | EVMC_REVERT | EVMC_OUT_OF_GAS",
    "output": "0x",
    "gas_used": 0,
    "gas_used_tolerance": 0,
    "logs": 0
  }
}
```

**字段说明：**

- `pre[].storage` — 可选，imported 向量按需添加
- `expected.gas_used` — 为 `0` 时跳过 gas 断言（兼容现有 5 个 placeholder）
- `expected.gas_used_tolerance` — 非零时允许 `|actual - expected| <= tolerance`

### 4.2 `EthFixtureAdapter.h`

**路径：** `bcos-evm/test/fixtures/EthFixtureAdapter.h`

职责：将 `FixtureCase` 转为 `ExecuteViaEthInput`：

- `fixture.tx` → `evmc_message`（sender, recipient, gas, value, input, kind）
  - 无 `to` → `EVMC_CREATE`
  - 有 `to` → `EVMC_CALL`
- `fixture.block` → `state::BlockInfo`
- `fixture.revision` → `evmc_revision` + `RevisionConfig`
- 空 `BlockHashes` lambda（与 `PragueStateTest` 一致）

### 4.3 `ExecuteViaEthFixtureTest.cpp`

**路径：** `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp`

```cpp
for (auto const& path : listFixtureFiles(ETH_STATE_FIXTURES_DIR)) {
    auto fixture = loadFixture(path);
    BOOST_TEST_CONTEXT("fixture=" << fixture.name);
    // setup InMemoryStateView from preState
    // build ExecuteViaEthInput via EthFixtureAdapter
    auto output = task::syncWait(executeViaEth(input));
    // assert expected
}
```

**断言策略：**

| 字段 | 规则 |
|------|------|
| `status` | 必须精确匹配 |
| `output` | 必须精确匹配 |
| `logs` | 有 expected 则匹配数量 |
| `gas_used` | `expected.gas_used != 0` 时断言；支持 `tolerance` |

### 4.4 `PragueStateTest` 重构

- 删除内联 `loadFixture` / 解析函数
- `#include "fixtures/EthStateFixtureLoader.h"`
- `fixtureFiles()` 改为 `listFixtureFiles` 仅根目录 5 个文件（或过滤不含 `imported/`）
- 执行路径保持 `transition()`，**不改为** `executeViaEth`（避免破坏现有回归语义）

---

## 5. Fixture 语料

### 5.1 现有（5 个，保留位置不变）

`bcos-evm/test/fixtures/state/`：

- `prague_call_empty_account.json`
- `prague_call_return_word.json`
- `prague_call_revert.json`
- `prague_create_empty_initcode.json`
- `prague_selfdestruct.json`

### 5.2 新增 imported（10～15 个）

**目录：** `bcos-evm/test/fixtures/state/imported/`

| # | 文件名（建议） | 参考来源 | 验证点 |
|---|----------------|----------|--------|
| 1 | `stExample_add.json` | `GeneralStateTests/stExample/add.json` | 简单算术 call |
| 2 | `stExample_gasPrice0.json` | `stExample` 变体 | 零 gas price call |
| 3 | `stRevert_revertBasic.json` | `stRevertTest` | `EVMC_REVERT` |
| 4 | `stRevert_revertDepth.json` | `stRevertTest` | 嵌套 revert |
| 5 | `stCreate_initCode.json` | `stCreateTest` | CREATE 成功 |
| 6 | `stCreate2_basic.json` | `stCreate2` | CREATE2 |
| 7 | `stPrecompile_ecrecover.json` | `stPreCompiledContracts` | ecrecover |
| 8 | `stPrecompile_identity.json` | `stPreCompiledContracts` | identity |
| 9 | `stPrecompile_sha256.json` | `stPreCompiledContracts` | sha256 |
| 10 | `stModExp_basic.json` | `stModExp` | modexp |
| 11 | `stBLS_add.json` | Prague BLS precompile | BLS12-381 G1 add |
| 12 | `stBLS_map.json` | Prague BLS precompile | BLS12-381 map |
| 13 | `stSelfDestruct_basic.json` | `stSelfBalance` / Prague | selfdestruct 变体 |
| 14 | `stEIP2930_accessList.json` | `stEIP2930` | access list warm（按需扩展 schema） |
| 15 | `stEIP7702_delegation.json` | `stEIP7702` | delegation code |

**转换策略：**

- 不引入 `ethereum/tests` git submodule
- 一次性 Python 辅助脚本 `tools/convert_eth_state_fixture.py`（可选，非阻塞）
- 手工校验每个向量的 `expected` 值（先本地跑通再入库）
- 每个 imported 文件 **必须** 含 `"source"` 字段

---

## 6. CMake 与 CI

### 6.1 新增目标（`bcos-evm/test/CMakeLists.txt`）

```cmake
add_executable(ExecuteViaEthFixtureTest
    eth/ExecuteViaEthFixtureTest.cpp
)
target_include_directories(ExecuteViaEthFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_compile_definitions(ExecuteViaEthFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state"
)
target_link_libraries(ExecuteViaEthFixtureTest PRIVATE
    bcos-evm
    evmone::evmone
    bcos-task
    Boost::unit_test_framework
)
add_test(NAME ExecuteViaEthFixture COMMAND ExecuteViaEthFixtureTest)
```

### 6.2 运行命令

```bash
cd build && cmake --build . --target ExecuteViaEthFixtureTest -j$(nproc)
ctest -R ExecuteViaEthFixture --output-on-failure
```

---

## 7. 与相邻任务关系

```
T-09 (本文) ── fixture harness + executeViaEth 库层 (~20 cases)
    │
    ├── 复用基础 ──► T-09b / 独立 spec：EthTransactionExecutorImpl executor e2e
    │
    ├── 扩展语料 ──► T-17：大规模 Compat + 100+ GeneralStateTest
    │
    └── 集成验证 ──► T-19：Scheduler 三分支
```

**T-09 为 T-09b 预埋：**

- `EthStateFixtureLoader` + `EthFixtureAdapter` 可直接用于 executor 层 adapter（fixture → `TransactionFactory`）
- imported 语料库可复用，无需重复转换

---

## 8. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 官方向量转换 `expected` 值不准 | 先本地 evmone 对照跑一遍；gas 用 tolerance |
| `executeViaEth` 与 `transition()` 语义差异 | `PragueStateTest` 保持 `transition()` 回归；新测试专测 `executeViaEth` |
| access list / 7702 fixture 需扩展 schema | 仅当 cherry-pick 的向量确实需要时才扩展；否则选手工简化向量 |
| imported 向量过多导致 CI 变慢 | 硬上限 15 个；T-17 再扩展 |

---

## 9. 预估工作量

| 项 | 估时 |
|----|------|
| Harness 抽取 + adapter | 0.5 天 |
| `ExecuteViaEthFixtureTest` 骨架 | 0.5 天 |
| 10～15 向量转换 + 调试 | 1～1.5 天 |
| `PragueStateTest` 重构 + 回归 | 0.25 天 |
| ctest + 文档 | 0.25 天 |
| **合计** | **2.5～3 天** |

---

## 10. Layer 2 暂缓说明（独立后续方案）

以下内容 **不在 T-09 实现**，将单独出 spec：

- `transaction-executor/tests/TestEthTransactionExecutor.cpp`
- HelloWorld deploy + call
- fixture → `EthTransactionExecutorImpl.executeTransaction()` adapter
- gas settlement / receipt 断言
- standalone `test-eth-transaction-executor` 目标（若聚合链接失败）

T-09 完成后，T-09b spec 可直接引用本文第 4、5 节的 harness 与语料。

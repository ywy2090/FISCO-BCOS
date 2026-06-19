# T-09 Eth 路径端到端验证 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 fixture 驱动的 `executeViaEth` 库层验证，复用/扩展 5 个现有 Prague JSON + 10～15 个 imported 向量，`ctest -R ExecuteViaEthFixture` 全绿。

**Architecture:** 从 `PragueStateTest.cpp` 抽取 `EthStateFixtureLoader.h`；新增 `EthFixtureAdapter.h` 将 fixture 转为 `ExecuteViaEthInput`；`ExecuteViaEthFixtureTest` 遍历 `fixtures/state/` 与 `fixtures/state/imported/` 调用 `executeViaEth` 并断言。Layer 2（`EthTransactionExecutorImpl`）不在本计划范围。

**Tech Stack:** C++20、Boost.Test、Boost.PropertyTree、evmone、bcos-evm、`bcos-task::syncWait`

## Global Constraints

- Layer 2（`TestEthTransactionExecutor`）**暂缓**，独立后续 spec
- 不引入 `ethereum/tests` git submodule；imported 向量以 vendored JSON 入库
- `PragueStateTest` 保持 `transition()` 路径，仅重构 loader
- imported fixture **必须**含 `"source"` 字段
- `expected.gas_used == 0` 时跳过 gas 断言
- imported 向量硬上限 **15** 个
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）

---

## File Map

| 文件 | 职责 |
|------|------|
| `bcos-evm/test/fixtures/EthStateFixtureLoader.h` | JSON 解析、fixture 枚举 |
| `bcos-evm/test/fixtures/EthFixtureAdapter.h` | fixture → `ExecuteViaEthInput` |
| `bcos-evm/test/fixtures/FixtureAssert.h` | 共享断言辅助 |
| `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp` | 主测试 |
| `bcos-evm/test/state/PragueStateTest.cpp` | 改用共享 loader |
| `bcos-evm/test/fixtures/state/imported/*.json` | 10～15 官方向量 |
| `bcos-evm/test/CMakeLists.txt` | 新 target + ctest |
| `tools/convert_eth_state_fixture.py` | 可选转换脚本 |

---

### Task 1: 抽取 `EthStateFixtureLoader.h`

**Files:**
- Create: `bcos-evm/test/fixtures/EthStateFixtureLoader.h`
- Modify: `bcos-evm/test/state/PragueStateTest.cpp`

**Interfaces — Produces:**
```cpp
namespace bcos::evm::test::fixtures {
struct ExpectedResult {
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gasUsed = 0;
    int64_t gasUsedTolerance = 0;
    bcos::bytes output;
    size_t logs = 0;
};
struct FixtureCase {
    std::string name;
    std::string source;
    std::string revision;
    state::Transaction tx;
    state::BlockInfo block;
    state::TransactionProperties txProps;
    std::vector<std::pair<evmc_address, state::Account>> preState;
    ExpectedResult expected;
};
FixtureCase loadFixture(std::filesystem::path const& path);
std::vector<std::filesystem::path> listRootFixtureFiles(std::filesystem::path const& rootDir);
std::vector<std::filesystem::path> listAllFixtureFiles(std::filesystem::path const& rootDir);
bool sameBytes(bcos::bytes const& lhs, bcos::bytes const& rhs);
}
```

- [ ] **Step 1:** 创建 `EthStateFixtureLoader.h`，从 `PragueStateTest.cpp` 迁移 `parseStatus/parseAddress/parseU256/parseBytes/parseBlock/loadFixture`，并新增 `gas_used_tolerance` 解析与 `source` 字段

- [ ] **Step 2:** 实现 `listRootFixtureFiles`（仅根目录 5 个文件，供 `PragueStateTest`）与 `listAllFixtureFiles`（根目录 `*.json` + `imported/*.json`）

- [ ] **Step 3:** 重构 `PragueStateTest.cpp`：
```cpp
#include "fixtures/EthStateFixtureLoader.h"
using namespace bcos::evm::test::fixtures;
// fixtureFiles() → listRootFixtureFiles(PRAGUE_STATE_FIXTURES_DIR)
// loadFixture → fixtures::loadFixture
// sameBytes → fixtures::sameBytes
```

- [ ] **Step 4:** 构建并回归
```bash
cd build && cmake --build . --target PragueStateTest -j$(sysctl -n hw.ncpu)
./bcos-evm/test/PragueStateTest
```
Expected: 5/5 PASS

- [ ] **Step 5:** Commit
```bash
rtk git add bcos-evm/test/fixtures/EthStateFixtureLoader.h bcos-evm/test/state/PragueStateTest.cpp
rtk git commit -m "refactor(test): extract EthStateFixtureLoader for shared fixture parsing"
```

---

### Task 2: `EthFixtureAdapter.h` + 断言辅助

**Files:**
- Create: `bcos-evm/test/fixtures/EthFixtureAdapter.h`
- Create: `bcos-evm/test/fixtures/FixtureAssert.h`

**Interfaces — Consumes:** `FixtureCase`, `loadFixture`, `sameBytes`  
**Interfaces — Produces:**
```cpp
bcos::evm_standard::RevisionConfig makePragueRevisionConfig();
ExecuteViaEthInput buildExecuteViaEthInput(
    FixtureCase const& fixture, state::StateView const& stateView,
    evmc::VM& vm, bcos::crypto::Hash const& hashImpl);
void assertFixtureResult(FixtureCase const& fixture, ExecuteViaEthOutput const& output, int64_t gasBefore);
```

- [ ] **Step 1:** 实现 `makePragueRevisionConfig()`（`EVMC_PRAGUE` + `eip2537/eip7623/eip7702/warm_access` 等同 `EthPolicy::computeRevisionConfig` 对 Prague 块的标志）

- [ ] **Step 2:** 实现 `buildExecuteViaEthInput`：
  - `tx.to` 有值 → `EVMC_CALL`；无值 → `EVMC_CREATE`
  - `message.sender = tx.from`；`recipient/code_address = tx.to`
  - `message.gas = tx.gasLimit`；value/data 从 fixture 填充
  - `blockInfo` 从 `fixture.block` 映射
  - `blockHashes = [](int64_t){ return evmc_bytes32{}; }`
  - `revisionConfig = makePragueRevisionConfig()`

- [ ] **Step 3:** 实现 `FixtureAssert.h`：
```cpp
inline void assertFixtureResult(FixtureCase const& f, ExecuteViaEthOutput const& out, int64_t gasBefore) {
    BOOST_CHECK_EQUAL(static_cast<int>(out.evmcResult.status_code), static_cast<int>(f.expected.status));
    bcos::bytes actual(out.evmcResult.output_data, out.evmcResult.output_data + out.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, f.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x" << bcos::toHex(f.expected.output));
    BOOST_CHECK_EQUAL(out.executionContext.logs.size(), f.expected.logs);
    if (f.expected.gasUsed != 0) {
        int64_t const actualGas = gasBefore - out.evmcResult.gas_left;
        int64_t const diff = std::abs(actualGas - f.expected.gasUsed);
        BOOST_CHECK_LE(diff, f.expected.gasUsedTolerance);
    }
}
```

- [ ] **Step 4:** Commit（仅 header，尚无独立测试目标）
```bash
rtk git add bcos-evm/test/fixtures/EthFixtureAdapter.h bcos-evm/test/fixtures/FixtureAssert.h
rtk git commit -m "feat(test): add EthFixtureAdapter and fixture assertion helpers"
```

---

### Task 3: `ExecuteViaEthFixtureTest`（现有 5 fixtures）

**Files:**
- Create: `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1:** 编写测试（先写失败测试）
```cpp
#define BOOST_TEST_MODULE ExecuteViaEthFixtureTest
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "fixtures/EthFixtureAdapter.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/FixtureAssert.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test {
using namespace fixtures;

BOOST_AUTO_TEST_CASE(existing_prague_fixtures_via_execute_via_eth) {
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto const files = listAllFixtureFiles(
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
    );
    BOOST_REQUIRE_GE(files.size(), 5u);
    for (auto const& path : files) {
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string()) {
            state::test::InMemoryStateView view;
            for (auto const& [addr, acct] : fixture.preState) view.insert_account(addr, acct);
            auto input = buildExecuteViaEthInput(fixture, view, vm, hashImpl);
            int64_t const gasBefore = input.message.gas;
            auto output = task::syncWait(executeViaEth(std::move(input)));
            assertFixtureResult(fixture, output, gasBefore);
        }
    }
}
}
```

- [ ] **Step 2:** CMake 追加（`bcos-evm/test/CMakeLists.txt` 末尾）：
```cmake
add_executable(ExecuteViaEthFixtureTest eth/ExecuteViaEthFixtureTest.cpp)
target_include_directories(ExecuteViaEthFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(ExecuteViaEthFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(ExecuteViaEthFixtureTest PRIVATE
    bcos-evm evmone::evmone bcos-task bcos-crypto Boost::unit_test_framework)
add_test(NAME ExecuteViaEthFixture COMMAND ExecuteViaEthFixtureTest)
```

- [ ] **Step 3:** 构建运行
```bash
cd build && cmake .. && cmake --build . --target ExecuteViaEthFixtureTest -j$(sysctl -n hw.ncpu)
./bcos-evm/test/ExecuteViaEthFixtureTest
```
Expected: 5/5 PASS（若 `prague_call_return_word` gas 断言失败，将该 fixture `gas_used` 改为 `0` 或填入实测值）

- [ ] **Step 4:** `rtk git add` 测试文件 + CMakeLists，`rtk git commit -m "feat(test): add ExecuteViaEthFixtureTest for existing Prague fixtures"`

---

### Task 4: Imported fixtures 批次 1（revert + precompile，5 个）

**Files:**
- Create: `bcos-evm/test/fixtures/state/imported/stRevert_revertBasic.json`
- Create: `bcos-evm/test/fixtures/state/imported/stPrecompile_identity.json`
- Create: `bcos-evm/test/fixtures/state/imported/stPrecompile_sha256.json`
- Create: `bcos-evm/test/fixtures/state/imported/stExample_return42.json`
- Create: `bcos-evm/test/fixtures/state/imported/stCall_emptyAccount.json`

**方法：** 每个 JSON 先以最小 hand-crafted 向量入库；`expected` 用首次运行实测值填写。

- [ ] **Step 1:** 添加 `stExample_return42.json`（示例）
```json
{
  "name": "stExample_return42",
  "source": "hand-crafted/from GeneralStateTests/stExample",
  "revision": "prague",
  "tx": {
    "from": "0x0000000000000000000000000000000000000001",
    "to": "0x00000000000000000000000000000000000000aa",
    "gas_limit": 100000,
    "gas_price": "0x0",
    "value": "0x0",
    "nonce": 1,
    "data": "0x"
  },
  "block": { "number": 1, "timestamp": 1, "gas_limit": 30000000,
    "coinbase": "0x00000000000000000000000000000000000000cb", "base_fee": "0x0", "chain_id": "0x1" },
  "pre": [
    { "address": "0x0000000000000000000000000000000000000001", "balance": "0x1000000", "nonce": 1, "code": "0x" },
    { "address": "0x00000000000000000000000000000000000000aa", "balance": "0x0", "nonce": 0,
      "code": "0x602a60005260206000f3" }
  ],
  "expected": { "status": "EVMC_SUCCESS", "gas_used": 0, "logs": 0,
    "output": "0x000000000000000000000000000000000000000000000000000000000000002a" }
}
```

- [ ] **Step 2:** 添加其余 4 个（revert: `code=0xfd`；identity/sha256: 标准 precompile 输入；empty call: 无 code 目标）

- [ ] **Step 3:** 运行测试，对 `gas_used==0` 且需精确 gas 的向量填入实测值 + `gas_used_tolerance`（如需）

- [ ] **Step 4:** `rtk git commit -m "test(eth): add imported state fixtures batch 1 (5 cases)"`

---

### Task 5: Imported fixtures 批次 2（create + modexp + BLS，5 个）

**Files:**
- Create: `imported/stCreate_initCode.json`
- Create: `imported/stCreate2_basic.json`
- Create: `imported/stModExp_basic.json`
- Create: `imported/stBLS_add.json`
- Create: `imported/stSelfDestruct_basic.json`

- [ ] **Step 1:** 编写 5 个 JSON（含 `source` 字段）

- [ ] **Step 2:** 运行 `ExecuteViaEthFixtureTest`，修复失败向量（调整 pre-state code/data 或 expected）

- [ ] **Step 3:** 确认总数 ≥ 10 imported + 5 root = **15 cases**

- [ ] **Step 4:** Commit

---

### Task 6: Imported fixtures 批次 3（补齐至 15 imported，可选脚本）

**Files:**
- Create: up to 5 more under `imported/`（`stPrecompile_ecrecover.json`, `stRevert_revertDepth.json`, `stEIP2930_accessList.json`, `stEIP7702_delegation.json`, `stExample_gasPrice0.json`）
- Create (optional): `tools/convert_eth_state_fixture.py`

- [ ] **Step 1:** 补齐 imported 至 **10～15** 个（总数 **15～20**）

- [ ] **Step 2（可选）:** 添加转换脚本骨架（读取简化 intermediate JSON，输出本仓库 schema）

- [ ] **Step 3:** 全量回归
```bash
rtk test ./bcos-evm/test/ExecuteViaEthFixtureTest
rtk test ./bcos-evm/test/PragueStateTest
cd build && ctest -R "ExecuteViaEthFixture|PragueState" --output-on-failure
```

- [ ] **Step 4:** Commit

---

### Task 7: Done 验收

- [ ] **Step 1:** 确认 Done 清单
  - `ctest -R ExecuteViaEthFixture` 全绿，cases ≥ 15
  - 每个 `imported/*.json` 含 `source`
  - `PragueStateTest` 全绿

- [ ] **Step 2:** 在 spec 文件勾选 Done 标准（或更新 `remaining-tasks.md` 标记 T-09 完成）

- [ ] **Step 3:** 最终 commit
```bash
rtk git commit -m "test(eth): complete T-09 executeViaEth fixture e2e validation"
```

---

## Spec Self-Review

| Spec 要求 | 对应 Task |
|-----------|-----------|
| EthStateFixtureLoader | Task 1 |
| EthFixtureAdapter | Task 2 |
| ExecuteViaEthFixtureTest | Task 3 |
| 10～15 imported fixtures | Task 4–6 |
| PragueStateTest 重构 | Task 1 |
| ctest 注册 | Task 3 |
| Layer 2 暂缓 | Global Constraints |
| source 字段 | Task 4–6 |
| gas_used 跳过规则 | Task 2 FixtureAssert |

**无 TBD/占位符。**

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-06-18-t09-eth-e2e-validation.md`.

**两种执行方式：**

1. **Subagent-Driven（推荐）** — 每 Task 派发独立 subagent，Task 间做 review  
2. **Inline Execution** — 本会话按 Task 顺序实现，checkpoint 验收

**选哪种？**

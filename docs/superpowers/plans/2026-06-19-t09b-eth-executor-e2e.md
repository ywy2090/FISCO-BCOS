# T-09b Eth Executor 级 E2E 验证 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 复用 T-09 的 20 个 fixture，经 `EthTransactionExecutorImpl::executeTransaction` 全链路验证 receipt 语义；Phase 1 断言 status/output/logs，Phase 2 对精选 fixture 增加 `gas_used_executor`。

**Architecture:** 在 `bcos-evm/test/fixtures/` 新增 StorageSeeder + TransactionBuilder + ExecutorFixtureAssert；`transaction-executor/tests/TestEthTransactionExecutorFixture.cpp` 遍历 fixture，播种 `MutableStorage` 后执行 Eth 执行器。

**Tech Stack:** C++20、Boost.Test、bcos-tars-protocol、`EthTransactionExecutorImpl`、`EVMAccount`、`bcos-task`

## Global Constraints

- 不新增 fixture JSON（复用 T-09 20 个）
- Phase 1 **跳过** `gas_used` / `receipt->gasUsed()` 断言
- standalone 目标 `test-eth-transaction-executor-fixture`，不加入 `test-transaction-executor` UNITY 聚合
- `call=false`（真实交易，非 eth_call）
- `ledgerConfig` 必须开启 Prague + balance features（参考 `EthTxGasSettlementExecutorTest::setPragueFeatures`）
- 命令前缀使用 `rtk`（仓库 CLAUDE.md）
- Layer 1 `ExecuteViaEthFixtureTest` 不得回归破坏

---

## File Map

| 文件 | 职责 |
|------|------|
| `bcos-evm/test/fixtures/EthFixtureStorageSeeder.h` | pre-state → `MutableStorage` |
| `bcos-evm/test/fixtures/EthFixtureTransactionBuilder.h` | fixture → `TransactionImpl` |
| `bcos-evm/test/fixtures/ExecutorFixtureAssert.h` | receipt 断言 Phase 1/2 |
| `transaction-executor/tests/TestEthTransactionExecutorFixture.cpp` | 主测试 |
| `transaction-executor/tests/CMakeLists.txt` | standalone target + ctest |
| `bcos-evm/test/fixtures/EthStateFixtureLoader.h` | Phase 2 扩展 `gasUsedExecutor` 字段 |
| `bcos-evm/test/fixtures/state/*.json` | Phase 2 为 6 个候选加 `gas_used_executor` |

---

### Task 1: `EthFixtureStorageSeeder.h`

**Files:**
- Create: `bcos-evm/test/fixtures/EthFixtureStorageSeeder.h`

**Interfaces — Produces:**
```cpp
namespace bcos::evm::test::fixtures {
task::Task<void> seedPreState(
    executor_v1::MutableStorage& storage,
    FixtureCase const& fixture,
    crypto::Hash::Ptr const& hashImpl);
}
```

- [ ] **Step 1:** 创建 header，include `EthStateFixtureLoader.h`、`TestMemoryStorage.h`（或 `MutableStorage` 类型）、`EVMAccount.h`、`bcos-task/Wait.h`

- [ ] **Step 2:** 实现 `seedPreState`：

```cpp
inline task::Task<void> seedPreState(
    executor_v1::MutableStorage& storage,
    FixtureCase const& fixture,
    crypto::Hash::Ptr const& hashImpl)
{
    for (auto const& [address, account] : fixture.preState)
    {
        ledger::account::EVMAccount<decltype(storage)> evmAccount(storage, address, false);
        if (!co_await evmAccount.exists())
        {
            co_await evmAccount.create();
        }
        co_await evmAccount.setBalance(account.balance);
        co_await evmAccount.setNonce(std::to_string(account.nonce));
        if (!account.code.empty())
        {
            auto const codeHash = hashImpl->hash(
                bcos::bytesConstRef(account.code.data(), account.code.size()));
            co_await evmAccount.setCode(account.code, account.abi, codeHash);
        }
    }
}
```

- [ ] **Step 3:** 编译烟测 — 在 `TestEthTransactionExecutorFixture.cpp` 骨架中临时 `#include` 并调用 `seedPreState`（Task 4 前可用单行编译验证）

- [ ] **Step 4:** Commit

```bash
rtk git add bcos-evm/test/fixtures/EthFixtureStorageSeeder.h
rtk git commit -m "feat(test): add EthFixtureStorageSeeder for executor fixture harness"
```

---

### Task 2: `EthFixtureTransactionBuilder.h`

**Files:**
- Create: `bcos-evm/test/fixtures/EthFixtureTransactionBuilder.h`

**Interfaces — Consumes:** `FixtureCase`, `TransactionFactoryImpl`  
**Interfaces — Produces:**
```cpp
bcostars::protocol::TransactionImpl::Ptr buildFixtureTransaction(
    FixtureCase const& fixture,
    bcostars::protocol::TransactionFactoryImpl& factory);
```

- [ ] **Step 1:** 实现地址/数值辅助：

```cpp
inline std::string addressToHexLower(evmc_address const& addr)
{
    return bcos::toHex(std::span(addr.bytes, sizeof(addr.bytes)));
}

inline std::string u256ToHex(bcos::u256 const& v)
{
    if (v == 0) return "0x0";
    return "0x" + bcos::toHex(bcos::u256ToBytes(v));
}
```

- [ ] **Step 2:** 实现 `buildFixtureTransaction`：

```cpp
inline bcostars::protocol::TransactionImpl::Ptr buildFixtureTransaction(
    FixtureCase const& fixture,
    bcostars::protocol::TransactionFactoryImpl& factory)
{
    std::string to;
    if (fixture.tx.to.has_value())
    {
        to = addressToHexLower(*fixture.tx.to);
    }
    auto tx = factory.createTransaction(
        0,                                          // version
        to,                                         // to (empty = CREATE)
        fixture.tx.data,                            // input
        std::to_string(fixture.tx.nonce),           // nonce
        999'999'999,                                // blockLimit
        "1",                                        // chainId
        "",                                         // groupId
        0,                                          // importTime
        "",                                         // abi
        u256ToHex(fixture.tx.value),                // value
        "0x0",                                      // gasPrice
        fixture.tx.gasLimit,                        // gasLimit
        "0x0",                                      // maxFeePerGas
        "0x0");                                     // maxPriorityFeePerGas
    tx->forceSender(bcos::bytes(
        fixture.tx.from.bytes, fixture.tx.from.bytes + sizeof(fixture.tx.from.bytes)));
    return std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
}
```

- [ ] **Step 3:** Commit

```bash
rtk git add bcos-evm/test/fixtures/EthFixtureTransactionBuilder.h
rtk git commit -m "feat(test): add EthFixtureTransactionBuilder for executor fixtures"
```

---

### Task 3: `ExecutorFixtureAssert.h`（Phase 1）

**Files:**
- Create: `bcos-evm/test/fixtures/ExecutorFixtureAssert.h`

**Interfaces — Produces:**
```cpp
enum class AssertPhase { Phase1, Phase2 };
void assertExecutorFixtureResult(
    FixtureCase const& fixture,
    bcos::protocol::TransactionReceipt const& receipt,
    AssertPhase phase = AssertPhase::Phase1);
```

- [ ] **Step 1:** 实现 status 映射辅助：

```cpp
inline int32_t expectedReceiptStatus(evmc_status_code evmcStatus)
{
    using bcos::protocol::TransactionStatus;
    switch (evmcStatus)
    {
    case EVMC_SUCCESS:
        return static_cast<int32_t>(TransactionStatus::None);
    case EVMC_REVERT:
        return static_cast<int32_t>(TransactionStatus::RevertInstruction);
    case EVMC_OUT_OF_GAS:
        return static_cast<int32_t>(TransactionStatus::OutOfGasLimit);
    default:
        return static_cast<int32_t>(TransactionStatus::Unknown);
    }
}
```

> 若实测 REVERT 映射为其他 `TransactionStatus`，以首次失败测试输出为准修正此表。

- [ ] **Step 2:** 实现 Phase 1 断言：

```cpp
inline void assertExecutorFixtureResult(
    FixtureCase const& fixture,
    bcos::protocol::TransactionReceipt const& receipt,
    AssertPhase phase = AssertPhase::Phase1)
{
    BOOST_CHECK_EQUAL(receipt.status(), expectedReceiptStatus(fixture.expected.status));
    BOOST_CHECK_MESSAGE(sameBytes(receipt.output(), fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(receipt.output())
        << " expected=0x" << bcos::toHex(fixture.expected.output));
    BOOST_CHECK_EQUAL(receipt.logEntries().size(), fixture.expected.logs);
    if (phase == AssertPhase::Phase2 && fixture.expected.gasUsedExecutor != 0)
    {
        int64_t const actual = static_cast<int64_t>(receipt.gasUsed());
        int64_t const diff = std::abs(actual - fixture.expected.gasUsedExecutor);
        BOOST_CHECK_LE(diff, fixture.expected.gasUsedExecutorTolerance);
    }
}
```

- [ ] **Step 3:** Commit

```bash
rtk git add bcos-evm/test/fixtures/ExecutorFixtureAssert.h
rtk git commit -m "feat(test): add ExecutorFixtureAssert for eth executor fixtures"
```

---

### Task 4: Phase 1 测试 + CMake

**Files:**
- Create: `transaction-executor/tests/TestEthTransactionExecutorFixture.cpp`
- Modify: `transaction-executor/tests/CMakeLists.txt`

- [ ] **Step 1:** 编写 harness + 测试：

```cpp
#include "../bcos-transaction-executor/EthTransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "fixtures/EthFixtureStorageSeeder.h"
#include "fixtures/EthFixtureTransactionBuilder.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/ExecutorFixtureAssert.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <intx/intx.hpp>

namespace bcos::evm::test {
using namespace fixtures;

class EthExecutorFixtureHarness {
public:
    executor_v1::MutableStorage storage;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite =
        std::make_shared<crypto::CryptoSuite>(
            std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    executor_v1::EthTransactionExecutorImpl executor{
        receiptFactory, cryptoSuite->hashImpl()};
    int contextId = 0;

    EthExecutorFixtureHarness() {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
    }

    ledger::LedgerConfig makeLedgerConfig(FixtureCase const& fixture) {
        ledger::LedgerConfig cfg;
        ledger::Features features;
        features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        features.set(ledger::Features::Flag::feature_evm_prague);
        features.set(ledger::Features::Flag::feature_evm_eip2929);
        features.set(ledger::Features::Flag::feature_balance);
        features.set(ledger::Features::Flag::feature_balance_policy1);
        cfg.setFeatures(features);
        cfg.setGasLimit({fixture.block.gasLimit, 0});
        cfg.setGasPrice({"0", 0});
        evmc_uint256be chainId{};
        intx::be::store(chainId.bytes, intx::uint256{fixture.block.chainId});
        cfg.setChainId(chainId);
        return cfg;
    }

    bcostars::protocol::BlockHeaderImpl makeBlockHeader(FixtureCase const& fixture) {
        bcostars::protocol::BlockHeaderImpl header;
        header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        header.setNumber(fixture.block.number);
        header.setTimestamp(fixture.block.timestamp);
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }
};

BOOST_FIXTURE_TEST_SUITE(EthTransactionExecutorFixture, EthExecutorFixtureHarness)

BOOST_AUTO_TEST_CASE(all_fixtures_phase1) {
    auto const files = listAllFixtureFiles(
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
    );
    BOOST_REQUIRE_EQUAL(files.size(), 20u);
    for (auto const& path : files) {
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string()) {
            task::syncWait([&, fixture]() -> task::Task<void> {
                co_await seedPreState(storage, fixture, cryptoSuite->hashImpl());
                auto tx = buildFixtureTransaction(fixture, transactionFactory);
                auto header = makeBlockHeader(fixture);
                auto ledgerConfig = makeLedgerConfig(fixture);
                auto receipt = co_await executor.executeTransaction(
                    storage, header, *tx, contextId++, ledgerConfig, false);
                BOOST_REQUIRE(receipt);
                assertExecutorFixtureResult(fixture, *receipt, AssertPhase::Phase1);
            }());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
```

- [ ] **Step 2:** CMake 追加（`transaction-executor/tests/CMakeLists.txt` 末尾）：

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
    executor
    Boost::unit_test_framework
)
add_dependencies(test-eth-transaction-executor-fixture executor)
add_test(NAME EthTransactionExecutorFixture
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND test-eth-transaction-executor-fixture)
```

- [ ] **Step 3:** 构建运行

```bash
cd build && cmake .. && cmake --build . --target test-eth-transaction-executor-fixture -j$(sysctl -n hw.ncpu)
./transaction-executor/tests/test-eth-transaction-executor-fixture
ctest -R EthTransactionExecutorFixture --output-on-failure
```

Expected: 20/20 PASS（1 test case）

- [ ] **Step 4:** 若个别 fixture 失败，按失败类型修复（不扩大 scope）：
  - **buyGas 余额不足** → 调高该 fixture `pre[].balance`
  - **status 映射错误** → 修正 `expectedReceiptStatus`
  - **CREATE output 非空** → CREATE 成功时 `expected.output` 可能需为空（合约地址在 `receipt->contractAddress()`，Phase 1 不断言）

- [ ] **Step 5:** 回归 Layer 1

```bash
./bcos-evm/test/ExecuteViaEthFixtureTest
```

- [ ] **Step 6:** Commit

```bash
rtk git add transaction-executor/tests/TestEthTransactionExecutorFixture.cpp transaction-executor/tests/CMakeLists.txt
rtk git commit -m "feat(test): add EthTransactionExecutorFixture Phase 1 (20 fixtures)"
```

---

### Task 5: Phase 2 — `gas_used_executor` 断言

**Files:**
- Modify: `bcos-evm/test/fixtures/EthStateFixtureLoader.h`
- Modify: `bcos-evm/test/fixtures/ExecutorFixtureAssert.h`（已在 Task 3 预埋 Phase2 分支）
- Modify: 6 个 fixture JSON
- Modify: `transaction-executor/tests/TestEthTransactionExecutorFixture.cpp`

- [ ] **Step 1:** 扩展 `ExpectedResult` + loader 解析：

```cpp
struct ExpectedResult {
    // ... existing fields ...
    int64_t gasUsedExecutor = 0;
    int64_t gasUsedExecutorTolerance = 0;
};
// in loadFixture expected tree:
fixture.expected.gasUsedExecutor = expectedTree.get<int64_t>("gas_used_executor", 0);
fixture.expected.gasUsedExecutorTolerance =
    expectedTree.get<int64_t>("gas_used_executor_tolerance", 0);
```

- [ ] **Step 2:** 为 6 个候选 fixture 添加字段（先跑 executor 记录实测值再入库）：

| Fixture | 初始 `gas_used_executor` |
|---------|--------------------------|
| `prague_call_return_word.json` | 实测填入 |
| `imported/stExample_return42.json` | 实测填入 |
| `imported/stRevert_revertBasic.json` | 实测填入 |
| `imported/stRevert_revertDepth.json` | 实测填入 |
| `imported/stCreate_initCode.json` | 实测填入 |
| `imported/stCreate2_basic.json` | 实测填入 |

获取实测值方法：临时在测试中 `BOOST_TEST_MESSAGE("gasUsed=" << receipt->gasUsed())` 跑一遍。

- [ ] **Step 3:** 新增测试 case：

```cpp
BOOST_AUTO_TEST_CASE(gas_executor_phase2_subset) {
    static std::array<std::string_view, 6> kGasFixtures = {
        "prague_call_return_word.json",
        "imported/stExample_return42.json",
        "imported/stRevert_revertBasic.json",
        "imported/stRevert_revertDepth.json",
        "imported/stCreate_initCode.json",
        "imported/stCreate2_basic.json",
    };
    // ... run with AssertPhase::Phase2 ...
}
```

- [ ] **Step 4:** 运行 Phase 1 + Phase 2 全绿；在 spec §Phase 2 记录 gas 口径说明（executor 含 intrinsic/buyGas）

- [ ] **Step 5:** Commit

```bash
rtk git commit -m "test(eth): add gas_used_executor assertions for executor fixture Phase 2"
```

---

### Task 6: Done 验收

- [ ] **Step 1:** 确认 Done 清单
  - `ctest -R EthTransactionExecutorFixture` 全绿
  - Phase 1: 20 fixtures status/output/logs
  - Phase 2: ≥6 fixtures gas_used_executor
  - `ExecuteViaEthFixtureTest` 仍 20/20

- [ ] **Step 2:** 更新 `remaining-tasks.md`：T-09b ✅，T-17 部分前置完成

- [ ] **Step 3:** 最终 commit（若 Step 2 有文档更新）

```bash
rtk git commit -m "docs: mark T-09b eth executor fixture e2e complete"
```

---

## Spec Self-Review

| Spec 要求 | Task |
|-----------|------|
| StorageSeeder | Task 1 |
| TransactionBuilder | Task 2 |
| ExecutorFixtureAssert Phase 1 | Task 3 |
| 20 fixtures Phase 1 | Task 4 |
| gas_used_executor Phase 2 | Task 5 |
| standalone CMake + ctest | Task 4 |
| 复用 loader/fixtures | Task 4–5 |
| 策略 C 分阶段 | Task 4 + Task 5 |

无 TBD/占位符。

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-06-19-t09b-eth-executor-e2e.md`.

**两种执行方式：**

1. **Subagent-Driven（推荐）** — 每 Task 派发独立 subagent，Task 间 review
2. **Inline Execution** — 本会话按 Task 顺序实现

**选哪种？**

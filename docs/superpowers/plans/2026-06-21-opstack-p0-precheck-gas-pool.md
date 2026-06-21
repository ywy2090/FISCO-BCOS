# OPStack P0：EIP-4844 preCheck + Block Gas Pool 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 闭合 Wave 3 审计项 R3-4844-1/2/3 与 R3-POOL-1，使 Isthmus OP-Stack 普通 L2 tx 的 blob preCheck 与块级 gas pool 行为对齐 op-geth `state_transition.go:417-433` 与 `gaspool.go`。

**Architecture:** PR-1 在 `opStackPreCheck` 集中补齐 blob 三规则（方案 A）。PR-2 扩展 `BlockGasPool`（SubGas/ReturnGas/cumulative），在 `opStackExecuteViaHost` 普通 L2 路径 buyGas 前 SubGas、执行后 ReturnGas；`OpStackTransactionExecutorImpl` 持有块级 shared pool，由 `SchedulerSerialImpl::executeBlock` 块首/块尾 reset。

**Tech Stack:** C++17、Boost.Test、bcos-evm-op、transaction-executor、CMake/CTest

**Design spec:** `docs/superpowers/specs/2026-06-21-opstack-p0-precheck-gas-pool-design.md`

## Global Constraints

- 范围：Isthmus TE baseline；普通 L2 tx + deposit 回归；不含 P1（7702 CREATE、eth_call noBaseFee、Receipt BlobGasUsed）
- op-geth 锚点：`state_transition.go:417-433`（blob preCheck）；`gaspool.go:52-68`（ReturnGas）；`buyGas` 内 SubGas 先于余额检查
- `hasBlobTxIntent`：`web3TypedTxKind == 0x03 || !blobVersionedHashes.empty()`
- blob 畸形输入 → `protocol::TransactionStatus::Malformed`；fee cap 不足 → `InsufficientFunds`
- buyGas 余额不足 **不** ReturnGas（对齐 geth：SubGas 已发生）
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）

---

## 文件职责图

| 文件 | 职责 |
|------|------|
| `bcos-evm/opstack/Eip4844.h` | `hasBlobTxIntent`、`isValidVersionedHash` |
| `bcos-evm/opstack/OpStackPreCheck.cpp` | blob 三规则 + 统一 blob 分支 |
| `bcos-evm/opstack/OpStackExecuteViaHost.h/.cpp` | `gasPoolReturnGasHook`；SubGas/ReturnGas 编排 |
| `transaction-executor/.../OpStackTxInputBuilder.h` | `BlockGasPool::returnGas`、`remaining`、`cumulativeUsed` |
| `transaction-executor/.../OpStackTransactionExecutorImpl.h` | 块级 pool + `beginBlock`/`endBlock` |
| `transaction-scheduler/.../SchedulerSerialImpl.h` | 块首/块尾调用 executor lifecycle |
| `bcos-evm/test/opstack/OpStackPreCheck4844Test.cpp` | 4844 单元测试 |
| `bcos-evm/test/opstack/BlockGasPoolTest.cpp` | pool API + orchestration 测试 |
| `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp` | 两笔 tx 超 block gas 集成测试 |

**PR 切分：** PR-1 = Task 1–2；PR-2 = Task 3–8

---

## Task 1: EIP-4844 辅助头 + preCheck 三规则

**Files:**
- Create: `bcos-evm/opstack/Eip4844.h`
- Modify: `bcos-evm/opstack/OpStackPreCheck.cpp:77-87`

**Interfaces:**
- Produces:
  ```cpp
  namespace bcos::evm {
  inline bool hasBlobTxIntent(OpStackExecuteViaHostInput const& input) noexcept;
  inline bool isValidVersionedHash(bcos::h256 const& h) noexcept;
  }
  ```

- [ ] **Step 1: 写失败测试** — 见 Task 2（TDD：先跳至 Task 2 Step 1，再回来实现）

- [ ] **Step 2: 创建 `Eip4844.h`**

```cpp
#pragma once
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include <bcos-utilities/FixedBytes.h>

namespace bcos::evm
{
inline bool hasBlobTxIntent(OpStackExecuteViaHostInput const& input) noexcept
{
    return input.web3TypedTxKind == 0x03 || !input.blobVersionedHashes.empty();
}

inline bool isValidVersionedHash(bcos::h256 const& h) noexcept
{
    return h[0] == 0x01;  // kzg4844.BlobCommitmentVersionKZG
}
}  // namespace bcos::evm
```

- [ ] **Step 3: 修改 `OpStackPreCheck.cpp`**

在 `#include "OpStackPreCheck.h"` 后增加 `#include "bcos-evm/opstack/Eip4844.h"`。

将 `if (!input.skipTransactionChecks)` 内原 `if (!input.blobVersionedHashes.empty()) { ... }` 块替换为：

```cpp
        if (hasBlobTxIntent(input))
        {
            if (!input.revisionConfig.eip4844)
            {
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            }
            if (isCreateKind(input.message.kind))
            {
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            }
            if (input.blobVersionedHashes.empty())
            {
                return makePreCheckError(protocol::TransactionStatus::Malformed);
            }
            for (auto const& hash : input.blobVersionedHashes)
            {
                if (!isValidVersionedHash(hash))
                {
                    return makePreCheckError(protocol::TransactionStatus::Malformed);
                }
            }
            if (input.blobGasFeeCap < input.blockInfo.blobBaseFee)
            {
                return makePreCheckError(protocol::TransactionStatus::InsufficientFunds);
            }
        }
```

- [ ] **Step 4: 运行 Task 2 测试**

Run: `cd build && ctest -R OpStackPreCheck4844 -V`  
Expected: PASS

- [ ] **Step 5: 回归现有 blob 测试**

Run: `cd build && ctest -R 'BlobGasBalance|DepositTxPreCheck' -V`  
Expected: PASS

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/Eip4844.h bcos-evm/opstack/OpStackPreCheck.cpp
rtk git commit -m "$(cat <<'EOF'
fix(opstack): align EIP-4844 blob preCheck with op-geth

Reject blob CREATE, empty hashes on type-0x03 intent, and invalid
versioned hash prefix before buyGas.
EOF
)"
```

---

## Task 2: OpStackPreCheck4844Test

**Files:**
- Create: `bcos-evm/test/opstack/OpStackPreCheck4844Test.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`（在 `DepositTxPreCheckTest` 块后追加）

**Interfaces:**
- Consumes: `opStackPreCheck`, `hasBlobTxIntent` 行为（间接）

- [ ] **Step 1: 写失败测试文件**

```cpp
#define BOOST_TEST_MODULE OpStackPreCheck4844Test

#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpStackPreCheck.h"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

h256 makeVersionedHash(uint8_t versionByte)
{
    h256 hash{};
    hash[0] = versionByte;
    hash[31] = 0x42;
    return hash;
}

OpStackExecuteViaHostInput makeBlobInput(evmc_address sender)
{
    OpStackExecuteViaHostInput input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 100'000;
    input.nonce = 0;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.revisionConfig.eip4844 = true;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 100;
    input.blobGasFeeCap = 200;
    input.web3TypedTxKind = 0x03;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x01));
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(rejects_blob_create)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    state::State state(stateView);
    auto input = makeBlobInput(sender);
    input.message.kind = EVMC_CREATE;

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_type03_with_empty_hashes)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x02);
    state::State state(stateView);
    auto input = makeBlobInput(sender);
    input.blobVersionedHashes.clear();

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_versioned_hash_prefix)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x03);
    state::State state(stateView);
    auto input = makeBlobInput(sender);
    input.blobVersionedHashes = {makeVersionedHash(0x00)};

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_blob_when_eip4844_disabled)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x04);
    state::State state(stateView);
    auto input = makeBlobInput(sender);
    input.revisionConfig.eip4844 = false;

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_low_blob_fee_cap)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x05);
    state::State state(stateView);
    auto input = makeBlobInput(sender);
    input.blobGasFeeCap = 99;

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(accepts_valid_blob_precheck)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x06);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);
    auto input = makeBlobInput(sender);

    auto error = opStackPreCheck(input, state);
    BOOST_CHECK(!error.has_value());
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: 注册 CMake**

在 `bcos-evm/test/CMakeLists.txt` 的 `DepositTxPreCheckTest` 块之后追加：

```cmake
set(OPSTACK_PRECHECK_4844_TEST_BINARY_NAME OpStackPreCheck4844Test)

add_executable(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME}
    opstack/OpStackPreCheck4844Test.cpp
)

target_include_directories(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackPreCheck4844
    COMMAND ${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME}
)
```

- [ ] **Step 3: 构建并确认失败**

Run: `cmake --build build --target OpStackPreCheck4844Test && build/bcos-evm/test/OpStackPreCheck4844Test`  
Expected: FAIL（blob CREATE / empty hashes / invalid version 用例失败）

- [ ] **Step 4: 完成 Task 1 实现后重跑**

Expected: 6/6 PASS

- [ ] **Step 5: Commit**（可与 Task 1 合并为 PR-1 单次 commit，或分两次）

---

## Task 3: 扩展 BlockGasPool API

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h:28-54`

**Interfaces:**
- Produces:
  ```cpp
  class BlockGasPool {
  public:
      explicit BlockGasPool(int64_t gasLimit);
      bool tryConsume(uint64_t gas) noexcept;
      void returnGas(uint64_t gasRemaining, uint64_t gasUsed) noexcept;
      [[nodiscard]] int64_t remaining() const noexcept;
      [[nodiscard]] uint64_t cumulativeUsed() const noexcept;
  private:
      std::atomic<int64_t> m_remaining;
      std::atomic<uint64_t> m_cumulativeUsed{0};
  };
  ```

- [ ] **Step 1: 写失败测试** — 见 Task 4 Step 1

- [ ] **Step 2: 扩展 `BlockGasPool`**

在 `tryConsume` 成功分支后，`m_cumulativeUsed` 不在 tryConsume 更新（geth cumulative 在 ReturnGas 时加 gasUsed）。

实现 `returnGas`（对齐 `gaspool.go:52-68`）：

```cpp
void returnGas(uint64_t gasRemaining, uint64_t gasUsed) noexcept
{
    auto returned = static_cast<int64_t>(
        std::min<uint64_t>(gasRemaining, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
    auto prev = m_remaining.fetch_add(returned, std::memory_order_acq_rel);
    (void)prev;
    m_cumulativeUsed.fetch_add(gasUsed, std::memory_order_relaxed);
}

[[nodiscard]] int64_t remaining() const noexcept
{
    return m_remaining.load(std::memory_order_relaxed);
}

[[nodiscard]] uint64_t cumulativeUsed() const noexcept
{
    return m_cumulativeUsed.load(std::memory_order_relaxed);
}
```

- [ ] **Step 3: 运行 Task 4 单元测试**

Expected: PASS

- [ ] **Step 4: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h
rtk git commit -m "$(cat <<'EOF'
feat(opstack): add BlockGasPool returnGas and cumulativeUsed

Align block gas accounting with op-geth GasPool.ReturnGas semantics.
EOF
)"
```

---

## Task 4: BlockGasPool 单元 + orchestration hook 测试

**Files:**
- Create: `bcos-evm/test/opstack/BlockGasPoolTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1: 写测试**

```cpp
#define BOOST_TEST_MODULE BlockGasPoolTest

#include "../../../transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::opstack_tx::BlockGasPool;

BOOST_AUTO_TEST_CASE(try_consume_and_return_gas)
{
    BlockGasPool pool(1'000'000);
    BOOST_REQUIRE(pool.tryConsume(100'000));
    BOOST_CHECK_EQUAL(pool.remaining(), 900'000);
    pool.returnGas(80'000, 20'000);
    BOOST_CHECK_EQUAL(pool.remaining(), 980'000);
    BOOST_CHECK_EQUAL(pool.cumulativeUsed(), 20'000);
}

BOOST_AUTO_TEST_CASE(second_tx_fails_when_pool_exhausted)
{
    auto shared = std::make_shared<BlockGasPool>(200'000);
    BOOST_REQUIRE(shared->tryConsume(150'000));
    BOOST_CHECK(!shared->tryConsume(100'000));
}

BOOST_AUTO_TEST_CASE(opstack_execute_subgas_and_return_on_success)
{
    // 最小 smoke：普通 L2 路径 SubGas + ReturnGas（Task 5 完成后启用完整断言）
    state::test::InMemoryStateView stateView;
    evmc_address sender{};
    sender.bytes[19] = 0x21;
    evmc_address target{};
    target.bytes[19] = 0x22;
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000'000), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    auto pool = std::make_shared<BlockGasPool>(500'000);
    evmc::VM vm{evmc_create_evmone()};

    OpStackExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message.kind = EVMC_CALL;
    input.message.gas = 100'000;
    input.message.sender = sender;
    input.message.recipient = target;
    input.message.code_address = target;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;
    input.txProps.warmDestination = true;
    input.gasPoolSubGasHook = [pool](uint64_t gas) { return pool->tryConsume(gas); };
    input.gasPoolReturnGasHook = [pool](uint64_t remaining, uint64_t used) {
        pool->returnGas(remaining, used);
    };

    auto output = task::syncWait(opStackExecuteViaHost(input));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK(pool->cumulativeUsed() > 0);
    BOOST_CHECK(pool->remaining() < 500'000);
}
}  // namespace bcos::evm::test
```

> **Note:** 最后一个用例依赖 Task 5 添加 `gasPoolReturnGasHook`；Task 4 先写 pool 纯单元用例，orchestration 用例在 Task 5 后启用。

- [ ] **Step 2: CMake 注册**（模式同 Task 2）

- [ ] **Step 3: 构建运行**

Run: `ctest -R BlockGasPool -V`  
Expected: 前两个用例 PASS；orchestration 用例在 Task 5 前 SKIP 或 XFAIL

---

## Task 5: opStackExecuteViaHost SubGas / ReturnGas 编排

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.h:46-47`
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp:196-261`（普通 L2 路径）
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp:124-193`（deposit ReturnGas）

**Interfaces:**
- Produces:
  ```cpp
  struct OpStackExecuteViaHostInput {
      // existing...
      std::function<bool(uint64_t)> gasPoolSubGasHook;
      std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> gasPoolReturnGasHook;
  };
  ```

- [ ] **Step 1: 在 `OpStackExecuteViaHost.h` 增加 hook**

```cpp
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> gasPoolReturnGasHook;
```

- [ ] **Step 2: 普通 L2 — buyGas 前 SubGas**

在 `OpStackExecuteViaHost.cpp` 普通 L2 分支，`co_await input.opTxExecutor.buyGas(txData)` **之前**插入：

```cpp
    bool gasPoolReserved = false;
    auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, input.message.gas));
    if (input.gasPoolSubGasHook)
    {
        if (!input.gasPoolSubGasHook(gasLimitForPool))
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_OUT_OF_GAS;
            failResult.gas_left = 0;
            output.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
            co_return output;
        }
        gasPoolReserved = true;
    }
```

- [ ] **Step 3: 添加 RAII helper（匿名 namespace）**

```cpp
struct GasPoolReturnGuard
{
    std::function<void(uint64_t, uint64_t)> hook;
    uint64_t gasRemaining{0};
    uint64_t gasUsed{0};
    bool active{false};

    ~GasPoolReturnGuard()
    {
        if (active && hook)
        {
            hook(gasRemaining, gasUsed);
        }
    }
};
```

普通 L2 路径：buyGas 成功后 `guard.active = true`；settlement 后赋值 `gasRemaining`/`gasUsed`；**buyGas 失败时不 active**（对齐 geth）。

deposit 路径：preCheck 已 SubGas，执行结束后：

```cpp
    if (input.gasPoolReturnGasHook)
    {
        auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
        auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
        auto const gasRemaining = gasLimit > gasUsed ? gasLimit - gasUsed : 0;
        input.gasPoolReturnGasHook(gasRemaining, gasUsed);
    }
```

- [ ] **Step 4: 运行 BlockGasPool orchestration 测试 + DepositTxPreCheck 回归**

Run: `ctest -R 'BlockGasPool|DepositTxPreCheck|OpStackExecuteViaHost' -V`  
Expected: PASS

- [ ] **Step 5: Commit**

---

## Task 6: OpStackTransactionExecutorImpl 块级 pool

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h:71-137, 208-209`

**Interfaces:**
- Produces:
  ```cpp
  class OpStackTransactionExecutorImpl {
  public:
      void beginBlock(int64_t blockGasLimit) noexcept;
      void endBlock() noexcept;
  private:
      std::shared_ptr<opstack_tx::BlockGasPool> m_blockGasPool;
  };
  ```

- [ ] **Step 1: 添加成员与方法**

```cpp
    void beginBlock(int64_t blockGasLimit) noexcept
    {
        m_blockGasPool = std::make_shared<opstack_tx::BlockGasPool>(blockGasLimit);
    }

    void endBlock() noexcept { m_blockGasPool.reset(); }

private:
    std::shared_ptr<opstack_tx::BlockGasPool> m_blockGasPool;
```

- [ ] **Step 2: 修改 `ExecuteContext::Data` 构造**

删除 per-tx `make_shared<BlockGasPool>`，改为：

```cpp
                m_blockGasPool(executor.m_blockGasPool ?
                                   executor.m_blockGasPool :
                                   std::make_shared<opstack_tx::BlockGasPool>(
                                       static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()))))
```

- [ ] **Step 3: 接线 return hook**

在 `opStackExecuteViaHostTx()`：

```cpp
            input.gasPoolSubGasHook = [pool = m_data->m_blockGasPool](uint64_t gas) {
                return !pool || pool->tryConsume(gas);
            };
            input.gasPoolReturnGasHook = [pool = m_data->m_blockGasPool](
                                             uint64_t remaining, uint64_t used) {
                if (pool)
                {
                    pool->returnGas(remaining, used);
                }
            };
```

- [ ] **Step 4: Commit**

---

## Task 7: SchedulerSerialImpl beginBlock / endBlock

**Files:**
- Modify: `transaction-scheduler/bcos-transaction-scheduler/SchedulerSerialImpl.h:28-34, 117-120`

- [ ] **Step 1: 块首 beginBlock**

在 `executeBlock` 开头、`contexts.reserve(count)` 之后：

```cpp
        if constexpr (requires { executor.beginBlock(0); })
        {
            executor.beginBlock(static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit())));
        }
```

- [ ] **Step 2: 块尾 endBlock（RAII 或 finally）**

在 `co_return receipts;` 之前：

```cpp
        if constexpr (requires { executor.endBlock(); })
        {
            executor.endBlock();
        }
```

> 若 pipeline 可能抛异常，用 scope guard 确保 `endBlock` 必调。

- [ ] **Step 3: 检查 `SchedulerParallelImpl.h`**

若 OP 路径也走 parallel scheduler，同样加 `if constexpr` 块（读文件确认后镜像修改）。

- [ ] **Step 4: Commit**

---

## Task 8: TE 集成测试 — 两笔 tx 超 block gas

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`（追加 test case）

- [ ] **Step 1: 写集成测试**

```cpp
BOOST_AUTO_TEST_CASE(second_transaction_rejected_when_block_gas_exhausted)
{
    auto const blockGasLimit = int64_t{300'000};
    ledgerConfig().setGasLimit({blockGasLimit, ledgerConfig().gasLimit().second});

    executor().beginBlock(blockGasLimit);

    // tx1: gasLimit=200000, 应成功
    auto receipt1 = task::syncWait(executor().executeTransaction(
        storage(), blockHeader(), makeCallTx(/* gasLimit */ 200'000, /* to */ 0x12), 0, ledgerConfig(), false));
    BOOST_REQUIRE(receipt1);
    BOOST_CHECK(receipt1->status() == 0 || receipt1->status() == 1); // success or revert OK

    // tx2: gasLimit=200000, 块剩余不足 → OutOfGasLimit
    auto receipt2 = task::syncWait(executor().executeTransaction(
        storage(), blockHeader(), makeCallTx(200'000, 0x13), 1, ledgerConfig(), false));
    BOOST_REQUIRE(receipt2);
    BOOST_CHECK_NE(receipt2->status(), 0); // 或断言具体 OutOfGasLimit status

    executor().endBlock();
}
```

按 fixture 现有 helper（`makeCallTx` 等）调整参数；若 fixture 无 `beginBlock` 暴露，通过 `executor()` 直接调用。

- [ ] **Step 2: 运行 TE 测试**

Run: `ctest -R OpStackTransactionExecutorFixture -V`  
Expected: PASS

- [ ] **Step 3: Commit**

---

## Task 9: 文档与审计闭合

**Files:**
- Modify: `bcos-evm/capability-matrix.md`
- Modify: `bcos-evm/docs/audits/2026-06-21-l2-tx-rlp-to-receipt-comparison.md`
- Modify: `bcos-evm/docs/audits/2026-06-21-opstack-isthmus-reaudit-wave3.md`

- [ ] **Step 1:** 矩阵 EIP-4844 行添加 `OpStackPreCheck4844Test` ref
- [ ] **Step 2:** R3-4844-1/2/3、R3-POOL-1 标 **CLOSED**，附 commit/测试名
- [ ] **Step 3:** Wave 3 主判定更新（P0 闭合后评估是否升 ✅）
- [ ] **Step 4: Commit**

---

## 全量验证

- [ ] **Step 1: 构建**

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
```

- [ ] **Step 2: OP Stack CTest 套件**

```bash
cd build && ctest -R 'OpStack|BlobGas|DepositTx|BlockGas' -V
```

Expected: 全部 PASS

- [ ] **Step 3: TE fixture**

```bash
cd build && ctest -R OpStackTransactionExecutorFixture -V
```

---

## Plan Self-Review

| Spec 要求 | 对应 Task |
|-----------|-----------|
| 4844 CREATE 拒绝 | Task 1 |
| 4844 空 hashes | Task 1 |
| 4844 version 0x01 | Task 1 |
| hasBlobTxIntent 0x03 \|\| hashes | Task 1 |
| buyGas SubGas | Task 5 |
| ReturnGas | Task 5 |
| buyGas 失败不 ReturnGas | Task 5 |
| deposit ReturnGas | Task 5 |
| 块级 shared pool | Task 6–7 |
| 多 tx 超 block gas 测试 | Task 8 |
| 矩阵/审计更新 | Task 9 |

无 TBD；类型名 `gasPoolReturnGasHook` 全 plan 一致。

---

## 执行选项

Plan 已保存至 `docs/superpowers/plans/2026-06-21-opstack-p0-precheck-gas-pool.md`。

**1. Subagent-Driven（推荐）** — 每 Task 派生子 agent，Task 间 review  
**2. Inline Execution** — 本会话按 Task 顺序直接实现，checkpoint Review

你想用哪种方式开始实现？

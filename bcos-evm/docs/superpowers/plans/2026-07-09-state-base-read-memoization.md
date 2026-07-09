# M-N：`State` 账户字段读记忆化（读穿缓存 + 负缓存）实施计划 v2

> ## ❌ 已取消（2026-07-09，用户裁定，最终状态）
>
> 用户原则「**要尽量复用 evmone 的代码**」下的三选一裁定：**A——转入替换路径**。
>
> 理由：本 plan 本质是在 `bcos-evm/eth/state/State`（按 rev.7 D2 属**待替换代码**）里手写一遍
> evmone `State::find()` 原生就有的读穿缓存——代码无法直接搬（Account/journal 结构不同），
> 只能移植设计；而替换路径上这层缓存**就是 evmone 的代码本身**，免费获得。
>
> **工作去向**：并入 **M3.5 Phase 2**（真实账本 ↔ `evmone::state` 桥接原型 + 三项开销实测）——
> evmone 读穿缓存免费；唯一缺口（负查询不缓存，spike 实测占账本读 27.9%）以 **~5 行加在
> bcos-evm-ref 侧适配器**解决，不碰 evmone。本 plan 的调查产出随之转移：
> 23 调用点普查、`LedgerStateView` 逐方法读代价表、`CountingStateView`/`weightedReads()` 测量设计、
> ETH/OP 路径不变量验证口径（"谁写 view 底下的存储"）、附录 A 的 FISCO 反例记录。
>
> v1 → HOLD（FISCO 反例）→ v2（裁定豁免 + 四路审查吸收）→ **v3 = 本取消记录**。全文保留作设计档案。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> ## 版本与授权状态
>
> - **v1（2026-07-09 上午）**：四路审查后 **ON HOLD**——核心不变量在 FISCO 路径上被证伪
>   （`persistContractCreateNonce` 执行中途直写 view 底下的存储，反例链条见文末附录 A）。
> - **v2（2026-07-09，本版）**：**HOLD 解除**。依据是用户原则性裁定：
>   **「后续版本不支持 FISCO 路径，可以不考虑」**（与此前"忽略 FISCO 折扣"的裁定一致）。
>   ETH/OP 路径经实地验证无任何执行中途写 base 的路径（FISCO 侧的 `buyGas`/`refundGas` 也位于
>   `State` 生存期外；唯一的中途写就是附录 A 的 nonce 持久化，FISCO 专属）。**本 plan 不为 FISCO
>   实现任何禁用开关**——按裁定直接不考虑；在 FISCO 代码物理移除之前，附录 A 的分歧场景在该路径
>   上理论存在，已知悉并接受。
> - v2 已吸收四路审查的全部必改项：测试框架改 Boost.Test、命名空间与 CMake 按仓库约定、
>   `mutable_account()` 纳入 memo（新 Task 3）、覆盖面如实声明（14/23 调用点）、加权读数与墙钟
>   记录、`copy_code` 遮蔽改名、标题更名（storage 不在范围内）。

**Goal:** 消除 `bcos::evm::state::State` 对 base `StateView` 的重复**账户字段**读——同一笔交易内对同一账户的重复冷读（含负查询）、写路径物化时绕过缓存的整账户重载、以及 `build_diff()` 在结算期对每个 overlay 条目的整账户重载。

**Architecture:** 在 `State` 内加一层**只读记忆化**（`m_baseCache`）。核心不变量（在 ETH/OP 路径上成立，Task 1 验证并固化为文档）：**base view 在单个 `State` 的生存期内不可变，且 overlay（`m_accounts`）在任何查找中优先** ⇒ memo 无需失效、不入 journal、不参与 rollback。逐字段记忆，**永不加宽任何一次读**。

**Tech Stack:** C++20 · Boost.Test（`bcos-evm/test` 的既有框架——**不是 GTest**）· `bcos-evm/test/helpers/SparseStorageStateView.h`（生产 `LedgerStateView` 契约的镜像夹具，命名空间 `bcos::evm::state::test`）

**授权依据：** spec rev.7 决策 D3；FISCO 路径按用户 2026-07-09 裁定不予考虑。

## 问题的实测依据（立项论据，非本 PR 的 KPI 预测）

- M3.5 Phase 1（evmone 形状、EEST v5.4.0、53,131 tx）：负查询重复占全部 `get_account` 调用 70%、占全部账本读 27.9%。**该数字测自 evmone 形状，不可作为本 plan 的定量预测**；但"bcos-evm 浪费面严格大于它"有不等式证明（bcos-evm 读路径零缓存，evmone 至少缓存命中）。
- `bcos-evm` 的 `State::find()`（`State.cpp:43`）**连命中都不缓存**；`mutable_account()`（`:341`）物化时**再独立裸读一次**；`build_diff()`（`:653`）对每个 overlay 条目**第三次**整账户重载。
- `LedgerStateView` 读代价（每 lambda 以 `syncWait(exists)` 起手、miss 提前返回）：`get_account` 命中 5 / miss 1；`get_code_hash` 命中 3（空 code 账户为 2）；`get_balance`/`get_nonce`/`get_code` 命中 2 / miss 1；`account_exists` 恒 1。
- **时间成本未实测**（executor 存储是 storage2 View 分层 + 后端缓存）——因此本 plan 只承诺**读次数**下降，货币化留给 M3.5 Phase 2；Task 7 记录墙钟时间作为观测项。

## Global Constraints

- 仓库根（worktree）：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`。
- **不变量（ETH/OP 路径）**：base view 在 `State` 生存期内不可变；Task 1 以"**谁写 view 底下的存储**"为口径验证（不是"谁碰 `State`"——v1 就栽在这个口径上），范围含 `transaction-executor/` 与 `bcos-evm/` 全部，FISCO 专属写入点（附录 A 三处）按裁定豁免。
- **memo 不是状态**：`mutable` 成员，不入 journal、不参与 rollback/checkpoint；overlay 永远优先。
- **永不加宽读**：逐字段记忆，各自沿用原 base 方法；常驻断言守护。
- **零行为变更**：纯性能改动；任何测试期望值修改视为缺陷。
- **测试框架 = Boost.Test**：`BOOST_TEST_MODULE` included 变体 + `BOOST_AUTO_TEST_CASE`；每测试文件一个独立 `add_executable`+`add_test`（仓库约定，见 `test/cmake/StateTests.cmake` 的 `SparseStorageOverlayTest` 先例）。
- 新代码命名空间与夹具一致：`bcos::evm::state::test`。
- pre-commit clang-format 钩子：被拒即已就地格式化，重新 add 再提交。

## 覆盖面（如实声明）

`State.cpp` 共 23 处 `m_baseStateView->` 调用。本 plan 转换 **14 处**：

| 分类 | 调用点 |
|---|---|
| ✅ 转换 14 处 | `find`(49)、`mutable_account`(341)、`account_exists`(136/162)、`get_balance`(63)、`get_nonce`(72)、`get_code_hash`(138/152)、`get_code` ×5(84/86/97/99/113)、`build_diff`(653) |
| ❌ 有意保留裸读 6 处（低频/语义特殊） | `hasNonEmptyStorage`(180)、`clear_storage`(468)、`get_transient_storage`(513)、`has_self_destructed`(720)、`finalize_self_destructs`(751)、`isPreexistingAccount`(826)——每 tx 至多一两次的路径，收益小；留待有数据再议 |
| ❌ storage 非目标 3 处 | `get_storage`(212/214) 与 `storageOriginalAtTxStart`(235)——与 `storageReset`/`m_storageOriginal` 语义交织（先例 plan `2026-07-08-overlay-storage-read-through.md` 修过仅生产可见的 bug），独立成后续 plan。**storage 读占 spike 总读量 ~31%（create 类负载 57.6%）——本 plan 的收益上限不含这一块** |

## File Structure

```
bcos-evm/eth/state/State.hpp                 # BaseCacheEntry + m_baseCache + 6 访问器声明
bcos-evm/eth/state/State.cpp                 # 访问器实现；14 处调用点改走访问器
bcos-evm/test/helpers/CountingStateView.h    # 新建：计数装饰器（namespace bcos::evm::state::test）
bcos-evm/test/state/StateBaseReadCacheTest.cpp  # 新建：Boost.Test，独立可执行目标
bcos-evm/test/cmake/StateTests.cmake         # 追加独立 add_executable/add_test
bcos-evm/docs/adr/0NN-state-base-read-memoization.md  # Task 7
```

---

### Task 1: 不变量验证（正确口径）+ 计数夹具 + 基线红棒

**Files:**
- Create: `bcos-evm/test/helpers/CountingStateView.h`
- Create: `bcos-evm/test/state/StateBaseReadCacheTest.cpp`
- Modify: `bcos-evm/test/cmake/StateTests.cmake`

**Interfaces:**
- Produces: `bcos::evm::state::test::CountingStateView`（包装任意 `StateView`，逐方法计数 + 按 `LedgerStateView` 代价表加权的 `weightedReads()`）。

- [ ] **Step 1: 不变量验证——口径是"谁写 view 底下的存储"**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
# 找出所有对 rollbackable/ledger 存储的写入点（不是对 State 的使用点）
grep -rn "m_rollbackableStorage" transaction-executor/bcos-transaction-executor --include='*.h' | head -30
grep -rn "EVMAccount" bcos-evm --include='*.h' --include='*.cpp' | grep -v "LedgerStateView\|StateDiffApplier\|test" | head -20
```
Expected：写入点分三类——① `applyStateDiff`（`State` 析构后，安全）；② FISCO 专属（附录 A 三处：`buyGas` 预扣 / `persistContractCreateNonce` 中途写 / `refundGas` 回滚）——**按用户裁定豁免，逐条列进报告并标注 "FISCO-only, out of scope by decree"**；③ 其他任何 ETH/OP 路径上、可能落在 `State` 生存期内的写入——**发现即 BLOCKED**。
三类清单（文件:行号 + 相序论证）写进报告；这份清单同时是 Task 7 ADR 的素材。

- [ ] **Step 2: 写 `bcos-evm/test/helpers/CountingStateView.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Counting decorator over any StateView, for base-read regression tests.
 * @file CountingStateView.h
 */

#pragma once

#include "bcos-evm/eth/state/StateView.hpp"

namespace bcos::evm::state::test
{
/// Wraps a StateView and counts every base read. Used to assert that State's
/// memoization removes repeated cold reads (plan M-N). Never used in production.
class CountingStateView : public StateView
{
public:
    struct Counts
    {
        size_t account{0};
        size_t exists{0};
        size_t balance{0};
        size_t nonce{0};
        size_t code{0};
        size_t codeHash{0};
        size_t storage{0};

        [[nodiscard]] size_t total() const
        {
            return account + exists + balance + nonce + code + codeHash + storage;
        }

        /// Ledger-read equivalents, weighted by LedgerStateView's per-method cost
        /// (hit-side weights; see plan M-N cost table). Raw total() undercounts:
        /// a get_account is worth 5 ledger reads while account_exists is worth 1.
        [[nodiscard]] size_t weightedReads() const
        {
            return account * 5 + exists * 1 + balance * 2 + nonce * 2 + code * 2 +
                   codeHash * 3 + storage * 2;
        }
    };

    explicit CountingStateView(StateView const& inner) noexcept : m_inner{inner} {}

    std::optional<Account> get_account(const evmc_address& address) const override
    {
        ++m_counts.account;
        return m_inner.get_account(address);
    }
    bool account_exists(const evmc_address& address) const override
    {
        ++m_counts.exists;
        return m_inner.account_exists(address);
    }
    bcos::u256 get_balance(const evmc_address& address) const override
    {
        ++m_counts.balance;
        return m_inner.get_balance(address);
    }
    uint64_t get_nonce(const evmc_address& address) const override
    {
        ++m_counts.nonce;
        return m_inner.get_nonce(address);
    }
    bcos::bytes get_code(const evmc_address& address) const override
    {
        ++m_counts.code;
        return m_inner.get_code(address);
    }
    evmc_bytes32 get_code_hash(const evmc_address& address) const override
    {
        ++m_counts.codeHash;
        return m_inner.get_code_hash(address);
    }
    evmc_bytes32 get_storage(const evmc_address& address, const evmc_bytes32& key) const override
    {
        ++m_counts.storage;
        return m_inner.get_storage(address, key);
    }

    [[nodiscard]] Counts const& counts() const noexcept { return m_counts; }
    void reset() noexcept { m_counts = {}; }

private:
    StateView const& m_inner;
    mutable Counts m_counts;
};
}  // namespace bcos::evm::state::test
```

- [ ] **Step 3: 写基线测试（Boost.Test；BEFORE 快照）**

`bcos-evm/test/state/StateBaseReadCacheTest.cpp`：

```cpp
#define BOOST_TEST_MODULE StateBaseReadCacheTest
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/CountingStateView.h"
#include "helpers/SparseStorageStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
{
namespace
{
constexpr evmc_address kAddr{{0x01}};
constexpr evmc_address kAbsent{{0x02}};

Account makeAccount()
{
    Account a;
    a.balance = bcos::u256{100};
    a.nonce = 7;
    a.code = bcos::bytes{0x60, 0x01};
    return a;
}
}  // namespace

// 负缓存：不存在账户被重复查询，base 读必须只发生一次
BOOST_AUTO_TEST_CASE(AbsentAccountProbedOnce)
{
    SparseStorageStateView base;
    CountingStateView counting{base};
    State st{counting};

    for (int i = 0; i < 5; ++i)
    {
        BOOST_CHECK(!st.get_account(kAbsent).has_value());
    }
    BOOST_CHECK_EQUAL(counting.counts().account, 1U);
}

// 同字段重复读：base 读必须只发生一次
BOOST_AUTO_TEST_CASE(BalanceReadOnce)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    State st{counting};

    for (int i = 0; i < 5; ++i)
    {
        BOOST_CHECK_EQUAL(st.get_balance(kAddr), bcos::u256{100});
    }
    BOOST_CHECK_EQUAL(counting.counts().balance, 1U);
}

// 永不加宽读：读 balance 不得触发 get_account（常驻守护）
BOOST_AUTO_TEST_CASE(BalanceReadDoesNotWidenToFullAccountLoad)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    State st{counting};

    (void)st.get_balance(kAddr);
    BOOST_CHECK_EQUAL(counting.counts().account, 0U);
}
}  // namespace bcos::evm::state::test
```

- [ ] **Step 4: 注册独立目标（照 `SparseStorageOverlayTest` 先例）**

`bcos-evm/test/cmake/StateTests.cmake` 追加：

```cmake
add_executable(StateBaseReadCacheTest state/StateBaseReadCacheTest.cpp)
target_include_directories(StateBaseReadCacheTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(StateBaseReadCacheTest PRIVATE bcos-evm-eth)
add_test(NAME StateBaseReadCache COMMAND StateBaseReadCacheTest)
```

- [ ] **Step 5: 构建并观察基线**

Expected: `AbsentAccountProbedOnce`（计数 5）与 `BalanceReadOnce`（计数 5）**红**；`BalanceReadDoesNotWidenToFullAccountLoad` **绿**。BEFORE 数字入报告。

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/test/helpers/CountingStateView.h bcos-evm/test/state/StateBaseReadCacheTest.cpp bcos-evm/test/cmake/StateTests.cmake
git commit -m "test(bcos-evm): CountingStateView fixture + failing baseline for State base-read memoization"
```

---

### Task 2: 负缓存 + `account_exists` 记忆化

**Files:** Modify `bcos-evm/eth/state/State.hpp`、`State.cpp`

**Interfaces（后续 Task 复用，签名勿改）:**
```cpp
std::optional<Account> baseAccount(const evmc_address& address) const;  // 含负缓存 + 交叉填充
bool                   baseExists(const evmc_address& address) const;
```

- [ ] **Step 1: `State.hpp` 私有区新增**（`m_baseStateView` 声明后；`<optional>`/`<unordered_map>` 已在 include 链中，若缺则补）

```cpp
    /// Memoized base-view reads. NOT state: never journaled, never rolled back.
    /// Safe without invalidation because the base view is immutable for this State's
    /// lifetime on the ETH/OP paths (verified in plan M-N Task 1; FISCO-path mid-tx
    /// base writes are out of scope by user decree, 2026-07-09) and the overlay
    /// `m_accounts` always wins on lookup.
    struct BaseCacheEntry
    {
        std::optional<bool> exists;
        std::optional<bcos::u256> balance;
        std::optional<uint64_t> nonce;
        std::optional<bcos::bytes> code;
        std::optional<evmc_bytes32> codeHash;
        /// Outer optional: memoized? Inner: present in base?
        std::optional<std::optional<Account>> account;
    };
    mutable std::unordered_map<evmc_address, BaseCacheEntry, AddressHash, AddressEqual> m_baseCache;

    std::optional<Account> baseAccount(const evmc_address& address) const;
    bool baseExists(const evmc_address& address) const;
```

- [ ] **Step 2: `State.cpp` 实现**（置于 `State::find` 之前）

```cpp
std::optional<Account> State::baseAccount(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.account.has_value())
    {
        entry.account = m_baseStateView->get_account(address);
        // 交叉填充：ledger 视图的 get_account 已把这些字段读回来，别再单独读。
        // codeHash 有意不交叉填充——StateView::get_code_hash 有归一化语义
        // （空 code -> emptyCodeHash，零 hash -> keccak256(code)），与
        // Account::codeHash 原始值不等价。
        if (entry.account->has_value())
        {
            auto const& loaded = **entry.account;
            entry.exists = true;
            entry.balance = loaded.balance;
            entry.nonce = loaded.nonce;
            entry.code = loaded.code;
        }
        else
        {
            entry.exists = false;
        }
    }
    return *entry.account;
}

bool State::baseExists(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.exists.has_value())
    {
        entry.exists = m_baseStateView->account_exists(address);
    }
    return *entry.exists;
}
```

- [ ] **Step 3: 替换 `find()`（:49）与 2 处 `account_exists`（:136/:162）为访问器调用**

- [ ] **Step 4: 跑测试**：`AbsentAccountProbedOnce` 转绿；`BalanceReadOnce` 仍红（Task 4 处理）。

- [ ] **Step 5: 全量回归**（`bcos-evm` 全套单测）：全绿、零期望值改动。

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp
git commit -m "perf(bcos-evm): memoize base get_account (incl. negative lookups) and account_exists"
```

---

### Task 3: `mutable_account()` 接入 memo（v2 新增——v1 最大的遗漏）

**背景**：`mutable_account()`（`State.cpp:334-344`）物化 overlay 账户时**自己裸调** `m_baseStateView->get_account()`，不经过 `find()`。写路径的每次首次物化都是一次不受 memo 保护的整账户读；v1 声称的"`mutable_account` 之前必先 `find()`"**是错的**——真实预热路径是 `journal_account_once → find()`，且依赖 checkpoint 非空。接入 memo 后：(a) 写路径的重复读消失；(b) Task 6 的 `build_diff` 命中率从"经验大概率"变成**结构必然**。

- [ ] **Step 1: 追加失败测试**

```cpp
// 写路径物化不得绕过 memo：先读后写同一地址，base get_account 只允许一次
BOOST_AUTO_TEST_CASE(MutableAccountReusesMemo)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    State st{counting};

    (void)st.get_account(kAddr);            // find -> baseAccount (memo 填充)
    st.add_balance(kAddr, bcos::u256{1});   // journal_account_once + mutable_account
    BOOST_CHECK_EQUAL(counting.counts().account, 1U);
}
```

- [ ] **Step 2: 跑，确认红**（当前 `mutable_account` 的裸读使计数为 2）。

- [ ] **Step 3: 改 `mutable_account()`（:341）**：`auto account = m_baseStateView->get_account(address);` → `auto account = baseAccount(address);`（其余不动）。

- [ ] **Step 4: 跑测试 + 全量回归。**

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.cpp bcos-evm/test/state/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): route mutable_account materialization through the base memo"
```

---

### Task 4: `balance` / `nonce` / `codeHash` 窄读记忆化

**Files:** Modify `State.hpp`、`State.cpp`、`StateBaseReadCacheTest.cpp`

- [ ] **Step 1: 追加失败测试**（Boost.Test 风格，同 Task 1）：
  - `NonceReadOnce`：5 次 `get_nonce`，断言 `counts().nonce == 1U`；
  - `CodeHashReadOnce`：5 次 `get_code_hash`，断言 `counts().codeHash == 1U` 且 `counts().exists <= 1U`；
  - `FullAccountLoadCrossFillsNarrowFields`：一次 `get_account` 后调 `get_balance`/`get_nonce`，断言 `balance == 0U && nonce == 0U`。
- [ ] **Step 2: 确认三红。**
- [ ] **Step 3: 实现 `baseBalance()`/`baseNonce()`/`baseCodeHash()`**（模式同 `baseExists`；`baseCodeHash` 记忆 base 的**归一化**结果，与交叉填充无关）。
- [ ] **Step 4: 替换 4 处调用点（:63/:72/:138/:152）；跑测试 + 全量回归**（`BalanceReadOnce` 此时转绿）。
- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp bcos-evm/test/state/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): memoize base balance/nonce/codeHash narrow reads"
```

---

### Task 5: `code` 记忆化（修掉"拉整段 code 只为取 size"）

- [ ] **Step 1: 追加失败测试** `CodeReadOnceAcrossSizeAndCopy`：

```cpp
BOOST_AUTO_TEST_CASE(CodeReadOnceAcrossSizeAndCopy)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    State st{counting};

    BOOST_CHECK_EQUAL(st.get_code_size(kAddr), 2U);
    BOOST_CHECK_EQUAL(st.get_code_size(kAddr), 2U);
    uint8_t buf[2]{};
    BOOST_CHECK_EQUAL(st.copy_code(kAddr, 0, buf, sizeof(buf)), 2U);
    BOOST_CHECK(st.get_code(kAddr) == (bcos::bytes{0x60, 0x01}));

    BOOST_CHECK_EQUAL(counting.counts().code, 1U);
}
```

- [ ] **Step 2: 确认红**（当前计数 4）。
- [ ] **Step 3: 实现 `bcos::bytes const& baseCode(const evmc_address&) const`**（memo 持有；`unordered_map` 元素引用在 rehash 下稳定——标准保证，仅迭代器失效）并替换 5 处（:84/:86/:97/:99/:113）。
  **⚠️ 遮蔽陷阱**：`copy_code()` 内已有局部变量 `bcos::bytes baseCode;`（`State.cpp:106`）——**必须改名**（如 `baseCodeBuf`），否则 `baseCode(address)` 会被解析为"调用一个 `bcos::bytes` 对象"而编译失败。`get_code()` 对外仍按值返回拷贝（契约不变）；`get_code_size()`/`copy_code()` 直接用引用，不再拷贝。
- [ ] **Step 4: 跑测试 + 全量回归**（`copy_code` 边界：offset ≥ size、buffer_size==0 仍绿）。
- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp bcos-evm/test/state/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): memoize base code; stop pulling full code just to take its size"
```

---

### Task 6: `build_diff()` 用 memo

- [ ] **Step 1: 追加失败测试**

```cpp
BOOST_AUTO_TEST_CASE(BuildDiffReusesMemoizedBaseAccount)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    State st{counting};

    st.add_balance(kAddr, bcos::u256{1});
    counting.reset();  // 只统计 build_diff 阶段

    auto const diff = st.build_diff();
    BOOST_CHECK_EQUAL(counting.counts().total(), 0U);
    BOOST_CHECK(!diff.accounts.empty());
}
```

- [ ] **Step 2: 确认红。**
- [ ] **Step 3: 改 `build_diff()`（:653）**：局部变量 `baseAccount` **改名 `baseAcc`**（4 处引用 :653/:659/:666/:693 同步改——漏改会编译失败，属自纠错），赋值改为 `auto const baseAcc = baseAccount(address);`。经 Task 3，此处 memo 命中率为**结构必然 100%**（任何进入 `m_accounts` 的地址都经过 `baseAccount()`）。
- [ ] **Step 4: 跑测试 + 全量回归**（重点 `StateBuildDiffTest`、`TopLevelInsufficientBalanceStateDiffTest`、`EthFeeSettlementStateTest`）。
- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.cpp bcos-evm/test/state/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): build_diff() reads the base memo instead of reloading each account"
```

---

### Task 7: 端到端测量（加权口径）+ 回归硬 gate + ADR

- [ ] **Step 1: 端到端对比**：在一个走完整交易管线的既有用例外套 `CountingStateView`，记录 BEFORE/AFTER 的 `counts()` 明细、**`weightedReads()`**（ADR 主口径——裸 `total()` 把值 5 读的 `get_account` 和值 1 读的 `exists` 等权，系统性低估降幅）、以及 `bcos-evm` 单测套件**墙钟时间**（观测项不设阈值——内存 fixture 下 memo 对只读一次的地址是纯哈希开销，此项兜底观测）。
- [ ] **Step 2: 回归硬 gate**（三项全绿，输出尾部入报告）：① `bcos-evm` 单测全套；② EEST state 全量；③ EEST blockchain 全量——`EthEestBlockchainFull` 的 405 个 pre-Cancun 失败**不增不减**（减少同样可疑：纯性能改动不该改变行为）。
- [ ] **Step 3: 写 ADR**（编号取 `docs/adr/` 现最大 +1），必含：
  - 不变量及其验证口径（"谁写 view 底下的存储"）；FISCO 豁免的用户裁定记录与附录 A 场景；
  - **`State` 非线程安全前提**（每 tx 一个实例、单执行流；此前提是继承的、从未文档化——防未来 DAG/预取改动踩雷）；
  - "永不加宽读"原则、`codeHash` 不交叉填充的原因、`journal_account_once` 零 checkpoint 早退（Task 3 后已无关紧要，记录备查）；
  - code 在 memo 与 overlay 双份拷贝的内存成本（24KB 合约 × 触碰地址数量级，生存期一个 tx）；
  - 覆盖面 14/23、storage（~31% 读量）留存在外；27.9% 是立项论据不是本 PR 实测。
- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/state/StateBaseReadCacheTest.cpp bcos-evm/docs/adr/0NN-state-base-read-memoization.md
git commit -m "docs(bcos-evm): ADR + weighted read-count evidence for State base-read memoization"
```

---

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| ETH/OP 路径存在未发现的中途写 base | Task 1 以正确口径（写存储，非碰 State）全仓验证；发现即 BLOCKED |
| FISCO 路径在代码移除前被运行 | 附录 A 场景理论存在；**按用户裁定不考虑**，ADR 记录在案 |
| `build_diff` base 值与执行期不一致 | 同一 memo ⇒ 构造上一致；Task 3 使命中结构必然 |
| 加宽读 | 逐字段 memo + 常驻守护断言 |
| code 双份内存 | 已入风险表与 ADR；量级 = 触碰合约的 code 总量，生存期一个 tx |
| 内存 fixture 下纯哈希开销 | Task 7 墙钟观测项兜底 |
| 测量口径失真 | `weightedReads()` 加权主口径 |

## 附录 A：FISCO 路径反例（v1 的 HOLD 原因，按用户裁定豁免）

链条：CREATE 到地址 A → 碰撞检查读 A（`eth/kernel/execution/CreateDeployment.h:79`）⇒ memo 缓存"A 不存在" → `FiscoEvmHostHooks::bumpContractCreateNonce`（`bcos/FiscoEvmHostHooks.cpp:133`）经 `persistContractCreateNonce`（`transaction-executor/.../TransactionExecutorImpl.h:261`）**执行中途**把 A 的 nonce 直写进 `LedgerStateView` 底下的存储 → 内层 revert 撤掉 A 的 overlay 条目 → 同 tx 再读 A：现行代码见 nonce，memo 版见过期"不存在"。另两处 FISCO 写入（`buyGas` 预扣、`refundGas` 回滚）经时序核实位于 `State` 生存期外（`TransactionExecutorImpl.h:166/169/183`），本就安全。**用户裁定（2026-07-09，两次强调）：「后续版本不支持 FISCO 路径，可以不考虑 FISCO 的问题；后续会删除 FISCO 相关代码」——此场景不予考虑、不实现禁用开关；FISCO 代码删除后本附录自动失效。**

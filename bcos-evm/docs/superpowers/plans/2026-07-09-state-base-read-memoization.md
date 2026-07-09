# M-N：`State` base-view 读记忆化（读穿缓存 + 负缓存）实施计划

> ## 🔴 状态：ON HOLD——核心不变量被审查证伪（2026-07-09，四路审查后）
>
> **本 plan 的不变量（"base view 在 `State` 生存期内不可变"）在 FISCO 路径上不成立**，反例链条已实证闭合：
> 1. CREATE 到地址 A → 碰撞检查先读 A（`eth/kernel/execution/CreateDeployment.h:79`）→ memo 将缓存"A 不存在"；
> 2. `FiscoEvmHostHooks::bumpContractCreateNonce`（`bcos/FiscoEvmHostHooks.cpp:133`）**执行中途**经
>    `persistContractCreateNonce`（`transaction-executor/.../TransactionExecutorImpl.h:261`）把 A 的 nonce
>    **直写进 `LedgerStateView` 底下的 `m_rollbackableStorage`**；
> 3. 内层 revert → journal 撤掉 A 的 overlay 条目；
> 4. 同 tx 再读 A：现行代码重读 base（看到 nonce），memo 版返回过期的"不存在"——**FISCO 共识路径行为分歧**。
>
> （已核实安全的两处：FISCO `buyGas` 在 `applyFiscoMessageTx()` 之前、`refundGas` 在其之后——均在 `State`
> 生存期外，`TransactionExecutorImpl.h:166/169/183` 时序为证。ETH/OP 路径未发现中途写 base。）
>
> **原 Task 1 Step 1 的审计范围（只 grep `eth/kernel`+`eth/apply`+`applyStateDiff`）看不见上述反例**——
> 正确范围必须含 `transaction-executor/` 与 `bcos-evm/bcos/`，且要查的是"谁写 view 底下的存储"，
> 不是"谁碰 State"。
>
> **解除 HOLD 的条件（任选其一 + 补充项）**：
> - (a) memo 范围收窄为 ETH/OP 路径专用（FISCO 经构造参数禁用），或
> - (b) 给 `persistContractCreateNonce` 加 memo 失效钩子（耦合面大，不推荐），或
> - (c) 证明该分歧点不可达（须给出可复核的论证，而非 grep 缺席）。
> - 补充项（无论选哪条）：先做 **M3.5 Phase 2** 给"每省一次读"定价——若单次重复读只是微秒级内存查找
>   （executor 存储是 storage2 View 分层 + 后端缓存，"协程账本往返"的时间成本从未实测），本 plan 应降级为
>   仅 Task 2 的负缓存或干脆不做。
> - 其余审查发现（须在解除 HOLD 后的修订版吸收）：`mutable_account()` 裸读须纳入（Task 5 的"必然命中"
>   因果链原文是错的，真实机制是 `journal_account_once→find()` 且依赖 checkpoint 非空）；覆盖面如实改为
>   13/23 调用点；code 在 memo 与 overlay 双份拷贝的内存成本入风险表；Task 6 用代价表加权计读数 + 记录
>   墙钟时间；标题改"账户字段读记忆化"并写明 storage（占总读 ~31%）留存在外；ADR 记 `State` 单线程前提。


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 `bcos::evm::state::State` 对 base `StateView` 的重复读——同一笔交易内对同一账户/字段的重复冷读，以及 `build_diff()` 对每个 overlay 条目的整账户重载。在 `LedgerStateView` 背书下，每一次 base 读都是一次 `task::syncWait` 协程账本往返。

**Architecture:** 在 `State` 内加一层**只读记忆化**（`m_baseCache`）。核心不变量：**base view 在单个 `State` 的生存期内不可变，且 overlay（`m_accounts`）在任何查找中优先**——因此 memo 无需失效、无需 journal、不参与 rollback。逐字段记忆（不是整账户），保证**永不加宽任何一次读**。

**Tech Stack:** C++20 · `bcos-evm/eth/state/{State.hpp,State.cpp,StateView.hpp,Account.hpp}` · GTest · 现有 `bcos-evm/test/helpers/SparseStorageStateView.h`（生产 `LedgerStateView` 契约的镜像夹具）

**授权依据：** spec rev.7 决策 D3（`bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md` §1.3 已为本动作部分解禁"不改动 bcos-evm"）。本 plan 完全在 `bcos-evm` 内，与 `bcos-evm-ref` 无任何代码关系。

## 问题的实测依据

来自 `bcos-evm-ref/spike/`（M3.5 Phase 1，evmone 形状的等价工作负载，EEST v5.4.0，53,131 笔 tx）：

- 上游 evmone 的 `State::find()` **命中时写缓存**，但 nullopt 不写；即便如此，同一 tx 内对同一个不存在地址的重复查询仍占全部 `get_account` 调用的 **70%**、占全部账本读的 **27.9%**。
- **`bcos-evm` 的 `State::find()`（`eth/state/State.cpp:43`）连命中都不写缓存**：`m_accounts` 只在**写**路径经 `mutable_account()` 填充；所有读路径（`get_balance`/`get_nonce`/`get_code`/`get_code_size`/`copy_code`/`get_code_hash`/`account_exists`/`get_account`）在 overlay 未命中时**每次都回 base view**。故 bcos-evm 的浪费面**严格大于** evmone 的 27.9%。

代码层面的证据（`State.cpp` 共 **23** 处 `m_baseStateView->` 调用）：

| base 方法 | 调用点数 | 备注 |
|---|---|---|
| `get_account` | 9 | 含 `build_diff()` 对**每个** overlay 条目一次整账户重载 |
| `get_code` | 5 | `get_code_size()` / `copy_code()` **拉整段 code 只为取长度/切片** |
| `get_storage` | 3 | 本 plan **非目标**，见下 |
| `get_code_hash` | 2 | `State::get_code_hash` 先 `account_exists()` 再 `get_code_hash()` |
| `account_exists` | 2 | |
| `get_nonce` / `get_balance` | 各 1 | |

`StateView.hpp:35-37` 的注释已自陈该问题：*"the default derives from get_account (which ledger-backed views implement as a multi-read full load, code included) — backends that can answer existence with a single read should override this."*

`LedgerStateView` 的每个 lambda 都以 `syncWait(account.exists())` 起手并在为假时提前返回，故实测读代价：

| base 方法 | 命中 | 未命中 |
|---|---|---|
| `get_account` | 5（exists+balance+nonce+**code**+codeHash） | 1 |
| `account_exists` | 1 | 1 |
| `get_balance` / `get_nonce` | 2 | 1 |
| `get_code` | 2 | 1 |
| `get_code_hash` | 3（exists+code+codeHash） | 1 |
| `get_storage` | 2 | 1 |

一次 cold `EXTCODEHASH`（overlay 有 touch-only 条目、code 为空且未 dirty）当前 = `account_exists`(1) + `get_code_hash`(3) = **4 次账本读**，且同一地址下次再来还是 4 次。

## Global Constraints

- 仓库根（worktree）：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`；所有相对路径与 git 提交都以此为根。
- **不变量（本 plan 的正确性基石，Task 1 必须验证）**：`State` 的生存期内 base view 不可变——`State` 位于每笔交易的 `StateTransitionContext`，写全部落到 overlay，`applyStateDiff()` 在 `build_diff()` **之后**由 TE 执行。故 memo 无需失效。
- **memo 不是状态**：`mutable` 成员，**永不写入 journal，永不参与 rollback/checkpoint**。overlay（`m_accounts`）在任何查找中优先于 memo。
- **永不加宽读**：不得为服务 `get_balance` 而去调 base 的 `get_account`。逐字段记忆，各自沿用原本的 base 方法。
- **零行为变更**：本 plan 是纯性能改动。任何测试期望值的修改都视为缺陷。
- 与 `bcos-evm-ref` 无关；不 `#include` 也不链接它。
- 有 pre-commit clang-format 钩子；被拒时它会就地格式化，重新 `git add` 再提交。

## 非目标（本 plan 明确不做）

- **`get_storage` 的记忆化**：它与 `Account::storageReset`（见先例 plan `2026-07-08-overlay-storage-read-through.md`）及 `m_storageOriginal` 的语义交织，风险面独立。留作后续 plan。
- 不改 `StateView` 接口，不改 `LedgerStateView`（它在 `bcos-evm/storage/`，由 TE 构造，生存期不在本模块掌控）。
- 不改 `Account` 结构，不动 journal / checkpoint / warm 集合。

## File Structure

```
bcos-evm/eth/state/State.hpp          # 新增 BaseCacheEntry + m_baseCache + 6 个 base 访问器声明
bcos-evm/eth/state/State.cpp          # 6 个访问器实现；23 处调用点改为走访问器
bcos-evm/test/helpers/CountingStateView.h   # 新建：计数装饰器（包装任意 StateView）
bcos-evm/test/eth/StateBaseReadCacheTest.cpp # 新建：不变量 + 各字段 memo 的 TDD 测试
bcos-evm/test/cmake/*.cmake 或 test/CMakeLists.txt # 追加新测试源（Task 1 确认实际位置）
```

---

### Task 1: 计数夹具 + 基线测量 + 不变量验证（无生产代码改动）

**Files:**
- Create: `bcos-evm/test/helpers/CountingStateView.h`
- Create: `bcos-evm/test/eth/StateBaseReadCacheTest.cpp`
- Modify: `bcos-evm` 测试的 CMake 源清单（先 `grep -rn "SparseStorageStateView\|StateTransitionExecuteTest" bcos-evm/test --include=CMakeLists.txt --include=*.cmake` 定位实际注册处）

**Interfaces:**
- Produces: `bcos::evm::test::CountingStateView`——包装任意 `state::StateView`，逐方法计数；后续所有 Task 用它断言"读次数下降"。

- [ ] **Step 1: 验证不变量（先做，它决定整个方案成不成立）**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
grep -rn "applyStateDiff" bcos-evm --include='*.h' --include='*.cpp' | grep -v "storage/StateDiffApplier.h"
grep -rn "state::State state\|State state{" bcos-evm/eth/kernel bcos-evm/eth/apply
```
Expected: `applyStateDiff` 只在 `bcos-evm/storage/StateDiffApplier.h` 定义、且**不在 `State` 生存期内**被调用；`State` 是 `StateTransitionContext` 的成员（每 tx 一个）。
**若发现 base view 在 tx 执行中被写**（例如同一 `State` 跨块复用、或中途 apply），**立即 BLOCKED 并上报**——本 plan 的不变量不成立，方案需重新设计。把结论写进报告。

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

namespace bcos::evm::test
{
/// Wraps a StateView and counts every base read. Used to assert that State's
/// memoization removes repeated cold reads (M-N). Never used in production.
class CountingStateView : public state::StateView
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
    };

    explicit CountingStateView(state::StateView const& inner) noexcept : m_inner{inner} {}

    std::optional<state::Account> get_account(const evmc_address& address) const override
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
    state::StateView const& m_inner;
    mutable Counts m_counts;
};
}  // namespace bcos::evm::test
```

- [ ] **Step 3: 写基线测试（现在全部会显示"重复读"，这是 BEFORE 快照）**

`bcos-evm/test/eth/StateBaseReadCacheTest.cpp`：

```cpp
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/test/helpers/CountingStateView.h"
#include "bcos-evm/test/helpers/SparseStorageStateView.h"
#include <gtest/gtest.h>

using namespace bcos::evm;
using namespace bcos::evm::test;

namespace
{
constexpr evmc_address kAddr{{0x01}};
constexpr evmc_address kAbsent{{0x02}};

state::Account makeAccount()
{
    state::Account a;
    a.balance = bcos::u256{100};
    a.nonce = 7;
    a.code = bcos::bytes{0x60, 0x01};
    return a;
}
}  // namespace

// 不存在的账户被重复查询：base 读次数必须等于 1（负缓存）
TEST(StateBaseReadCache, AbsentAccountProbedOnce)
{
    SparseStorageStateView base;
    CountingStateView counting{base};
    state::State st{counting};

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_FALSE(st.get_account(kAbsent).has_value());
    }
    EXPECT_EQ(counting.counts().account, 1u) << "nullopt lookups must be memoized";
}

// 同一账户的同一字段被重复读：base 读次数必须等于 1
TEST(StateBaseReadCache, BalanceReadOnce)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(st.get_balance(kAddr), bcos::u256{100});
    }
    EXPECT_EQ(counting.counts().balance, 1u);
}

// 关键：读记忆化不得加宽读——读 balance 不应触发 get_account
TEST(StateBaseReadCache, BalanceReadDoesNotWidenToFullAccountLoad)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};

    (void)st.get_balance(kAddr);
    EXPECT_EQ(counting.counts().account, 0u) << "get_balance must not fall back to get_account";
}
```

- [ ] **Step 4: 注册测试源并构建，观察基线红棒**

先定位注册处：`grep -rn "StateTransitionExecuteTest" bcos-evm/test --include=CMakeLists.txt --include=*.cmake`，把 `eth/StateBaseReadCacheTest.cpp` 加进同一个可执行文件的源清单。

Run: 构建并只跑这三个用例。
Expected: `AbsentAccountProbedOnce` 与 `BalanceReadOnce` **红**（当前每次都回 base，计数分别是 5 和 5）；`BalanceReadDoesNotWidenToFullAccountLoad` **绿**（当前 `get_balance` 本来就走窄读）。把三条的实际计数抄进报告——这是 BEFORE 数字。

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/test/helpers/CountingStateView.h bcos-evm/test/eth/StateBaseReadCacheTest.cpp
git add <被修改的 CMake 文件>
git commit -m "test(bcos-evm): CountingStateView fixture + failing baseline for State base-read memoization"
```

---

### Task 2: 负缓存 + `account_exists` 记忆化

**Files:**
- Modify: `bcos-evm/eth/state/State.hpp`（新增 `BaseCacheEntry`、`m_baseCache`、`baseAccount()`/`baseExists()` 私有声明）
- Modify: `bcos-evm/eth/state/State.cpp`（实现两个访问器；把 `find()` 与 `account_exists()` 相关的 base 调用改为走访问器）

**Interfaces:**
- Produces（后续 Task 复用）：
```cpp
/// 记忆化的 base 读。memo 不是状态：mutable、不入 journal、不参与 rollback；
/// overlay(m_accounts) 在任何查找中优先。base view 在 State 生存期内不可变（Task 1 已验证）。
std::optional<Account> baseAccount(const evmc_address& address) const;
bool                   baseExists(const evmc_address& address) const;
```

- [ ] **Step 1: `State.hpp` 私有区新增 memo 结构与成员**（放在 `m_baseStateView` 声明之后）

```cpp
    /// Memoized base-view reads. NOT state: never journaled, never rolled back.
    /// Safe without invalidation because the base view is immutable for this State's
    /// lifetime (one tx) and the overlay `m_accounts` always wins on lookup.
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

（`State.hpp` 已 include `<optional>`/`<unordered_map>`；若无则补。）

- [ ] **Step 2: `State.cpp` 实现两个访问器**（放在 `State::find` 之前）

```cpp
std::optional<Account> State::baseAccount(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.account.has_value())
    {
        entry.account = m_baseStateView->get_account(address);
        // 交叉填充：ledger 视图的 get_account 已经把这些字段读回来了，别再单独读一次。
        // 注意 codeHash 不交叉填充——StateView::get_code_hash 有归一化语义（空 code -> emptyCodeHash，
        // 零 hash -> keccak256(code)），与 Account::codeHash 原始值不等价。
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

- [ ] **Step 3: 把 `find()` 与 2 处 `account_exists` 调用改为走访问器**

`State::find`（`State.cpp:43` 附近）：
```cpp
std::optional<Account> State::find(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second;
    }
    return baseAccount(address);
}
```

用 `grep -n "m_baseStateView->account_exists" bcos-evm/eth/state/State.cpp` 定位 2 处，各改为 `baseExists(address)`。

- [ ] **Step 4: 跑测试**

Run: 构建 + 跑 `StateBaseReadCache.*`
Expected: `AbsentAccountProbedOnce` **由红转绿**（`counts().account == 1`）；`BalanceReadDoesNotWidenToFullAccountLoad` 保持绿；`BalanceReadOnce` **仍红**（Task 3 处理）。

- [ ] **Step 5: 全量回归（不得有任何行为变化）**

Run: `bcos-evm` 的完整单测套件。
Expected: 全绿，且**无任何期望值被修改**。若出现失败，第一嫌疑是"某处依赖了重复读 base 的副作用"——报告并停下，不要改测试。

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp
git commit -m "perf(bcos-evm): memoize base get_account (incl. negative lookups) and account_exists

State::find() previously re-queried the base view on every m_accounts miss, and
never cached nullopt. Under LedgerStateView each such miss is a task::syncWait
ledger round-trip. The base view is immutable for a State's lifetime and the
overlay always wins on lookup, so the memo needs no invalidation and is never
journaled."
```

---

### Task 3: `balance` / `nonce` / `codeHash` 窄读记忆化

**Files:** Modify `bcos-evm/eth/state/State.hpp`、`State.cpp`

**Interfaces:**
- Consumes: Task 2 的 `m_baseCache` / `BaseCacheEntry`
- Produces: `baseBalance()` / `baseNonce()` / `baseCodeHash()`

- [ ] **Step 1: 追加失败测试**（`StateBaseReadCacheTest.cpp`）

```cpp
TEST(StateBaseReadCache, NonceReadOnce)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};
    for (int i = 0; i < 5; ++i) EXPECT_EQ(st.get_nonce(kAddr), 7u);
    EXPECT_EQ(counting.counts().nonce, 1u);
}

// EXTCODEHASH 冷读：当前是 account_exists(1) + get_code_hash(3) = 4 次账本读，且可重复
TEST(StateBaseReadCache, CodeHashReadOnce)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};
    auto const first = st.get_code_hash(kAddr);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(st.get_code_hash(kAddr), first);
    EXPECT_EQ(counting.counts().codeHash, 1u);
    EXPECT_LE(counting.counts().exists, 1u) << "exists must be memoized too (Task 2)";
}

// 交叉填充：一次 get_account 之后，balance/nonce 不应再回 base
TEST(StateBaseReadCache, FullAccountLoadCrossFillsNarrowFields)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};
    (void)st.get_account(kAddr);
    (void)st.get_balance(kAddr);
    (void)st.get_nonce(kAddr);
    EXPECT_EQ(counting.counts().balance, 0u);
    EXPECT_EQ(counting.counts().nonce, 0u);
}
```

- [ ] **Step 2: 跑，确认三条红**

Expected: `NonceReadOnce`(5)、`CodeHashReadOnce`、`FullAccountLoadCrossFillsNarrowFields` 均红。

- [ ] **Step 3: 实现三个访问器**（`State.cpp`）

```cpp
bcos::u256 State::baseBalance(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.balance.has_value())
    {
        entry.balance = m_baseStateView->get_balance(address);
    }
    return *entry.balance;
}

uint64_t State::baseNonce(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.nonce.has_value())
    {
        entry.nonce = m_baseStateView->get_nonce(address);
    }
    return *entry.nonce;
}

evmc_bytes32 State::baseCodeHash(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.codeHash.has_value())
    {
        entry.codeHash = m_baseStateView->get_code_hash(address);
    }
    return *entry.codeHash;
}
```

在 `State.hpp` 补三行声明。

- [ ] **Step 4: 替换调用点**

`grep -n "m_baseStateView->get_balance\|m_baseStateView->get_nonce\|m_baseStateView->get_code_hash" bcos-evm/eth/state/State.cpp` → 各改为对应访问器（共 1+1+2 处）。

- [ ] **Step 5: 跑测试 + 全量回归**

Expected: 新三条转绿；`BalanceReadOnce` 转绿；`BalanceReadDoesNotWidenToFullAccountLoad` 仍绿；`bcos-evm` 全套单测无变化。

- [ ] **Step 6: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp bcos-evm/test/eth/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): memoize base balance/nonce/codeHash narrow reads

Per-field memoization, never widening a read: get_balance still uses the base
view's get_balance (2 ledger reads under LedgerStateView), it just stops doing
it more than once per address per tx. A prior full get_account cross-fills
balance/nonce/code, but never codeHash — StateView::get_code_hash normalizes
(empty code -> emptyCodeHash, zero hash -> keccak256(code)) and is not
equivalent to Account::codeHash."
```

---

### Task 4: `code` 记忆化（顺带修掉"拉整段 code 只为取 size"）

**Files:** Modify `bcos-evm/eth/state/State.hpp`、`State.cpp`

- [ ] **Step 1: 追加失败测试**

```cpp
TEST(StateBaseReadCache, CodeReadOnceAcrossSizeAndCopy)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};

    EXPECT_EQ(st.get_code_size(kAddr), 2u);
    EXPECT_EQ(st.get_code_size(kAddr), 2u);
    uint8_t buf[2]{};
    EXPECT_EQ(st.copy_code(kAddr, 0, buf, sizeof(buf)), 2u);
    EXPECT_EQ(st.get_code(kAddr), (bcos::bytes{0x60, 0x01}));

    EXPECT_EQ(counting.counts().code, 1u) << "code must be fetched from base at most once";
}
```

- [ ] **Step 2: 跑，确认红**（当前 `counts().code` 为 4）

- [ ] **Step 3: 实现 `baseCode()` 并替换 5 处调用点**

```cpp
bcos::bytes const& State::baseCode(const evmc_address& address) const
{
    auto& entry = m_baseCache[address];
    if (!entry.code.has_value())
    {
        entry.code = m_baseStateView->get_code(address);
    }
    return *entry.code;
}
```

注意返回 `const&`（memo 持有），调用点若原本按值使用需保持语义：`get_code()` 仍按值返回一份拷贝（对外契约不变），但 `get_code_size()` / `copy_code()` 改为直接用 `baseCode(address)` 的引用，**不再拷贝整段 code**。

`grep -n "m_baseStateView->get_code(" bcos-evm/eth/state/State.cpp` 定位 5 处逐一替换。

- [ ] **Step 4: 跑测试 + 全量回归**

Expected: 新用例绿；`bcos-evm` 全套无变化。**特别留意 `copy_code` 的边界用例**（offset ≥ size、buffer_size == 0）仍绿。

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp bcos-evm/test/eth/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): memoize base code; stop pulling full code just to take its size

get_code_size() and copy_code() previously called base get_code() and discarded
the bytes, once per call. Now the code is fetched at most once per address per
tx and served from the memo by reference."
```

---

### Task 5: `build_diff()` 用 memo，取消每条 overlay 的整账户重载

**Files:** Modify `bcos-evm/eth/state/State.cpp`

**背景**：`build_diff()` 对 `m_accounts` 的**每一个**条目调 `m_baseStateView->get_account(address)`。在 `LedgerStateView` 下这是每个被写账户 5 次账本读，发生在交易结算阶段。这些账户在执行期几乎必然已被读过（`mutable_account()` 之前要先 `find()`），故 memo 命中率应接近 100%。

- [ ] **Step 1: 追加失败测试**

```cpp
TEST(StateBaseReadCache, BuildDiffReusesMemoizedBaseAccount)
{
    SparseStorageStateView base;
    base.insert_account(kAddr, makeAccount());
    CountingStateView counting{base};
    state::State st{counting};

    st.add_balance(kAddr, bcos::u256{1});   // 触发 mutable_account -> find -> baseAccount(memo)
    counting.reset();                        // 只统计 build_diff 阶段的读

    auto const diff = st.build_diff();
    EXPECT_EQ(counting.counts().total(), 0u) << "build_diff must not re-read the base view";
    EXPECT_FALSE(diff.accounts.empty());
}
```

- [ ] **Step 2: 跑，确认红**（当前 `build_diff` 会读 1 次 `get_account`）

- [ ] **Step 3: 改 `build_diff()`**

把 `auto const baseAccount = m_baseStateView->get_account(address);` 改为 `auto const baseAcc = baseAccount(address);`（注意与局部变量重名，改名避免遮蔽），其余比较逻辑不动。

- [ ] **Step 4: 跑测试 + 全量回归**

Expected: 新用例绿。**这是本 plan 风险最高的一步**——`build_diff` 的 base 值必须与执行期一致。全量回归（尤其 `TopLevelInsufficientBalanceStateDiffTest`、`EthFeeSettlementStateTest`）必须全绿。

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/eth/state/State.cpp bcos-evm/test/eth/StateBaseReadCacheTest.cpp
git commit -m "perf(bcos-evm): build_diff() reads the base memo instead of reloading each account

Every overlay entry triggered a full base get_account at settlement time (5
ledger reads each under LedgerStateView). Those accounts were already loaded
during execution, so the memo serves them for free -- and by construction the
diff now compares against exactly the base values execution saw."
```

---

### Task 6: 端到端测量 + 回归 + 记录

**Files:**
- Modify: `bcos-evm/test/eth/StateBaseReadCacheTest.cpp`（加一个端到端读次数断言）
- Create: `bcos-evm/docs/adr/0NN-state-base-read-memoization.md`（编号取当前 `docs/adr/` 最大值 +1）

- [ ] **Step 1: 端到端读次数对比**

在一个已有的、会走完整交易管线的测试（如 `StateTransitionExecuteTest` 的某个 fixture）外面套 `CountingStateView`，记录 memo 前/后的 `counts().total()`。
把 BEFORE（Task 1 Step 4 的数字）与 AFTER 写进报告与 ADR。

- [ ] **Step 2: 全量回归（硬 gate）**

Run（三项都必须绿，逐条把输出尾部录入报告）：
```bash
# 1) bcos-evm 单测全套
# 2) EEST state 全量
# 3) EEST blockchain 全量
```
具体命令以 `bcos-evm/test/eth-eest-test/README.md` 为准（`EEST_ROOT` 环境变量约定）。
Expected: 与本 plan 实施前**逐条一致**——`EthEestBlockchainFull` 的已知基线是 405 个失败（404 `fork_Frontier` + 1 `fork_Homestead`，pre-Cancun，与本改动无关）。**失败数不得增加，也不得减少**（减少同样是可疑信号，说明行为变了）。

- [ ] **Step 3: 写 ADR**

记录：不变量（base 不可变 + overlay 优先 ⇒ 无需失效）、"永不加宽读"原则、`codeHash` 不交叉填充的原因、`get_storage` 为何留在范围外、BEFORE/AFTER 读次数。

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/eth/StateBaseReadCacheTest.cpp bcos-evm/docs/adr/0NN-state-base-read-memoization.md
git commit -m "docs(bcos-evm): ADR + end-to-end read-count evidence for State base-read memoization"
```

---

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| **不变量不成立**（base view 在 `State` 生存期内被写） | Task 1 Step 1 先验证；不成立即 BLOCKED，不得继续 |
| `build_diff()` 的 base 值与执行期不一致 | 用同一个 memo ⇒ 按构造一致；Task 5 全量回归把关 |
| memo 被误当作状态（journal/rollback） | `mutable` + 只读访问器 + ADR 明文；测试 `AbsentAccountProbedOnce` 在 rollback 后仍应只读 1 次 |
| 加宽读导致冷路径回归 | 逐字段 memo；`BalanceReadDoesNotWidenToFullAccountLoad` 常驻断言 |
| `codeHash` 交叉填充引入语义错误 | **明确不交叉填充**（归一化语义不等价），Task 3 注释与 ADR 双记 |
| 内存增长（每 tx 一个 memo） | 生存期与 `State` 同（每 tx），条目数 ≤ 该 tx 触碰的地址数；与 `m_accounts` 同量级 |

## 预期收益

- 消除同一 tx 内的**全部**重复 base 读（当前 `State` 在读路径上零缓存）。
- `build_diff()` 少掉 `|m_accounts|` 次整账户加载（各 5 次账本读）。
- 一次冷 `EXTCODEHASH` 从 4 次账本读降到 4 次**首次**、之后 0 次。
- 参照 M3.5 在 evmone 形状上的实测，仅"负查询不缓存"一项即占全部账本读的 **27.9%**；`bcos-evm` 因连命中都不缓存，实际收益应更高。**准确数字由 Task 6 给出**——本 plan 不预先承诺具体百分比。

# M3.5 Phase 2：真实账本 ↔ `evmone::state` 桥接原型与三项开销实测

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 兑现 spec rev.4 对 M3.5 的原始要求（Phase 1 未完成的部分）：把 `StateViewAdapter` **接一次真实账本存储栈**（`EVMAccount` over `Rollbackable<MutableStorage>`——生产读路径去掉持久化后端的内存底板），实测三项开销的绝对值——①同步 `noexcept` 桥接开销 ②每 tx 重建 `State` 开销 ③code 按值返回开销——并按**预注册判据**给出 ETH 替换的 go/no-go。同时交付原 M-N 并入的**适配器侧负缓存**（~5 行）并测出其真实收益。

**Architecture:** 新建 `EvmoneLedgerStateView<Storage>`——header-only 模板（仿 `bcos-evm/storage/StateDiffApplier.h` 的模板-over-Storage 先例），实现 evmone 的 3 方法 `StateView`，每读一次 `task::syncWait(EVMAccount…)`（逐字对应生产 `LedgerStateView` 的 lambda 结构与提前返回语义）。内置三档缓存模式（none / negative / block）与读计数器。bench 目标只在**根构建树**内启用（`if(TARGET bcos-framework)` 门控）——`bcos-evm-ref` 的库与 standalone 构建不受影响。

**Tech Stack:** C++20 · Boost.Test（bench 用 `BOOST_AUTO_TEST_CASE` 承载，方便复用夹具；计时 `std::chrono::steady_clock`）· `executor_v1::MutableStorage` + `bcos::evm::Rollbackable`（transaction-executor）· `ledger::account::EVMAccount` + `bcos::crypto::Keccak256`（bcos-framework/bcos-crypto）· `evmone::state` / `evmone::testutils`（已导出的 port）

**授权依据：** spec rev.7 §7.1 M3.5 行（Phase 2 必做）；三条用户持久原则全部满足——最大化复用 evmone（缓存即其原生代码）、不考虑 FISCO、先 plan 后动手。

## Phase 1 的欠账（本 plan 要还的）

Phase 1（`bcos-evm-ref/spike/ReadAmplification.cpp`）只做了内存计数 + 手推成本模型，**未接任何账本、三项被点名的开销一项没测**，其 GO 判定已在 spec rev.6 降级为暂定。本 plan 的测量对象就是那三项，且**判据先于测量注册**（见下节）——不许赛后挪门柱。

## 预注册判据（先于任何测量写死；执行者不得修改，测量后只能对照宣读）

定义（全部在同一台机器、同一 Release 构建、内存底板上测得）：
- **R** = 经适配器的单次账户字段读中位延迟（ns/读）——Task 3 产出；
- **N** = 合成区块负载下、负缓存开启时的适配器读次数每 tx——Task 4 产出；
- **O = N × R** = 每 tx 桥接开销；
- **E** = 同负载下 evmone 纯执行（`TestState` 内存后端）的每 tx 中位耗时——Task 4 产出的对照腿。

判定规则：
| 结果 | 判定 |
|---|---|
| O ≤ 0.5 × E | **GO**——桥接开销在内存底板上不超过纯执行的一半；生产持久化后端的延迟对新旧两边一视同仁，形状差异不构成障碍 |
| 0.5 × E < O ≤ 2 × E | **GO（附带条件）**——必须实施 Phase 3 块级缓存；用 block 缓存档的实测 N 重算 O 复核 |
| O > 2 × E（即便 block 缓存档） | **NO-GO**——终局改判差分 oracle，写入 §7.2 |

阈值 0.5/2 是工程判断值，**在此预注册**；若用户想调整，须在 Task 3 开始前调整。
另两项独立记录（不进 go/no-go 公式，进报告）：③ 24KB code 按值拷贝的 µs/次与每 (tx,addr) 触发次数；负缓存 off→on 的 N 降幅（对照 Phase 1 预测的 27.9% 量级）。

## Global Constraints

- 仓库根（worktree）：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`；构建树复用 **`build-bcos-evm-check`**（已 configure、依赖已编）。
- **隔离不变**：`bcos-evm-ref` 不 `#include`/不链接 `bcos-evm`。本 plan 依赖的是 `bcos-framework`（`EVMAccount`）与 `transaction-executor`（`MutableStorage`/`Rollbackable`）——均非 `bcos-evm`，且这正是生产桥接的既定方向（spec §4.1 `StateViewAdapter` 的本职）。
- **适配器逐字对应生产 `LedgerStateView` 的读语义**（lambda 先 `exists()`、miss 提前返回），使 Task 3 测得的 R 可代表生产读路径形状；但**不 include 它**（隔离），以本 plan 附录 B 的语义对照表为准。
- FISCO：不考虑（用户持久原则）。
- 测量诚实性：所有数字由 bench 实测输出原样入报告；判据按上节宣读，不得改写；bench 关闭 CPU 频率波动干扰（报告标注机器/构建型号，迭代取中位数）。
- 已知边界（如实声明，不隐藏）：① 内存底板 ≠ 生产 RocksDB 延迟——测的是**形状差异的下界**，报告显著标注；② `has_storage` 恒填 `false`（`EVMAccount` 无廉价探测，EIP-7610 碰撞检查在 bench 负载中不出现；正确性差分测试避开 CREATE-collision 场景并记录此洞）；③ `get_account` 按 4 读建模（evmone `Account` 无 code 字段，fixture 恒写规范 codeHash，不触发 legacy 归一化）。
- pre-commit clang-format 钩子照旧。

## File Structure

```
bcos-evm-ref/adapter/EvmoneLedgerStateView.h     # 核心交付：header-only 模板适配器（3 方法 + 3 档缓存 + 计数器）
bcos-evm-ref/spike/bridge/BridgeCorrectnessTest.cpp  # Task 2：差分正确性（同 tx：adapter vs TestState）
bcos-evm-ref/spike/bridge/BridgeBench.cpp        # Task 3+4：微基准 + 区块负载基准
bcos-evm-ref/CMakeLists.txt                      # 追加 in-tree 门控目标
bcos-evm-ref/spike/bridge/REPORT.md              # Task 5：实测数字 + 判据宣读
```

---

### Task 1: in-tree bench 脚手架 + 存储夹具冒烟

**Files:**
- Modify: `bcos-evm-ref/CMakeLists.txt`
- Create: `bcos-evm-ref/spike/bridge/BridgeCorrectnessTest.cpp`（本 task 仅夹具冒烟，Task 2 填差分用例）

**Interfaces:**
- Produces: CMake 目标 `bcos-evm-ref-bridge-test` / `bcos-evm-ref-bridge-bench`（仅当 `TARGET bcos-framework` 存在，即根构建树内）；存储夹具函数 `makeFundedAccount(...)`。

- [ ] **Step 1: CMakeLists 追加门控段**（现有 spike 段之后）

```cmake
# M3.5 Phase 2：真实账本桥接 spike。依赖 bcos-framework/transaction-executor 目标，
# 仅在根构建树内可用（standalone 构建自动跳过）。
if(TARGET bcos-framework AND TARGET transaction-executor)
    find_package(Boost REQUIRED unit_test_framework)

    add_executable(bcos-evm-ref-bridge-test spike/bridge/BridgeCorrectnessTest.cpp)
    target_link_libraries(bcos-evm-ref-bridge-test PRIVATE
        bcosevmref::eth transaction-executor bcos-framework bcos-crypto
        Boost::unit_test_framework)
    add_test(NAME BcosEvmRefBridgeTest COMMAND bcos-evm-ref-bridge-test)

    add_executable(bcos-evm-ref-bridge-bench spike/bridge/BridgeBench.cpp)
    target_link_libraries(bcos-evm-ref-bridge-bench PRIVATE
        bcosevmref::eth transaction-executor bcos-framework bcos-crypto
        Boost::unit_test_framework)
    # bench 不进 ctest：手动运行，输出实测数字
endif()
```

（`transaction-executor` 目标名以 `transaction-executor/CMakeLists.txt` 实际为准——Step 3 验证时如不同则修正并记录。）

- [ ] **Step 2: 夹具冒烟**（`BridgeCorrectnessTest.cpp` 初版——照 `transaction-executor/tests/LedgerStateViewTest.cpp` 的模式建账户，读回验证）

```cpp
#define BOOST_TEST_MODULE BridgeCorrectnessTest
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evmref::bridge::test
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

template <class Storage>
void makeFundedAccount(Storage& storage, const evmc_address& address, bcos::u256 balance,
    uint64_t nonce, bcos::bytes code, bcos::crypto::Hash& hashImpl)
{
    ledger::account::EVMAccount account(storage, address, false);
    task::syncWait(account.create());
    task::syncWait(account.setBalance(balance));
    task::syncWait(account.setNonce(std::to_string(nonce)));
    if (!code.empty())
    {
        auto const codeHash = hashImpl.hash(bcos::bytesConstRef(code.data(), code.size()));
        task::syncWait(account.setCode(std::move(code), "", codeHash));
    }
}
}  // namespace

BOOST_AUTO_TEST_CASE(FixtureSmoke)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    executor_v1::MutableStorage storage;
    bcos::evm::Rollbackable<executor_v1::MutableStorage> rollbackable(storage);

    makeFundedAccount(rollbackable, addr(0x11), bcos::u256{1000}, 0, {}, *hashImpl);

    ledger::account::EVMAccount probe(rollbackable, addr(0x11), false);
    BOOST_CHECK(task::syncWait(probe.exists()));
    BOOST_CHECK_EQUAL(task::syncWait(probe.balance()), bcos::u256{1000});
}
}  // namespace bcos::evmref::bridge::test
```

- [ ] **Step 3: 构建并跑冒烟**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake --build build-bcos-evm-check --target bcos-evm-ref-bridge-test -j8
./build-bcos-evm-check/bcos-evm-ref/bcos-evm-ref-bridge-test
```
Expected: `FixtureSmoke` PASS。若 `BCOS_EVM_REF` 未在该树启用或目标名不符，先 `cmake build-bcos-evm-check -DBCOS_EVM_REF=ON` 重 configure / 修正目标名并记录。

- [ ] **Step 4: Commit**（`test(bcos-evm-ref): in-tree bridge spike scaffolding over real storage stack`）

---

### Task 2: `EvmoneLedgerStateView` 适配器 + 差分正确性

**Files:**
- Create: `bcos-evm-ref/adapter/EvmoneLedgerStateView.h`
- Modify: `bcos-evm-ref/spike/bridge/BridgeCorrectnessTest.cpp`

**Interfaces:**
- Produces（Task 3/4 依赖，签名勿改）：

```cpp
namespace bcos::evmref {
enum class BridgeCacheMode { None, Negative, Block };
template <class Storage>
class EvmoneLedgerStateView final : public evmone::state::StateView {
public:
    EvmoneLedgerStateView(Storage& storage, bcos::crypto::Hash& hashImpl,
        BridgeCacheMode mode = BridgeCacheMode::None);
    struct ReadCounts { size_t exists, balance, nonce, codeHash, code, storage; size_t total() const; };
    ReadCounts const& counts() const noexcept;
    void resetCounts() noexcept;
    // StateView: get_account / get_account_code / get_storage
};
}
```

- [ ] **Step 1: 写适配器**（完整实现；读语义逐字对应生产 `LedgerStateView`，见附录 B）

```cpp
// bcos-evm-ref/adapter/EvmoneLedgerStateView.h
#pragma once

#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Wait.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <test/state/state_view.hpp>
#include <optional>
#include <unordered_map>

namespace bcos::evmref
{
/// M3.5 Phase 2 桥接原型：evmone 3 方法 StateView over 真实账本存储栈。
/// 每次读 = task::syncWait(EVMAccount 协程)，先 exists() 且 miss 提前返回
/// ——与生产 bcos-evm/storage/LedgerStateView.h 的 lambda 结构逐字对应（附录 B）。
/// 缓存三档：None（裸）/ Negative（仅记不存在，~5 行，原 M-N 交付）/ Block（正+负，块级）。
/// has_storage 恒 false：EVMAccount 无廉价探测；EIP-7610 场景不在 bench 负载内（plan 已声明）。
enum class BridgeCacheMode
{
    None,
    Negative,
    Block
};

template <class Storage>
class EvmoneLedgerStateView final : public evmone::state::StateView
{
public:
    EvmoneLedgerStateView(
        Storage& storage, bcos::crypto::Hash& hashImpl, BridgeCacheMode mode = BridgeCacheMode::None)
      : m_storage(storage), m_hashImpl(hashImpl), m_mode(mode)
    {}

    struct ReadCounts
    {
        size_t exists{0};
        size_t balance{0};
        size_t nonce{0};
        size_t codeHash{0};
        size_t code{0};
        size_t storage{0};
        [[nodiscard]] size_t total() const
        {
            return exists + balance + nonce + codeHash + code + storage;
        }
    };

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        if (m_mode != BridgeCacheMode::None)
        {
            if (auto it = m_cache.find(addr); it != m_cache.end())
            {
                if (!it->second.has_value())
                {
                    return std::nullopt;  // 负缓存命中
                }
                if (m_mode == BridgeCacheMode::Block)
                {
                    return it->second;  // 正缓存命中（仅 Block 档）
                }
            }
        }

        ledger::account::EVMAccount account(m_storage, toLedgerAddress(addr), false);
        ++m_counts.exists;
        if (!task::syncWait(account.exists()))
        {
            if (m_mode != BridgeCacheMode::None)
            {
                m_cache.emplace(addr, std::nullopt);  // 负缓存写入（Negative/Block 共用）
            }
            return std::nullopt;
        }

        Account result;
        ++m_counts.balance;
        result.balance = toUint256(task::syncWait(account.balance()));
        ++m_counts.nonce;
        if (auto nonce = task::syncWait(account.nonce()); nonce.has_value() && !nonce->empty())
        {
            result.nonce = bcos::u256(*nonce).convert_to<uint64_t>();
        }
        ++m_counts.codeHash;
        result.code_hash = toBytes32(task::syncWait(account.codeHash()));
        result.has_storage = false;  // 见类注释

        if (m_mode == BridgeCacheMode::Block)
        {
            m_cache.emplace(addr, result);
        }
        return result;
    }

    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        ledger::account::EVMAccount account(m_storage, toLedgerAddress(addr), false);
        ++m_counts.exists;
        if (!task::syncWait(account.exists()))
        {
            return {};
        }
        ++m_counts.code;
        evmc::bytes out;
        if (auto codeEntry = task::syncWait(account.code()); codeEntry.has_value())
        {
            auto const view = codeEntry->get();
            out.assign(view.begin(), view.end());  // 按值拷贝——正是待测的开销 ③
        }
        return out;
    }

    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        ledger::account::EVMAccount account(m_storage, toLedgerAddress(addr), false);
        ++m_counts.exists;
        if (!task::syncWait(account.exists()))
        {
            return {};
        }
        ++m_counts.storage;
        return toBytes32(task::syncWait(account.storage(toLedgerBytes32(key))));
    }

    [[nodiscard]] ReadCounts const& counts() const noexcept { return m_counts; }
    void resetCounts() noexcept { m_counts = {}; }

private:
    // evmc::address / evmc_address 与 evmc::bytes32 / evmc_bytes32 的互转薄函数；
    // bcos::u256 -> intx::uint256 经 evmc bytes32 中转。实现约 20 行，执行者按两侧
    // 头文件（evmc.hpp 的 C++ 包装 vs bcos-framework 的 C 结构使用面）补齐并单测。
    static const evmc_address& toLedgerAddress(const evmc::address& a) noexcept;
    static evmc::bytes32 toBytes32(const bcos::crypto::HashType& h) noexcept;
    static evmc::bytes32 toBytes32(const evmc_bytes32& b) noexcept;
    static evmc_bytes32 toLedgerBytes32(const evmc::bytes32& b) noexcept;
    static intx::uint256 toUint256(const bcos::u256& v) noexcept;

    Storage& m_storage;
    bcos::crypto::Hash& m_hashImpl;
    BridgeCacheMode m_mode;
    mutable std::unordered_map<evmc::address, std::optional<Account>> m_cache;
    mutable ReadCounts m_counts;
};
}  // namespace bcos::evmref
```

（evmone 的 `evmc::address` 有 `std::hash` 特化，`unordered_map` 直接可用。互转函数与 `noexcept` 边界内的 `syncWait` 异常问题：`EVMAccount` 读在存储异常时会抛——`noexcept` 方法内未捕获即 `std::terminate`。spike 级接受并注释；生产版需 catch-and-flag，记入报告的"生产化清单"。）

- [ ] **Step 2: 差分正确性测试**（同一笔转账：adapter-over-真存储 vs `TestState` 内存——receipt 与 state_diff 必须一致）

```cpp
BOOST_AUTO_TEST_CASE(TransferMatchesTestStateReference)
{
    using namespace evmone;
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    // 腿 1：真实存储栈
    executor_v1::MutableStorage storage;
    bcos::evm::Rollbackable<executor_v1::MutableStorage> rollbackable(storage);
    makeFundedAccount(rollbackable, addr(0xA1), bcos::u256{1000000000000000000ULL}, 0, {}, *hashImpl);
    bcos::evmref::EvmoneLedgerStateView<decltype(rollbackable)> bridge(rollbackable, *hashImpl);

    // 腿 2：TestState 参照（与 M2 冒烟同构）
    test::TestState ref;
    ref[0xa1_address_last /* 同地址，helper 转换 */] = {.nonce = 0, .balance = /* 1e18 */};

    // 同一笔 21000 转账分别经 bcos::evmref::eth::runTransaction 执行
    // 断言：status、gas_used、state_diff 的 modified_accounts（地址集、balance、nonce）逐项相等
    // （代码同 M2 的 EthTransitionTest 模式，此处省略重复；执行者照抄该测试的交易构造。）
}
```

（完整交易构造照 `bcos-evm-ref/test/eth/EthTransitionTest.cpp` 的 `SimpleTransfer21000`——同一模块内的既有先例，非跨模块引用。再加一条负查询用例：向不存在地址转账，两腿的 receipt/diff 一致。）

- [ ] **Step 3: 跑测试全绿；Commit**（`feat(bcos-evm-ref): EvmoneLedgerStateView bridge adapter with differential correctness vs TestState`）

---

### Task 3: 开销①实测——单读延迟 R + 读语义计数验证

**Files:** Modify `bcos-evm-ref/spike/bridge/BridgeBench.cpp`（新建）

- [ ] **Step 1: 微基准**——预置 1000 个账户（500 带 24KB code，500 EOA）+ 每账户 16 个 storage 槽；然后：
  - `get_account` 命中/未命中各 10 万次（地址轮转防分支预测偏置），记录中位 ns 与 `counts()`（验证命中 4 读 / miss 1 读的模型）；
  - `get_storage` 命中 10 万次（中位 ns，2 读模型验证）；
  - `get_account_code` 对 24KB 合约 1 万次（中位 µs——这就是开销③的原子测量）；
  - 对照腿：同样操作打在 `test::TestState` 上（内存 map 基线），得出 **syncWait+EVMAccount+storage2 栈的净开销**。
- [ ] **Step 2: 输出表**（每行：操作 / adapter ns / TestState ns / 倍数 / 模型读数 vs 实测读数）。**R = get_account 命中中位 ns / 4**（单字段读折算）。
- [ ] **Step 3: Commit**（`bench(bcos-evm-ref): single-read latency over the real storage stack`）

---

### Task 4: 开销②③实测——区块负载 + 三档缓存

**Files:** Modify `BridgeBench.cpp`

- [ ] **Step 1: 合成区块负载**——50 个账户（10 个带 24KB code），200 笔 tx/块（80% 转账、20% 调用大合约的简单函数），**每 tx 新建 `State`**（evmone `transition()` 内部固有行为，正是开销②的来源）。三档各跑 20 个块取中位：
  - `None`：裸桥接——N₀ 读/tx、t₀ µs/tx；
  - `Negative`：负缓存（块级实例）——N₁、t₁；**N₀→N₁ 的降幅对照 Phase 1 预测的 27.9% 量级**；
  - `Block`：正+负块级缓存——N₂、t₂；`get_account_code` 触发次数单列（开销③的每块总量 = 次数 × Task 3 的 µs/次）。
  - 对照腿 **E**：同负载全程 `TestState`（无桥接）——纯执行每 tx 中位 µs。
- [ ] **Step 2: 宣读预注册判据**——N 取 `Negative` 档，O = N × R，对照 E 查表得出判定；若落入条件档，用 `Block` 档复核。**判定结果只许宣读，不许解释性改写。**
- [ ] **Step 3: Commit**（`bench(bcos-evm-ref): block-workload bridge overhead across three cache modes`）

---

### Task 5: 报告 + spec §7.2 更新

**Files:**
- Create: `bcos-evm-ref/spike/bridge/REPORT.md`
- Modify: spec `2026-07-08-bcos-evm-ref-evmone-reuse-design.md` §7.1 M3.5 行 + §7.2 判据 2

- [ ] **Step 1: REPORT.md**——机器/构建型号、全部实测表格原样、三项开销的绝对值结论、负缓存实测收益 vs Phase 1 预测、判据宣读（原文引用预注册表）、**内存底板≠生产延迟的显著标注**、生产化清单（noexcept 边界的异常处理、has_storage 探测、缓存失效策略若接入区块流水线）。
- [ ] **Step 2: spec 更新**——M3.5 行标 Phase 2 ✅ 与判定结果；§7.2 判据 2 从"暂定"改为实测结论（GO / GO 附条件 / NO-GO 三选一，如实）。
- [ ] **Step 3: Commit**（`docs(bcos-evm-ref): M3.5 Phase 2 report — three bridge costs measured, verdict per pre-registered criteria`）

---

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 判据被赛后调整（Phase 1 的病） | 判据预注册于本 plan；Task 4 Step 2 只许宣读 |
| 内存底板误当生产延迟 | 报告显著标注；判据设计本身只比较"形状差异"（两边同底板） |
| 互转函数写错（u256/bytes32 端序） | Task 2 差分正确性测试兜底（balance/nonce 逐项相等） |
| `noexcept` 内 syncWait 抛异常 → terminate | spike 级接受 + 注释；入生产化清单 |
| bench 噪声 | 中位数 + 迭代量级 10⁴–10⁵ + Release 构建 |
| 目标名/依赖与 plan 假设不符 | Task 1 Step 3 显式验证并记录修正 |

## 附录 B：与生产 `LedgerStateView` 的读语义对照（不 include 它，以此表为准）

| 方法 | 生产 lambda 语义 | 本适配器 |
|---|---|---|
| 账户读 | `exists()` 先行，false 提前返回（1 读）；命中续读 balance/nonce/(code)/codeHash | 同结构；**不读 code**（evmone `Account` 无 code 字段）→ 命中 4 读 |
| code 读 | `exists()` + `code()`（2 读） | 同 |
| storage 读 | `exists()` + `storage(key)`（2 读） | 同 |
| codeHash 归一化 | 空 code→emptyCodeHash、零 hash→keccak(code) | fixture 恒写规范 codeHash，不触发；生产化清单记补 |

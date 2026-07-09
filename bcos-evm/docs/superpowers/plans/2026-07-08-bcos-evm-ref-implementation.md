# bcos-evm-ref M1+M2 Implementation Plan（适配层 + ETH 路径 EEST state 对照绿）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 spec rev.3 的 M1（StateDiffWriteback + deleted_accounts 语义验证）与 M2（`eth::runTransaction`/`runBlockFinalize` 薄封装 + EEST state fixtures 对照绿）。

**Architecture:** 新建仓库顶层 `bcos-evm-ref/` 模块（可独立 configure，也可经 option 挂入根构建），链接 M0 已导出的 `evmone::state` + `evmone::testutils`（vcpkg overlay port，REF `3585c2cb`）。ETH 路径零 fee 逻辑：validate→transition 直调 evmone；测试后端复用 evmone `TestState`，判据 = `mpt_hash` stateRoot + `logs_hash` 逐位对照。

**Tech Stack:** C++20 · CMake ≥3.25 · vcpkg manifest（overlay ports 复用 `../ports/`）· evmone REF `3585c2cb`（`evmone::state`/`evmone::testutils`/`evmone::evmone`）· GTest（vcpkg）· EEST fixtures（`EVM_REF_EEST_ROOT` 环境约定）

**Spec:** `bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md`（rev.3）

## Global Constraints

- 仓库根（worktree）：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`；下文相对路径均以此为根，git 提交都发生在此 worktree。
- evmone 版本锁定 REF `3585c2cb`（v0.21.0 tag + SM3 补丁）：`TransactionReceipt` **无 `gas_refund` 字段**；不得引用本地 evmone HEAD 的 API。
- 与现有 `bcos-evm/` **严格隔离**：不 `#include` 其任何头、不链接其任何 target（spec §3.2）。
- 命名空间 `bcos::evmref`；目标 fork 过滤 `rev >= EVMC_CANCUN`（spec §1.2）。
- REF 的 `validate_transaction` 第 6 参为 `int64_t blob_gas_left`，由调用方用 `BlobParams` 预计算（spec §2/§4.2）；Cancun 的 `max_blob_gas_per_block` = 6×131072 = 786432。
- vcpkg 工具链：`vcpkg/scripts/buildsystems/vcpkg.cmake`（worktree 内符号链接，已 bootstrap）；`builtin-baseline` 固定 `f6729a3ac3bfdefc999aa8e3664f8014370886b8`。
- EEST fixtures 经 `EVM_REF_EEST_ROOT` 环境变量提供（与 `bcos-evm/test/eth-eest-test` 共享约定不共享代码）；未设置时 EEST 测试 GTEST_SKIP，不算失败。
- **EEST 版本 pin**（spec §6/§9 硬要求）：Task 5 选定一个 EEST stable release tag，写入 `bcos-evm-ref/test/EEST_VERSION` 提交入库，README 从该文件引用而非手抄；skip 准入 = **Cancun/Prague 必须 0 skip**（出现"evmone 也失败"的用例视为 tag 配对选错，换 tag 重跑），仅 Osaka 允许 skip 且逐条记录原因。CI tarball 缓存留 M6。

## 与 spec 的声明偏差（有意为之，spec 不改）

1. spec §3.1 列 `StateViewAdapter.h` + `BlockHashesAdapter.h` 两个占位头 → plan 合并为一个 `StateViewAdapter.h`（两个 using 别名都在，功能等价）。
2. spec §3.1 列 `StateDiffWriteback.h/.cpp` → plan 为 header-only（v1 委托 `TestState::apply` 仅一行）。注意由此 `adapter/` 依赖 `test/utils/test_state.hpp`（属 `evmone::testutils`）——超出 spec §3.2 "只依赖 evmone::state 公开头"的字面边界，v1 明示豁免。（原括注"eth 库不链接 testutils"已被偏差 #4 取代：fix wave 1 后 `bcos-evm-ref-eth` PUBLIC 链接 evmone::testutils，链接闭包与公开头一致。）
3. spec §8.6 "顶层平铺 add_subdirectory（M1）" → plan 移至 Task 6 且以 `option(BCOS_EVM_REF ... OFF)` 门控（默认不构建，避免影响主构建；standalone 模式已覆盖 M1–M2 期间的构建需求）。
4. review fix wave 1（用户裁定）：公开头移至 bcos-evm-ref/include/bcos-evm-ref/，PUBLIC include 从仓库根收窄为该 include 目录（编译期强制隔离）；bcos-evm-ref-eth PUBLIC 链接增加 evmone::testutils（公开缝头依赖 TestState::apply）。

## File Structure

```
ports/evmone/                       # M0 已改（portfile.cmake + vcpkg.json），Task 1 提交
bcos-evm-ref/
├── CMakeLists.txt                  # 可独立 project()，亦可被根 add_subdirectory
├── vcpkg.json                      # standalone manifest: evmone + gtest
├── vcpkg-configuration.json        # overlay-ports → ../ports/{evmone,intx,blst}
├── adapter/
│   ├── StateViewAdapter.h          # v1 占位：真账本桥接接口（纯注释+别名，无实现）
│   └── StateDiffWriteback.h        # applyStateDiff(TestState&, StateDiff) 薄缝
├── eth/
│   ├── EthTransition.h             # runTransaction / runBlockFinalize 声明
│   └── EthTransition.cpp
└── test/
    ├── CMakeLists.txt
    └── eth/
        ├── StateDiffWritebackTest.cpp   # M1: EIP-6780 / EIP-161 删除语义
        ├── EthTransitionTest.cpp        # M2 单元：21000 转账
        └── EestStateTest.cpp            # M2: EEST state fixture 对照
```

任务↔里程碑：Task 1–2 = 骨架（M1 前置）；Task 3 = M1；Task 4–6 = M2。

---

### Task 1: 提交 M0 port 改动

**Files:**
- Commit（已在工作区，未提交）: `ports/evmone/portfile.cmake`、`ports/evmone/vcpkg.json`

**Interfaces:**
- Produces: vcpkg 安装树中的 `evmone::state`、`evmone::testutils` imported targets（`find_package(evmone CONFIG)` 后可用），头文件树 `include/test/state/`、`include/test/utils/`。后续所有 Task 依赖。

- [ ] **Step 1: 核对工作区改动只含 port 两文件的预期内容**

Run: `git -C /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor diff --stat ports/evmone/`
Expected: 恰好 `portfile.cmake`（约 +100 行：EVMONE_STATE 注入、双库安装、include/test 头树、config 追加两 target）与 `vcpkg.json`（port-version 1 + nlohmann-json）。

- [ ] **Step 2: 提交**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git add ports/evmone/portfile.cmake ports/evmone/vcpkg.json
git commit -m "build(vcpkg): export evmone::state and evmone::testutils from evmone port

Inject EVMONE_STATE option (bypasses EVMONE_TESTING/hunter), install
libevmone-state.a + libevmone.testutils.a with include/test/{state,utils}
header tree, add imported targets to the manual config. Smoke-verified:
validate_transaction -> transition -> TestState::apply -> mpt_hash."
```

---

### Task 2: bcos-evm-ref 骨架（standalone 构建 + 空测试跑通）

**Files:**
- Create: `bcos-evm-ref/CMakeLists.txt`
- Create: `bcos-evm-ref/vcpkg.json`
- Create: `bcos-evm-ref/vcpkg-configuration.json`
- Create: `bcos-evm-ref/eth/EthTransition.h`（本 Task 仅空 namespace 占位）
- Create: `bcos-evm-ref/eth/EthTransition.cpp`（空实现文件）
- Create: `bcos-evm-ref/test/CMakeLists.txt`
- Create: `bcos-evm-ref/test/eth/EthTransitionTest.cpp`（本 Task 仅 1 个恒真用例验证链路）

**Interfaces:**
- Consumes: Task 1 的 `evmone::state`/`evmone::testutils` targets。
- Produces: CMake target `bcos-evm-ref-eth`（alias `bcosevmref::eth`，PUBLIC 链接 `evmone::state`+`evmone::evmone`，PUBLIC include = 仓库根，头以 `<bcos-evm-ref/...>` 引用）；测试可执行 `bcos-evm-ref-eth-tests`。

- [ ] **Step 1: 写 `bcos-evm-ref/vcpkg.json`**

```json
{
    "name": "bcos-evm-ref",
    "version-string": "0.1.0",
    "builtin-baseline": "f6729a3ac3bfdefc999aa8e3664f8014370886b8",
    "dependencies": [
        "evmone",
        "gtest"
    ]
}
```

- [ ] **Step 2: 写 `bcos-evm-ref/vcpkg-configuration.json`**（相对路径复用 worktree overlay ports）

```json
{
    "overlay-ports": [
        "../ports/evmone",
        "../ports/intx",
        "../ports/blst"
    ]
}
```

- [ ] **Step 3: 写 `bcos-evm-ref/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_NAME)
    set(BCOS_EVM_REF_STANDALONE ON)
else()
    set(BCOS_EVM_REF_STANDALONE OFF)
endif()

project(bcos-evm-ref LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm-ref-eth STATIC eth/EthTransition.cpp)
add_library(bcosevmref::eth ALIAS bcos-evm-ref-eth)
# PUBLIC include = 仓库根，使消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用
target_include_directories(bcos-evm-ref-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bcos-evm-ref-eth PUBLIC evmone::state evmone::evmone)

option(BCOS_EVM_REF_TESTS "Build bcos-evm-ref tests" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

- [ ] **Step 4: 写 `bcos-evm-ref/eth/EthTransition.h` 占位与空 `.cpp`**

```cpp
// bcos-evm-ref/eth/EthTransition.h
#pragma once

namespace bcos::evmref::eth
{
// Task 4 填充 runTransaction / runBlockFinalize
}  // namespace bcos::evmref::eth
```

```cpp
// bcos-evm-ref/eth/EthTransition.cpp
#include <bcos-evm-ref/eth/EthTransition.h>
```

- [ ] **Step 5: 写 `bcos-evm-ref/test/CMakeLists.txt`**

```cmake
find_package(GTest CONFIG REQUIRED)

add_executable(bcos-evm-ref-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-ref-eth-tests PRIVATE
    bcosevmref::eth
    evmone::testutils
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME BcosEvmRefEthTests COMMAND bcos-evm-ref-eth-tests)
```

- [ ] **Step 6: 写链路验证用例 `bcos-evm-ref/test/eth/EthTransitionTest.cpp`**（真实 include evmone 头以验证头树，恒真断言本 Task 后由 Task 4 替换）

```cpp
#include <bcos-evm-ref/eth/EthTransition.h>
#include <gtest/gtest.h>
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>

TEST(Skeleton, HeadersAndLinkOk)
{
    evmone::test::TestState state;
    EXPECT_TRUE(state.empty());
}
```

- [ ] **Step 7: configure + build + ctest**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake -B bcos-evm-ref/build -S bcos-evm-ref \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build bcos-evm-ref/build
ctest --test-dir bcos-evm-ref/build --output-on-failure
```

Expected: configure 阶段 vcpkg 安装 evmone（M0 二进制缓存命中，分钟级）+ gtest；`100% tests passed, 1 tests`。

- [ ] **Step 8: Commit**

```bash
git add bcos-evm-ref/
git commit -m "feat(bcos-evm-ref): module skeleton linking evmone::state (spec rev.3 M1 scaffold)"
```

---

### Task 3: M1 — StateDiffWriteback 与 deleted_accounts 语义（TDD）

**Files:**
- Create: `bcos-evm-ref/adapter/StateDiffWriteback.h`
- Create: `bcos-evm-ref/adapter/StateViewAdapter.h`
- Create: `bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp`
- Modify: `bcos-evm-ref/test/CMakeLists.txt`（追加源文件）

**Interfaces:**
- Consumes: `evmone::test::TestState`（`test/utils/test_state.hpp`：`TestState : StateView + std::map<address, TestAccount>`，`TestAccount{uint64_t nonce; uint256 balance; map<bytes32,bytes32> storage; bytes code;}`，成员 `void apply(const state::StateDiff&)`）；`evmone::state::transition/validate_transaction`。
- Produces: `bcos::evmref::applyStateDiff(evmone::test::TestState&, const evmone::state::StateDiff&)`——后续 harness（Task 5）与未来 OP 编排统一走此缝写回。

背景（spec §4.1）：**不能假设 Cancun 后 `deleted_accounts` 恒空**（`state_diff.hpp` 里"恒空"的注释是上游过时注释，`build_diff` 实码会填充）。测试分两层：
- **缝契约测试**（手工构造 diff）：写回缝是未来真账本实现要满足的规约——删除处理、storage 置 0 = erase 而非存零、code 可选覆盖。这层必须手工构造，否则断言可能被空转实现假绿（评审实证：6780 场景中被删合约在 pre 里本来就不存在，`count==0` 对空转恒真）。
- **语义发现测试**（真实交易全链路）：EIP-6780 同交易创建即自毁、EIP-161 空账户被触碰后擦除，证明 evmone 真的会产出删除项。
关键构造细节（对照 REF 源码核实）：6780 用例必须 `tx.value = 1`——若 value=0，SELFDESTRUCT 的 beneficiary 被 touch 后保持空账户，也进 `deleted_accounts`（EIP-161 分支），列表变 2 项且顺序不定；value=1 使 beneficiary 非空，删除项唯一且必为 CREATE 地址。

- [ ] **Step 1: 写失败测试 `bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp`**

```cpp
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/state/host.hpp>  // compute_create_address
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace evmone;
using namespace evmc::literals;
using namespace intx::literals;

namespace
{
constexpr auto kSender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
constexpr int64_t kCancunBlobGasLeft = 786432;  // 6 blobs * 131072 (EIP-4844)

state::BlockInfo makeBlock()
{
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.coinbase = 0x00000000000000000000000000000000c014ba5e_address;
    return block;
}

state::Transaction makeTx()
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = 200'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    return tx;
}

// validate + transition；校验失败时报告并返回 nullopt。diff 不写回。
std::optional<state::TransactionReceipt> run(test::TestState& pre, const state::Transaction& tx)
{
    const auto block = makeBlock();
    test::TestBlockHashes hashes;
    evmc::VM vm{evmc_create_evmone()};
    const auto validated = state::validate_transaction(
        pre, block, tx, EVMC_CANCUN, block.gas_limit, kCancunBlobGasLeft);
    if (const auto* err = std::get_if<std::error_code>(&validated))
    {
        ADD_FAILURE() << "validate: " << err->message();
        return std::nullopt;
    }
    return state::transition(pre, block, hashes, tx, EVMC_CANCUN, vm,
        std::get<state::TransactionProperties>(validated));
}
}  // namespace

// ============ 缝契约测试（手工构造 diff：未来真账本写回实现的规约） ============

TEST(StateDiffWriteback, ContractDeletesListedAccount)
{
    const auto victim = 0x00000000000000000000000000000000dead0001_address;
    test::TestState state;
    state[victim] = {.nonce = 1, .balance = 7};
    state[victim].storage[0x01_bytes32] = 0x02_bytes32;
    ASSERT_EQ(state.count(victim), 1u);  // 前置断言：防空转假绿

    state::StateDiff diff;
    diff.deleted_accounts.push_back(victim);
    bcos::evmref::applyStateDiff(state, diff);

    EXPECT_EQ(state.count(victim), 0u);
}

TEST(StateDiffWriteback, ContractStorageZeroMeansErase)
{
    const auto acct = 0x00000000000000000000000000000000dead0002_address;
    const auto k1 = 0x01_bytes32;
    const auto k2 = 0x02_bytes32;
    test::TestState state;
    state[acct] = {.nonce = 1, .balance = 0};
    state[acct].storage[k2] = 0xaa_bytes32;

    state::StateDiff diff;
    auto& entry = diff.modified_accounts.emplace_back();
    entry.addr = acct;
    entry.nonce = 1;
    entry.balance = 0;
    entry.code = std::nullopt;  // 不动 code
    entry.modified_storage = {
        {k1, 0x0b_bytes32},  // 新写入
        {k2, {}},            // 置 0 = 删除该槽
    };
    bcos::evmref::applyStateDiff(state, diff);

    EXPECT_EQ(state.at(acct).storage.at(k1), 0x0b_bytes32);
    EXPECT_EQ(state.at(acct).storage.count(k2), 0u);  // erase 而非存零（真账本最易做错的一条）
}

// ============ 语义发现测试（真实交易全链路产生 diff） ============

// EIP-6780: 合约在创建它的同一笔交易内 SELFDESTRUCT -> 进 deleted_accounts。
// tx.value 必须为 1：value=0 时 beneficiary 被 touch 后保持空账户，
// 也会以 EIP-161 分支进 deleted_accounts（列表变 2 项且顺序不定）。
TEST(StateDiffWriteback, DeletesSameTxSelfdestruct)
{
    test::TestState pre;
    pre[kSender] = {.nonce = 0, .balance = 1'000'000'000'000'000'000_u256};

    auto tx = makeTx();
    tx.to = {};    // 合约创建
    tx.value = 1;  // 使 beneficiary 非空，删除项唯一
    // initcode: PUSH20 0x...beef ; SELFDESTRUCT
    tx.data = *evmc::from_hex("73" "000000000000000000000000000000000000beef" "ff");

    const auto receipt = run(pre, tx);
    ASSERT_TRUE(receipt.has_value());
    ASSERT_EQ(receipt->status, EVMC_SUCCESS);
    ASSERT_EQ(receipt->state_diff.deleted_accounts.size(), 1u);
    EXPECT_EQ(receipt->state_diff.deleted_accounts[0],
        state::compute_create_address(kSender, 0));

    bcos::evmref::applyStateDiff(pre, receipt->state_diff);
    EXPECT_EQ(pre.count(receipt->state_diff.deleted_accounts[0]), 0u);
}

// EIP-161: pre-state 预置空账户被零值转账触碰 -> 擦除
TEST(StateDiffWriteback, ErasesTouchedEmptyAccount)
{
    const auto empty = 0x00000000000000000000000000000000c0ffee00_address;
    test::TestState pre;
    pre[kSender] = {.nonce = 0, .balance = 1'000'000'000'000'000'000_u256};
    pre[empty] = {};  // 空账户：nonce=0, balance=0, 无 code

    auto tx = makeTx();
    tx.to = empty;
    tx.value = 0;

    const auto receipt = run(pre, tx);
    ASSERT_TRUE(receipt.has_value());
    ASSERT_EQ(receipt->status, EVMC_SUCCESS);
    ASSERT_FALSE(receipt->state_diff.deleted_accounts.empty());

    ASSERT_EQ(pre.count(empty), 1u);  // 前置断言：防空转假绿
    bcos::evmref::applyStateDiff(pre, receipt->state_diff);
    EXPECT_EQ(pre.count(empty), 0u);
}
```

- [ ] **Step 2: 追加到 `bcos-evm-ref/test/CMakeLists.txt` 并跑，确认编译失败**

`add_executable` 源列表追加一行 `eth/StateDiffWritebackTest.cpp`。

Run: `cmake --build bcos-evm-ref/build 2>&1 | tail -5`
Expected: FAIL — `'bcos-evm-ref/adapter/StateDiffWriteback.h' file not found`。

- [ ] **Step 3: 写空转占位实现，让编译通过（红棒环节的前半）**

```cpp
// bcos-evm-ref/adapter/StateDiffWriteback.h —— 本 Step 为空转占位，Step 5 换真实现
#pragma once

#include <test/utils/test_state.hpp>

namespace bcos::evmref
{
inline void applyStateDiff(evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    (void)state;
    (void)diff;
}
}  // namespace bcos::evmref
```

```cpp
// bcos-evm-ref/adapter/StateViewAdapter.h
#pragma once

#include <test/state/state_view.hpp>

namespace bcos::evmref
{
/// v1 占位（spec §3.1/§4.1，BlockHashesAdapter 合并于此，见"声明偏差"节）：
/// 测试后端直接用 evmone::test::TestState。
/// 真账本桥接时在此实现 evmone::state::StateView 的三个只读方法；
/// 注意 StateView 是同步 noexcept 接口且 get_account_code 按值返回整段代码，
/// 桥接协程账本的性能评估见 spec §7.2（M3.5 spike）。
using StateView = evmone::state::StateView;
using BlockHashes = evmone::state::BlockHashes;
}  // namespace bcos::evmref
```

- [ ] **Step 4: 跑测试，观察真实断言红**

Run: `cmake --build bcos-evm-ref/build && ctest --test-dir bcos-evm-ref/build --output-on-failure 2>&1 | tail -20`
Expected: **3 红 2 绿**——`ContractDeletesListedAccount` / `ContractStorageZeroMeansErase` / `ErasesTouchedEmptyAccount` 断言失败（空转不删除/不写 storage）；`DeletesSameTxSelfdestruct` 绿（其断言验证的是 evmone 产出的 diff 内容，apply 断言对空转恰为平凡真——这正是缝契约测试必须存在的原因）；`Skeleton.HeadersAndLinkOk` 绿。

- [ ] **Step 5: 换真实现**

```cpp
// bcos-evm-ref/adapter/StateDiffWriteback.h
#pragma once

#include <test/utils/test_state.hpp>

namespace bcos::evmref
{
/// v1 写回缝：把 evmone StateDiff 应用到内存 TestState。
/// 契约（真账本写回实现必须满足，见 StateDiffWritebackTest 缝契约用例）：
///   1) deleted_accounts 必须删除（Cancun 后并非恒空：EIP-6780 同交易自毁、
///      EIP-161 空账户擦除都会产生删除项；state_diff.hpp 的"恒空"注释已过时）；
///   2) modified_storage 值为 0 表示删除槽（erase），不是存零；
///   3) code 仅在 has_value() 时覆盖。
inline void applyStateDiff(evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    state.apply(diff);
}
}  // namespace bcos::evmref
```

- [ ] **Step 6: 跑测试确认全绿**

Run: `cmake --build bcos-evm-ref/build && ctest --test-dir bcos-evm-ref/build --output-on-failure`
Expected: PASS（5 用例：Skeleton 1 + StateDiffWriteback 4）。

- [ ] **Step 7: Commit**

```bash
git add bcos-evm-ref/adapter/ bcos-evm-ref/test/
git commit -m "feat(bcos-evm-ref): M1 StateDiffWriteback with EIP-6780/EIP-161 deletion semantics tests"
```

---

### Task 4: M2 — `eth::runTransaction` / `runBlockFinalize`（TDD）

**Files:**
- Modify: `bcos-evm-ref/eth/EthTransition.h`（替换 Task 2 占位）
- Modify: `bcos-evm-ref/eth/EthTransition.cpp`
- Modify: `bcos-evm-ref/test/eth/EthTransitionTest.cpp`（替换恒真用例）

**Interfaces:**
- Consumes: `evmone::state::validate_transaction(view, block, tx, rev, int64_t, int64_t) -> variant<TransactionProperties, error_code>`；`state::transition(view, block, hashes, tx, rev, vm, props) -> TransactionReceipt`；`state::finalize(view, rev, coinbase, optional<uint64_t>, span<const Ommer>, span<const Withdrawal>) -> StateDiff`；Task 3 的 `bcos::evmref::applyStateDiff(TestState&, const StateDiff&)`（测试用）。
- Produces（Task 5 依赖，签名照抄勿改）:

```cpp
namespace bcos::evmref::eth {
using Result = std::variant<evmone::state::TransactionReceipt, std::error_code>;
Result runTransaction(const evmone::state::StateView&, const evmone::state::BlockInfo&,
    const evmone::state::BlockHashes&, const evmone::state::Transaction&,
    evmc_revision, evmc::VM&, int64_t blockGasLeft, int64_t blobGasLeft);
evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView&, evmc_revision,
    const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer>, std::span<const evmone::state::Withdrawal>);
}
```

设计要点（spec §4.2）：包装**不写回状态**（收 `const StateView&`，diff 由调用方经 `applyStateDiff` 写回）；`blobGasLeft` 与 `BlockInfo.blob_base_fee` 由调用方按 `BlobParams` 预计算。

- [ ] **Step 1: 重写 `EthTransitionTest.cpp` 为失败测试（21000 简单转账）**

```cpp
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>

using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

TEST(EthTransition, SimpleTransfer21000)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    const auto receiver = 0x0000000000000000000000000000000000001234_address;

    test::TestState state;
    state[sender] = {.nonce = 0, .balance = 1'000'000'000'000'000'000_u256};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.coinbase = 0x00000000000000000000000000000000c014ba5e_address;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = receiver;
    tx.value = 1000;
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432);

    ASSERT_TRUE(std::holds_alternative<state::TransactionReceipt>(res))
        << std::get<std::error_code>(res).message();
    const auto& receipt = std::get<state::TransactionReceipt>(res);
    EXPECT_EQ(receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(receipt.gas_used, 21000);

    // 包装不写回：state 尚未变化
    EXPECT_EQ(state.count(receiver), 0u);
    bcos::evmref::applyStateDiff(state, receipt.state_diff);
    EXPECT_EQ(state.at(receiver).balance, 1000);
}

TEST(EthTransition, InvalidTxRejectedWithoutSideEffect)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    test::TestState state;
    state[sender] = {.nonce = 0, .balance = 1_u256};  // 付不起 gas

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = sender;
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432);

    ASSERT_TRUE(std::holds_alternative<std::error_code>(res));
    EXPECT_EQ(state.at(sender).balance, 1);  // 状态不变
}

// 钉死 blockGasLeft/blobGasLeft 两个相邻 int64_t 形参不被换序：
// blobGasLeft=0 时 type-3 blob tx 必须被拒（换序后 786432 会放行它）。
// 同时是 spec §4.3 OP 路径 "blobGasLeft 内部传 0 拒 blob" 的前置契约。
TEST(EthTransition, BlobTxRejectedWhenNoBlobGasLeft)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    test::TestState state;
    state[sender] = {.nonce = 0, .balance = 1'000'000'000'000'000'000_u256};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.blob_base_fee = 1;

    state::Transaction tx;
    tx.type = state::Transaction::Type::blob;
    tx.sender = sender;
    tx.to = 0x0000000000000000000000000000000000001234_address;  // blob tx 必须有 to
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.max_blob_gas_price = 1;
    tx.blob_hashes = {
        0x0100000000000000000000000000000000000000000000000000000000000001_bytes32};
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, /*blobGasLeft=*/0);

    ASSERT_TRUE(std::holds_alternative<std::error_code>(res));
}

// runBlockFinalize 的 withdrawals 路径（EEST state 约定不触达，M2 内唯一覆盖点）：
// 金额按 gwei 计，落账须 ×1e9 换算为 wei。
TEST(EthTransition, FinalizeAppliesWithdrawalGweiToWei)
{
    const auto payee = 0x0000000000000000000000000000000000005e11_address;
    test::TestState state;

    const state::Withdrawal w{
        .index = 0, .validator_index = 0, .recipient = payee, .amount_in_gwei = 5};
    const auto diff = bcos::evmref::eth::runBlockFinalize(state, EVMC_CANCUN,
        0x00000000000000000000000000000000c014ba5e_address, std::nullopt, {},
        std::span{&w, 1});

    bcos::evmref::applyStateDiff(state, diff);
    EXPECT_EQ(state.at(payee).balance, 5'000'000'000_u256);  // 5 gwei = 5e9 wei
}
```

- [ ] **Step 2: 编译确认失败**

Run: `cmake --build bcos-evm-ref/build 2>&1 | tail -5`
Expected: FAIL — `no member named 'runTransaction' in namespace 'bcos::evmref::eth'`。

- [ ] **Step 3: 实现 `EthTransition.h` / `.cpp`**

```cpp
// bcos-evm-ref/eth/EthTransition.h
#pragma once

#include <evmc/evmc.hpp>
#include <test/state/state.hpp>
#include <span>
#include <variant>

namespace bcos::evmref::eth
{
using Result = std::variant<evmone::state::TransactionReceipt, std::error_code>;

/// validate -> transition（spec §4.2）。零 fee 逻辑：全部复用 evmone。
/// 不写回状态；调用方用 applyStateDiff(receipt.state_diff) 落账。
/// blobGasLeft 与 BlockInfo.blob_base_fee 由调用方按 BlobParams 预计算（spec §2）。
Result runTransaction(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, evmc_revision rev, evmc::VM& vm,
    int64_t blockGasLeft, int64_t blobGasLeft);

/// 块级收尾（withdrawals / ommer 奖励）。返回待写回的 diff。
evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view,
    evmc_revision rev, const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals);
}  // namespace bcos::evmref::eth
```

```cpp
// bcos-evm-ref/eth/EthTransition.cpp
#include <bcos-evm-ref/eth/EthTransition.h>

namespace bcos::evmref::eth
{
Result runTransaction(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, evmc_revision rev, evmc::VM& vm,
    int64_t blockGasLeft, int64_t blobGasLeft)
{
    const auto validated =
        evmone::state::validate_transaction(view, block, tx, rev, blockGasLeft, blobGasLeft);
    if (const auto* err = std::get_if<std::error_code>(&validated))
        return *err;
    return evmone::state::transition(view, block, hashes, tx, rev, vm,
        std::get<evmone::state::TransactionProperties>(validated));
}

evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view,
    evmc_revision rev, const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals)
{
    return evmone::state::finalize(view, rev, coinbase, blockReward, ommers, withdrawals);
}
}  // namespace bcos::evmref::eth
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build && ctest --test-dir bcos-evm-ref/build --output-on-failure`
Expected: PASS（8 用例：StateDiffWriteback 4 + EthTransition 4）。

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/eth/ bcos-evm-ref/test/
git commit -m "feat(bcos-evm-ref): M2 eth::runTransaction/runBlockFinalize thin wrappers"
```

---

### Task 5: M2 — EEST state fixture 对照 harness

**Files:**
- Create: `bcos-evm-ref/test/eth/EestStateTest.cpp`
- Modify: `bcos-evm-ref/test/CMakeLists.txt`（追加源文件）

**Interfaces:**
- Consumes: Task 4 的 `runTransaction`/`runBlockFinalize`（签名见 Task 4 Produces）；Task 3 的 `applyStateDiff`；`evmone::test::load_state_tests(std::istream&) -> std::vector<StateTransitionTest>`；`StateTransitionTest{name, pre_state, block_hashes, multi_tx, cases, input_labels, blob_schedule}`，`Case{rev, expectations, block}`，`Expectation{indexes, state_hash, logs_hash, exception}`；`test::get_blob_params(rev, blob_schedule) -> state::BlobParams`；`state::max_blob_gas_per_block(BlobParams)`；`state::mpt_hash(TestState)`；`test::logs_hash(vector<Log>)`。
- Produces: GTest 用例 `EestState.Fixtures`——遍历 `$EVM_REF_EEST_ROOT` 下 state fixtures，判据 stateRoot + logsHash 逐位（spec §6）。

参照：evmone `test/statetest/statetest_runner.cpp`（REF 上 73 行）——本 harness 是它的等价复刻，差异四条：走本模块 `runTransaction`/`applyStateDiff` 缝、过滤 `rev >= EVMC_CANCUN`、fixture 由目录遍历发现、skip 清单文件支持（上游 registrar 用 gtest_filter，此处用清单文件，能力等价）。`validate_state` 与上游一致保留。

- [ ] **Step 1: 选定并记录 EEST 版本 pin**

选定与 evmone REF `3585c2cb`（EIP 实现集 = v0.21.0：Cancun/Prague 完整、Osaka 进行中）配对的 EEST **stable** release tag，写入版本文件并入库：

```bash
# 例（执行时以 EEST releases 页面实际最新 stable 为准，Osaka 用例允许 skip）：
echo "eest-release: <选定的 tag，如 v4.x.0>" > bcos-evm-ref/test/EEST_VERSION
# fixtures 获取（本地或 CI 手动，tarball 缓存机制留 M6）：
# curl -L https://github.com/ethereum/execution-spec-tests/releases/download/<tag>/fixtures_stable.tar.gz | tar xz -C <目标目录>
# export EVM_REF_EEST_ROOT=<目标目录>/fixtures
```

README 与验收记录一律从 `EEST_VERSION` 文件引用，不手抄。

- [ ] **Step 2: 写 `bcos-evm-ref/test/eth/EestStateTest.cpp`**

```cpp
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/mpt_hash.hpp>
#include <test/utils/statetest.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using namespace evmone;

namespace
{
// EEST 根下 state fixtures 目录：兼容 <root>/state_tests 与 <root>/fixtures/state_tests
fs::path stateTestsDir(const fs::path& root)
{
    if (fs::exists(root / "state_tests"))
        return root / "state_tests";
    return root / "fixtures" / "state_tests";
}

// skip 清单：$EVM_REF_EEST_SKIP 指向的文本文件，每行一个相对 state_tests 目录的
// fixture 路径（# 开头为注释）。准入规则见 Task 6：Cancun/Prague 必须 0 skip，
// 仅 Osaka 允许且逐条记录原因。
std::set<std::string> loadSkipList()
{
    std::set<std::string> skip;
    if (const char* path = std::getenv("EVM_REF_EEST_SKIP"))
    {
        std::ifstream f{path};
        for (std::string line; std::getline(f, line);)
            if (!line.empty() && line[0] != '#')
                skip.insert(line);
    }
    return skip;
}

void runStateTest(const test::StateTransitionTest& t, evmc::VM& vm)
{
    SCOPED_TRACE(t.name);
    for (const auto& [rev, expectations, block] : t.cases)
    {
        if (rev < EVMC_CANCUN)  // spec §1.2: 目标 fork Cancun+
            continue;
        test::validate_state(t.pre_state, rev);  // 与上游 runner 一致的 pre-state 卫检
        for (size_t i = 0; i != expectations.size(); ++i)
        {
            SCOPED_TRACE(std::string{evmc::to_string(rev)} + '/' + std::to_string(i));
            const auto& expected = expectations[i];
            const auto tx = t.multi_tx.get(expected.indexes);
            auto state = t.pre_state;
            const auto blobParams = test::get_blob_params(rev, t.blob_schedule);

            const auto res = bcos::evmref::eth::runTransaction(state, block, t.block_hashes,
                tx, rev, vm, block.gas_limit,
                static_cast<int64_t>(state::max_blob_gas_per_block(blobParams)));

            if (std::holds_alternative<state::TransactionReceipt>(res))
            {
                bcos::evmref::applyStateDiff(
                    state, std::get<state::TransactionReceipt>(res).state_diff);
                // state test 约定：block reward 0 的最小块收尾
                bcos::evmref::applyStateDiff(state,
                    bcos::evmref::eth::runBlockFinalize(state, rev, block.coinbase, 0, {}, {}));
            }

            if (expected.exception)
            {
                ASSERT_FALSE(std::holds_alternative<state::TransactionReceipt>(res))
                    << "unexpected valid transaction";
                EXPECT_EQ(test::logs_hash(std::vector<state::Log>{}), expected.logs_hash);
            }
            else
            {
                ASSERT_TRUE(std::holds_alternative<state::TransactionReceipt>(res))
                    << "unexpected invalid transaction: "
                    << std::get<std::error_code>(res).message();
                EXPECT_EQ(test::logs_hash(std::get<state::TransactionReceipt>(res).logs),
                    expected.logs_hash);
            }
            EXPECT_EQ(state::mpt_hash(state), expected.state_hash);
        }
    }
}
}  // namespace

TEST(EestState, Fixtures)
{
    const char* root = std::getenv("EVM_REF_EEST_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "EVM_REF_EEST_ROOT not set";

    const auto dir = stateTestsDir(root);
    ASSERT_TRUE(fs::exists(dir)) << dir;
    const auto skip = loadSkipList();

    evmc::VM vm{evmc_create_evmone()};
    size_t files = 0;
    size_t skipped = 0;
    size_t failedFiles = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
    {
        if (entry.path().extension() != ".json" || entry.path().filename() == "index.json")
            continue;  // index.json 是 fixture 索引，不是测试（上游 registrar 同样排除）
        if (skip.count(fs::relative(entry.path(), dir).generic_string()) != 0)
        {
            ++skipped;
            continue;
        }
        ++files;
        SCOPED_TRACE(entry.path().string());
        const auto failuresBefore =
            ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
        std::ifstream f{entry.path()};
        try
        {
            for (const auto& t : test::load_state_tests(f))
                runStateTest(t, vm);
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "loader: " << e.what();
        }
        const auto failuresAfter =
            ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
        if (failuresAfter != failuresBefore)
            ++failedFiles;
    }
    std::clog << "EEST state: files=" << files << " skipped=" << skipped
              << " failed_files=" << failedFiles << "\n";
    EXPECT_GT(files, 0u);
}
```

- [ ] **Step 3: 追加到 `test/CMakeLists.txt`，构建**

`add_executable` 源列表追加 `eth/EestStateTest.cpp`。

Run: `cmake --build bcos-evm-ref/build`
Expected: 编译链接通过。

- [ ] **Step 4: 无 fixtures 时验证 SKIP 行为**

Run: `ctest --test-dir bcos-evm-ref/build --output-on-failure`
Expected: `EestState.Fixtures` 显示 SKIPPED（EVM_REF_EEST_ROOT not set），其余 PASS。

- [ ] **Step 5: 小子集冒烟**（取 pinned EEST release 里任一 Cancun 子目录先跑通判据）

```bash
export EVM_REF_EEST_ROOT=<按 Step 1 下载的 fixtures 根>
ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests --output-on-failure
```

Expected: `EestState.Fixtures` PASS，日志给出 `files=/skipped=/failed_files=` 计数。首个失败（若有）用 SCOPED_TRACE 中的 fixture 路径 + rev/case 号定位，对照 evmone statetest 二进制（获取方式见 Task 6 Step 3）区分 harness bug 与判据问题。

- [ ] **Step 6: Commit**

```bash
git add bcos-evm-ref/test/
git commit -m "test(bcos-evm-ref): EEST state fixture harness (stateRoot + logsHash, Cancun+, pinned EEST)"
```

---

### Task 6: M2 收口 — 全量 EEST state 运行 + 根构建挂接 + 记录

**Files:**
- Modify: `CMakeLists.txt`（仓库根，平铺 add_subdirectory 区域，现有 `add_subdirectory(bcos-evm)` 附近）
- Create: `bcos-evm-ref/README.md`

**Interfaces:**
- Consumes: Task 2–5 全部产物。
- Produces: 根构建 option `BCOS_EVM_REF`（默认 OFF）；M2 验收记录（EEST 通过率）写入 README。

- [ ] **Step 1: 根 `CMakeLists.txt` 挂接**（在 `add_subdirectory(bcos-evm)` 行后追加）

```cmake
option(BCOS_EVM_REF "Build bcos-evm-ref reference EVM module" OFF)
if(BCOS_EVM_REF)
    add_subdirectory(bcos-evm-ref)
endif()
```

- [ ] **Step 2: 验证根构建 configure 不回归**（不必全量编译，configure 即可）

```bash
cmake -B build-refcheck -S . -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake -DBCOS_EVM_REF=ON 2>&1 | tail -5
```

Expected: configure 成功（evmone 命中缓存）。完成后删除 `build-refcheck`。

- [ ] **Step 3: 全量 EEST state 运行并记录**

```bash
export EVM_REF_EEST_ROOT=<按 Task 5 Step 1 的 pinned fixtures 根>
ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests --output-on-failure | tee /tmp/eest-state-run.log
```

Expected: 目标 = 全绿（M2 验收判据，spec §7）。失败分诊：
- **分诊工具**：evmone statetest 二进制不在 vcpkg 安装内（port 刻意绕开该构建），自建一次：
  `cd /Users/octopus/octo/code/blockchain-impl/evmone && git switch --detach 3585c2cb && cmake -B build-st -DEVMONE_TESTING=ON && cmake --build build-st --target evmone-statetest`（顶层构建 hunter 会自动拉 GTest 等依赖；产物 `build-st/bin/evmone-statetest <fixture.json>`）。
- **仅本模块失败** = harness bug，必须修。
- **evmone 同样失败**：Cancun/Prague 用例出现此情况 = EEST tag 配对选错，回 Task 5 Step 1 换 tag 重跑，**不得入 skip 清单**；仅 **Osaka** 用例允许写入 skip 清单文件（`EVM_REF_EEST_SKIP`，相对路径每行一条 + `#` 注释记录原因），与 spec §6"Osaka 允许 skip 清单"对齐。

- [ ] **Step 4: 写 `bcos-evm-ref/README.md`**

```markdown
# bcos-evm-ref

Spec: `bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md` (rev.3)

复用 evmone::state（vcpkg overlay port，REF 3585c2cb）的标准 ETH/OpStack 参考模块。
与现有 bcos-evm/ 严格隔离（互不 include）。

## Build (standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build

## Test

    export EVM_REF_EEST_ROOT=<EEST fixtures root>   # 未设置则 EEST 用例 SKIP
    ctest --test-dir build --output-on-failure

## M2 验收记录

- EEST release: 见 `test/EEST_VERSION`（与 evmone REF 3585c2cb 配对；Task 5 Step 1 选定）
- state fixtures: <N> files, failed_files=<M>（harness 日志 `files=/skipped=/failed_files=` 原样粘贴）
- skip 清单: <EVM_REF_EEST_SKIP 文件内容，仅允许 Osaka 项，或"无">
```

（`<...>` 处从 Step 3 的运行日志粘贴实测数据——这是验收记录，不是模板占位。）

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt bcos-evm-ref/README.md
git commit -m "feat(bcos-evm-ref): wire into root build (BCOS_EVM_REF option) and record M2 EEST results"
```

---

## 后续 Plan（本 plan 范围外，按 spec §7 关键路径）

- **M3 plan**：ETH blockchain fixtures（区块头验证按 spec §1.3 裁剪移植 `blockchaintest_runner` 逻辑）。
- **M4+M5 plan**：OpStack Isthmus（OpForkSchedule/OpPredeploys 数据层 → OpHost/op_validate/op_transition/runDeposit/RollupCost），依赖本 plan 的 Task 4 接口。**rev.4：代码归属待决策点裁定**（本模块 vs bcos-evm/opstack/），故排在 M3.5 之后。
- **M3.5 plan**（rev.4 前置）：StateView 桥接真账本 spike → §7.2 go/no-go 评估报告 → 决策点（M4/M5 代码归属）。
- **M6 plan**：零值差分 CI 护栏 + op-geth t8n 离线向量 gate + upstream diff 脚本。

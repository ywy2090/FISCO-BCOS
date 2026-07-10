# bcos-evm-ref/opstack 缺陷修复（D-01–D-14）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消化审计台账 `bcos-evm-ref/docs/audits/2026-07-10-opstack-code-review-defect-ledger.md` 的全部 14 条已确认缺陷（🔴×11、🟡×1、🟢×2），使 `bcos-evm-ref/opstack/` 的 deposit 与配置路径与 op-geth 语义一致。

**Architecture:** 核心动作是把 `runDeposit` 从"手搓最小执行环"重建到与 `opTransition` 共享的执行核上（新文件 `OpExecCommon`，内容 = evmone baseline `transition()` 中段的照抄面）——这一步同时修 D-03/05/07/08/09 并消灭 D-14 的消息构造重复。validate 面具（D-01/02）、错误分类（D-04）、receipt 类型（D-06）、fork/host 配置（D-10/11/12）、清理（D-13/14）各自独立成 task。

**Tech Stack:** C++20 · evmone REF `3585c2cb`（v0.21.0 + SM3，`test/state` 库）· **GTest**（本模块测试框架——注意不是 bcos-evm 的 Boost.Test）· CMake（单 opstack 测试可执行文件 `bcos-evm-ref-opstack-tests`）。

## Global Constraints

- **op-geth v1.101702.2 是唯一 OP 正确性基准**（本地 `blockchain-impl/op-geth`）；`bcos-evm/opstack` 不构成依据（spec rev.8 D5）。
- **尽量复用 evmone**：照抄 baseline `transition()`（evmone `test/state/state.cpp:561-655`）的结构与顺序，不自创等价逻辑。
- evmone REF `3585c2cb`：**无** `gas_refund` 字段、无 EIP-7778；`TransactionProperties = {execution_gas_limit, min_gas_cost}`，`min_gas_cost` 即 EIP-7623 floor（`state.cpp:635-636` 先减 refund 再取 floor）。
- CREATE 地址派生约定：调用方**先** `++nonce`，evmone 用 `nonce-1` 计算（`test/state/host.cpp:239-240`，`assert(sender_acc.nonce != 0)`）。
- 测试用 **GTest**（`TEST(...)`/`EXPECT_*`），新用例并入既有文件或新文件加进 `test/CMakeLists.txt:15-30` 的可执行文件列表。
- 构建：`cd bcos-evm-ref && cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`；运行：`./build/test/bcos-evm-ref-opstack-tests`（可加 `--gtest_filter=`）。
- 忽略 FISCO 路径（用户裁定后续删除 FISCO 代码）。
- 每个 task 结束时**全部** opstack 测试保持绿色；频繁提交。
- 工作目录：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref`（下文相对路径以此为根；分支 `feat-evm-opstack-on-evmone`）。

## File Structure

| 文件 | 动作 | 职责 |
|------|------|------|
| `include/bcos-evm-ref/opstack/OpExecCommon.h` | 新建 | 共享执行核声明（`ExecOutcome` + `executeMessage`） |
| `opstack/OpExecCommon.cpp` | 新建 | 执行核实现：预热→消息+7702 委托→call→refund→floor（baseline 照抄面） |
| `opstack/OpTransition.cpp` | 改 | 删本地 `build_message`，中段改调执行核 |
| `opstack/OpDepositTx.cpp` | 改 | 重建 `runDeposit`：面具 validate、nonce 先递增、执行核、0x7E、bloom、错误分类 |
| `include/bcos-evm-ref/opstack/OpDepositTx.h` | 改 | `kDepositTxType`、`runDeposit` 增 `blockGasLeft` 形参 |
| `opstack/OpPrecompiles.cpp` + `.h` | 改 | 新增 `granitePrecompileOverrides()`（0x08 上限 112687） |
| `opstack/OpForkSchedule.cpp` | 改 | granite/holocene 接上表 |
| `opstack/OpHost.cpp` + `.h` | 改 | 覆写 `access_account`：override 表地址恒温 |
| `include/bcos-evm-ref/opstack/OpBlockFinalize.h` + `opstack/OpBlockFinalize.cpp` | 新建 | OP 块收尾；消费 `disable_prague_requests` |
| `opstack/OpValidate.cpp` + `.h`、`opstack/OpTransition.cpp` + `.h` | 改 | 删 `*FromState` 配对（D-13）；`OpTxProperties.flz_len` |
| `opstack/RollupCost.cpp` + `.h`、`opstack/OpReceiptMeta.cpp` + `.h` | 改 | `computeL1CostFromFlz` / `estimatedDaSizeFromFlz`；meta 以 flzLen 入参 |
| `test/opstack/OpDepositTest.cpp` 等 | 改/新建 | 每缺陷一个失败先行的用例；新建 `OpBlockFinalizeTest.cpp` |
| `CMakeLists.txt:22-33`、`test/CMakeLists.txt:15-30` | 改 | 新源文件/测试文件入列 |

---

### Task 1: 提取共享执行核 OpExecCommon（纯重构 + D-14 消息构造去重）

**Files:**
- Create: `include/bcos-evm-ref/opstack/OpExecCommon.h`
- Create: `opstack/OpExecCommon.cpp`
- Modify: `opstack/OpTransition.cpp:136-156`（删 `build_message`）、`:191-226`（改调执行核）
- Modify: `CMakeLists.txt:22-33`（源文件入列）

**Interfaces:**
- Consumes: `OpHost`（现有）、`evmone::state::State/Transaction`、`evmone::get_delegate_address`
- Produces: `struct ExecOutcome { evmc::Result result; int64_t gas_used; }`；
  `ExecOutcome executeMessage(evmone::state::State& state, OpHost& host, const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase, int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund)`——Task 2 的 `runDeposit` 依赖此签名。

- [ ] **Step 1: 写头文件**

```cpp
// include/bcos-evm-ref/opstack/OpExecCommon.h
#pragma once

#include <evmc/evmc.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
class OpHost;

struct ExecOutcome
{
    evmc::Result result;
    int64_t gas_used;  // EIP-3529 refund 与 EIP-7623 floor 已结算
};

/// baseline transition() 中段的照抄面（evmone test/state/state.cpp:600-636）：
/// 预热（sender/to/access_list/coinbase@Shanghai+）→ build_message + EIP-7702 委托解析 →
/// host.call → refund = min(delegation+result.gas_refund, used/quotient) → 7623 floor。
/// 前置条件：sender 已 get_or_insert 且 nonce 已递增（CREATE 地址派生用 nonce-1，
/// evmone host.cpp:239）。
ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);
}  // namespace bcos::evmref::opstack
```

- [ ] **Step 2: 写实现（内容 = 从 OpTransition.cpp:136-156 与 :193-226 原样搬移）**

```cpp
// opstack/OpExecCommon.cpp
#include <bcos-evm-ref/opstack/OpExecCommon.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <algorithm>
#include <cassert>
#include <evmone/delegation.hpp>

namespace bcos::evmref::opstack
{
namespace
{
evmc_message build_message(
    const evmone::state::Transaction& tx, int64_t execution_gas_limit) noexcept
{
    const auto recipient = tx.to.has_value() ? *tx.to : evmc::address{};

    return {
        .kind = tx.to.has_value() ? EVMC_CALL : EVMC_CREATE,
        .flags = 0,
        .depth = 0,
        .gas = execution_gas_limit,
        .recipient = recipient,
        .sender = tx.sender,
        .input_data = tx.data.data(),
        .input_size = tx.data.size(),
        .value = intx::be::store<evmc::uint256be>(tx.value),
        .create2_salt = {},
        .code_address = recipient,
        .code = nullptr,
        .code_size = 0,
    };
}
}  // namespace

ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund)
{
    state.get(tx.sender).access_status = EVMC_ACCESS_WARM;
    if (tx.to.has_value())
        host.access_account(*tx.to);
    for (const auto& [a, storage_keys] : tx.access_list)
    {
        host.access_account(a);
        for (const auto& key : storage_keys)
            state.get_storage(a, key).access_status = EVMC_ACCESS_WARM;
    }
    if (rev >= EVMC_SHANGHAI)
        host.access_account(coinbase);

    auto message = build_message(tx, execution_gas_limit);
    if (tx.to.has_value())
    {
        if (const auto delegate = evmone::get_delegate_address(host, *tx.to))
        {
            message.code_address = *delegate;
            message.flags |= EVMC_DELEGATED;
            host.access_account(message.code_address);
        }
    }

    auto result = host.call(message);

    auto gas_used = tx.gas_limit - result.gas_left;

    const auto max_refund_quotient = rev >= EVMC_LONDON ? 5 : 2;
    const auto refund_limit = gas_used / max_refund_quotient;
    const auto refund = std::min(delegation_refund + result.gas_refund, refund_limit);
    gas_used -= refund;
    assert(gas_used > 0);

    gas_used = std::max(gas_used, min_gas_cost);

    return {std::move(result), gas_used};
}
}  // namespace bcos::evmref::opstack
```

- [ ] **Step 3: OpTransition.cpp 改调执行核**

删 `opstack/OpTransition.cpp:136-156`（本地 `build_message`）与 `:8` 的 `#include <evmone/delegation.hpp>`（已移入核），头部加 `#include <bcos-evm-ref/opstack/OpExecCommon.h>`。将 `:193-226`（`sender_acc.access_status = ...` 至 `gas_used = std::max(...)`）整段替换为：

```cpp
    auto outcome = executeMessage(state, host, tx, rev, block.coinbase,
        props.props.execution_gas_limit, props.props.min_gas_cost, delegation_refund);
    const auto gas_used = outcome.gas_used;
```

其后 `:228-246` 的 receipt 构造中 `result.status_code` 改为 `outcome.result.status_code`。`OpHost host{...}` 一行保持在替换段之前不动。

- [ ] **Step 4: CMake 入列**

`CMakeLists.txt` 的 `add_library(bcos-evm-ref-opstack STATIC` 源列表（`:23-32`）加一行 `opstack/OpExecCommon.cpp`。

- [ ] **Step 5: 构建 + 全量测试（纯重构，必须全绿）**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（重构不改行为；`Op7702Test`/`OpTransitionTest`/`OpFloorGasTest` 是行为锚点）

- [ ] **Step 6: Commit**

```bash
rtk git add include/bcos-evm-ref/opstack/OpExecCommon.h opstack/OpExecCommon.cpp opstack/OpTransition.cpp CMakeLists.txt
rtk git commit -m "refactor(bcos-evm-ref): 提取共享执行核 OpExecCommon（D-14 消息构造去重）"
```

---

### Task 2: runDeposit 重建于执行核（D-03 refund / D-05 nonce / D-07 bloom / D-08 委托 / D-09 预热）

**Files:**
- Modify: `opstack/OpDepositTx.cpp`（整文件重写主体）
- Test: `test/opstack/OpDepositTest.cpp`（新增 5 个用例）

**Interfaces:**
- Consumes: Task 1 的 `executeMessage`（签名见 Task 1）
- Produces: `runDeposit` 行为变化——CREATE 地址=当前 nonce 派生、gasUsed 减 refund、logs bloom 非零、7702 委托解析、2929/3651 预热。签名此 task 不变。

- [ ] **Step 1: 写 5 个失败测试（追加到 `test/opstack/OpDepositTest.cpp`）**

文件头部补 include：`#include <test/state/host.hpp>`（`compute_create_address`）、`#include <test/state/bloom_filter.hpp>`。

```cpp
// D-05：合约创建型 deposit 部署地址 = compute_create_address(from, 执行前 nonce)
TEST(OpDeposit, CreateDepositDeploysAtCurrentNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = std::nullopt,                              // 合约创建
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = evmc::from_hex("00").value()};           // init code: STOP → 部署空码合约
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    const auto expected = evmone::state::compute_create_address(kFrom, 5);  // op-geth：当前 nonce
    ASSERT_EQ(ts.count(expected), 1u);
    EXPECT_EQ(ts.at(expected).nonce, 1u);                // EIP-161：新合约 nonce=1
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
}

// D-05：nonce=0 新账户创建型 deposit 不得崩溃（修复前 debug assert 中止）
TEST(OpDeposit, CreateDepositFromFreshAccount)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = std::nullopt,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = evmc::from_hex("00").value()};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.count(evmone::state::compute_create_address(kFrom, 0)), 1u);
}

// D-03：SSTORE 清零 refund 必须从 deposit gasUsed 扣除（op-geth Regolith+ calcRefund）
// gas 分解：intrinsic 21000 + PUSH1(3)+PUSH1(3)+SSTORE(2100冷+2900重置=5000) = 26006；
// refund = min(4800, 26006/5=5201) = 4800 → 21206；floor(空 calldata)=21000 → 21206。
TEST(OpDeposit, RefundLowersDepositGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kClear = 0x00000000000000000000000000000000000000ee_address;
    ts[kClear] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x00_bytes32, 0x01_bytes32}},
        .code = evmc::from_hex("600060005500").value()};  // PUSH1 0 PUSH1 0 SSTORE STOP
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kClear,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21206);
}

// D-07：有日志的 deposit receipt 必须携带非零 logs bloom
TEST(OpDeposit, DepositReceiptCarriesLogsBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kLogger = 0x00000000000000000000000000000000000000ef_address;
    ts[kLogger] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("60006000a000").value()};  // PUSH1 0 PUSH1 0 LOG0 STOP
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kLogger,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);

    ASSERT_EQ(r.receipt.logs.size(), 1u);
    const auto expected = evmone::state::compute_bloom_filter(r.receipt.logs);
    EXPECT_TRUE(evmc::bytes_view{r.receipt.logs_bloom_filter} == evmc::bytes_view{expected});
    EXPECT_FALSE(
        evmc::bytes_view{r.receipt.logs_bloom_filter} == evmc::bytes_view{evmone::state::BloomFilter{}});
}

// D-08：deposit 调用 7702 委托 EOA 必须执行委托目标代码（op-geth evm.Call 解析委托）
TEST(OpDeposit, DepositResolvesEip7702Delegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kImpl = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ab_address;
    ts[kImpl] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("600160005500").value()};  // PUSH1 1 PUSH1 0 SSTORE STOP
    ts[kEoa] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmc::bytes{kEoaDelegate.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kEoa,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    // 委托执行发生在 EOA 上下文：storage 写入 kEoa 名下 slot0=1
    EXPECT_EQ(ts.at(kEoa).storage.at(0x00_bytes32), 0x01_bytes32);
}

// D-09：sender 预热——BALANCE(ORIGIN) 收 warm 100 而非 cold 2600
// gas 分解：intrinsic 21000 + ORIGIN(2)+BALANCE(warm 100)+POP(2) = 21104（修复前 23604）。
TEST(OpDeposit, DepositWarmsSenderPerEip2929)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000ba_address;
    ts[kProbe] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("32315000").value()};      // ORIGIN BALANCE POP STOP
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kProbe,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21104);
}
```

`DepositResolvesEip7702Delegation` 中 `kEoaDelegate` 即 `kImpl`——直接写：
```cpp
        .code = evmc::from_hex("ef0100").value() + evmc::bytes{kImpl.bytes, 20}};
```
（删去 `kEoaDelegate` 名字，上面代码块以此行为准。）

- [ ] **Step 2: 跑新用例确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpDeposit.Create*:OpDeposit.Refund*:OpDeposit.DepositReceipt*:OpDeposit.DepositResolves*:OpDeposit.DepositWarms*'`
Expected: 6 个用例 FAIL（`CreateDepositFromFreshAccount` 在 Debug 构建下可能直接 assert 中止——同样算失败证据）

- [ ] **Step 3: 重写 `runDeposit` 主体**

`opstack/OpDepositTx.cpp` 整体替换为（本 task 保持 validate 传原始 `view` 与 gasLimit 预算不变——D-01/02/04 在 Task 3/4 处理）：

```cpp
#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpExecCommon.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <stdexcept>
#include <test/state/bloom_filter.hpp>

namespace bcos::evmref::opstack
{
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId)
{
    if (dep.is_system_tx)
        throw std::runtime_error("op deposit: is_system_tx not supported (block error)");

    evmone::state::State state{view};
    auto& fromAcc = state.get_or_insert(dep.from);
    const uint64_t preNonce = fromAcc.nonce;
    if (dep.mint.has_value())
        fromAcc.balance += *dep.mint;

    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::legacy;  // 内部执行壳；receipt 类型见 Task 4
    tx.sender = dep.from;
    tx.to = dep.to;
    tx.gas_limit = dep.gas_limit;
    tx.value = dep.value;
    tx.data = dep.data;
    tx.max_gas_price = 0;
    tx.max_priority_gas_price = 0;
    tx.nonce = preNonce;

    // Deposit 跳过 fee cap 校验；仅用 validate 求 intrinsic / EIP-7623 floor。
    evmone::state::BlockInfo validateBlock = block;
    validateBlock.base_fee = 0;
    const auto props = evmone::state::validate_transaction(
        view, validateBlock, tx, cfg.rev, block.gas_limit, 0);

    evmone::state::TransactionReceipt receipt;
    receipt.type = evmone::state::Transaction::Type::legacy;

    if (std::holds_alternative<std::error_code>(props))
    {
        // 处理级失败（op-geth Regolith）：mint 保留、nonce 强制递增、gasUsed=gasLimit。
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        ++state.get(dep.from).nonce;  // CREATE 地址派生要求先递增（evmone host.cpp:239）
        OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
        auto outcome = executeMessage(state, host, tx, cfg.rev, block.coinbase,
            p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
        receipt.status = outcome.result.status_code;
        receipt.gas_used = outcome.gas_used;
        receipt.logs = host.take_logs();
    }
    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);
    receipt.state_diff = state.build_diff(cfg.rev);
    return OpDepositReceipt{std::move(receipt), preNonce, 1};
}
}  // namespace bcos::evmref::opstack
```

要点（与旧实现的差异）：
- 删除 checkpoint/rollback——baseline `transition()` 不做 tx 级回滚，`Host::call` 失败时自行回滚执行效果；mint 与 nonce 在失败时保留，与 op-geth Regolith 一致。旧代码的 rollback+重设 nonce 终态相同但属多余动作。
- 执行失败分支消失：`executeMessage` 的 outcome 统一承载成功/失败（失败时 `Host::call` 已回滚、logs 为空、gasUsed 含 refund/floor 结算——op-geth Regolith+ 对 deposit 亦然）。

- [ ] **Step 4: 跑全量测试**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS。特别核对既有 `OpDeposit.EvmRevertKeepsMintAndChargesActualGas`（revert 路径 gasUsed 语义不变）与 `OpFloorGasTest`（floor 仍生效）。

- [ ] **Step 5: Commit**

```bash
rtk git add opstack/OpDepositTx.cpp test/opstack/OpDepositTest.cpp
rtk git commit -m "fix(bcos-evm-ref): runDeposit 重建于共享执行核（D-03/05/07/08/09）"
```

---

### Task 3: Deposit validate 面具（D-01 铸币后余额 / D-02 跳过 EIP-3607）

**Files:**
- Modify: `opstack/OpDepositTx.cpp`（新增匿名 ns 的 `DepositValidationView`；validate 改传面具；显式 CanTransfer）
- Test: `test/opstack/OpDepositTest.cpp`（新增 3 个用例）

**Interfaces:**
- Consumes: `evmone::state::StateView`（3 方法接口，`state_view.hpp`）
- Produces: 行为——桥接 deposit 成功、带码 sender 允许、value 超铸币后余额 → 失败 receipt 收 `max(intrinsic, floor)`。

- [ ] **Step 1: 写 3 个失败测试**

```cpp
// D-01：标准 L1→L2 桥接——from 余额 0，靠 mint 供资再转给收款人，必须成功
TEST(OpDeposit, BridgeDepositSpendsMintedValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = intx::uint256{100},
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{40});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

// D-02：带 code 的 sender（如 7702 委托 EOA）发 deposit，op-geth 跳过 EOA 检查
TEST(OpDeposit, SenderWithCodeIsAllowed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    constexpr auto kImpl = 0x00000000000000000000000000000000000000aa_address;
    ts[kFrom] = {.nonce = 3,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmc::bytes{kImpl.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{10},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
}

// D-01 补充：value 超过铸币后余额 → 执行级失败，收 max(intrinsic, floor)=21000 而非 gasLimit
//（op-geth：CanTransfer 失败是 vmerr → Regolith 失败 receipt，gas 只烧到 intrinsic+floor）
TEST(OpDeposit, ValueOverPostMintBalanceFailsWithIntrinsicGas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = intx::uint256{5},
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 21000);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{5});  // mint 保留，value 未动
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);                  // nonce 仍递增
}
```

- [ ] **Step 2: 跑新用例确认失败**

Run: `./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpDeposit.Bridge*:OpDeposit.SenderWithCode*:OpDeposit.ValueOver*'`（先构建）
Expected: 3 FAIL（前两个 status=FAILURE；第三个 gas_used=100000）

- [ ] **Step 3: 实现面具 + 显式 CanTransfer**

`opstack/OpDepositTx.cpp` 匿名 namespace 加：

```cpp
namespace
{
/// validate_transaction 的 view 面具：对 depositor 呈现「余额充足 + 空代码」，
/// 镜像 op-geth 对 deposit 跳过 preCheck（EOA/余额检查）。value 可负担性由
/// runDeposit 内显式 CanTransfer（对铸币后余额）核对——与 op-geth evm.Call 语义一致。
class DepositValidationView final : public evmone::state::StateView
{
public:
    DepositValidationView(const evmone::state::StateView& base, const evmc::address& sender) noexcept
      : m_base{base}, m_sender{sender}
    {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        auto acc = m_base.get_account(addr);
        if (addr != m_sender)
            return acc;
        if (!acc.has_value())
            acc.emplace();
        acc->balance = std::numeric_limits<intx::uint256>::max();  // 屏蔽 INSUFFICIENT_FUNDS
        acc->code_hash = evmone::state::Account::EMPTY_CODE_HASH;  // 屏蔽 EIP-3607
        return acc;
    }

    evmone::state::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        return addr == m_sender ? evmone::state::bytes{} : m_base.get_account_code(addr);
    }

    evmone::state::bytes32 get_storage(
        const evmc::address& addr, const evmone::state::bytes32& key) const noexcept override
    {
        return m_base.get_storage(addr, key);
    }

private:
    const evmone::state::StateView& m_base;
    evmc::address m_sender;
};
}  // namespace
```

文件头部补 `#include <limits>`、`#include <optional>`、`#include <test/state/state_view.hpp>`（若未经传递包含）。

`runDeposit` 中 validate 调用改为：

```cpp
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(
        maskedView, validateBlock, tx, cfg.rev, block.gas_limit, 0);
```

成功分支在 `++nonce` 之前插入显式 CanTransfer（op-geth `evm.Call`）：

```cpp
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        if (state.get(dep.from).balance < dep.value)
        {
            // op-geth：ErrInsufficientBalance 是 vmerr → Regolith 失败 receipt，
            // gas 消耗 = intrinsic（execution gas 未动），再抬 7623 floor。
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_FAILURE;
            receipt.gas_used =
                std::max(dep.gas_limit - p.execution_gas_limit, p.min_gas_cost);
        }
        else
        {
            ++state.get(dep.from).nonce;  // CREATE 地址派生要求先递增（evmone host.cpp:239）
            OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
            auto outcome = executeMessage(state, host, tx, cfg.rev, block.coinbase,
                p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
            receipt.status = outcome.result.status_code;
            receipt.gas_used = outcome.gas_used;
            receipt.logs = host.take_logs();
        }
```

（`else` 体即 Task 2 成功路径原文整体缩进一层。）文件头部需要 `#include <algorithm>`。

- [ ] **Step 4: 全量测试**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（既有 `EntryFailureChargesFullGasLimitButKeepsMint` 仍走 intrinsic 校验失败 → gasLimit，不受影响）

- [ ] **Step 5: Commit**

```bash
rtk git add opstack/OpDepositTx.cpp test/opstack/OpDepositTest.cpp
rtk git commit -m "fix(bcos-evm-ref): deposit validate 面具——铸币后余额+跳过 EIP-3607（D-01/D-02）"
```

---

### Task 4: 块级错误分类 + blockGasLeft 形参（D-04）+ receipt 0x7E（D-06）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpDepositTx.h`（`kDepositTxType`；`runDeposit` 增 `int64_t blockGasLeft` 末位形参）
- Modify: `opstack/OpDepositTx.cpp`
- Modify: `test/opstack/OpDepositTest.cpp`（既有 4 处调用补实参 + 2 个新用例）
- Modify: `test/opstack/OpBlockHarnessTest.cpp:134,162`、`test/opstack/OpFloorGasTest.cpp:107,120`（补实参）

**Interfaces:**
- Produces: `constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);`（OpDepositTx.h，公开——编码器/测试引用）；
  `runDeposit(view, block, hashes, dep, cfg, vm, chainId, blockGasLeft)`——`GAS_LIMIT_REACHED` 抛 `std::runtime_error`。

- [ ] **Step 1: 写 2 个失败测试**

```cpp
// D-04：deposit gasLimit 超块剩余 gas = 块级错误（op-geth ErrGasLimitReached 不转 receipt）
TEST(OpDeposit, GasLimitOverBlockBudgetIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 60000,
        .is_system_tx = false,
        .data = {}};
    EXPECT_THROW(
        runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, /*blockGasLeft=*/50000),
        std::runtime_error);
}

// D-06：deposit receipt 类型 = 0x7E
TEST(OpDeposit, DepositReceiptTypeIs7E)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{1},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(static_cast<uint8_t>(r.receipt.type), 0x7e);
    EXPECT_EQ(r.receipt.type, kDepositTxType);
}
```

- [ ] **Step 2: 编译确认失败（签名不存在 → 编译错）**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`
Expected: 编译 FAIL（`runDeposit` 8 参重载不存在、`kDepositTxType` 未定义）

- [ ] **Step 3: 实现**

`OpDepositTx.h`：
- 加 `constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);`（namespace 级、`OpDepositReceipt` 定义之前，附注释 `/// OP 0x7E deposit 交易/receipt 类型（EIP-2718 typed envelope 前缀）。`）
- `runDeposit` 声明加末位形参 `int64_t blockGasLeft`，doc 注释补一行：`/// gas_limit 超 blockGasLeft 抛 std::runtime_error（op-geth ErrGasLimitReached，块级错误）。`

`OpDepositTx.cpp`：
- 定义同步加形参；validate 调用的 `block.gas_limit` 实参改为 `blockGasLeft`；
- `receipt.type = evmone::state::Transaction::Type::legacy;` 改为 `receipt.type = kDepositTxType;`
- 失败分支前插入分类：

```cpp
    if (const auto* err = std::get_if<std::error_code>(&props))
    {
        if (*err == evmone::state::make_error_code(evmone::state::GAS_LIMIT_REACHED))
            throw std::runtime_error("op deposit: block gas limit reached (block error)");
        // 处理级失败（intrinsic 超 gasLimit 等，op-geth Regolith）：…原分支体不变…
    }
```

文件头部补 `#include <test/state/errors.hpp>`。

- 8 处既有调用补第 8 实参：`OpDepositTest.cpp` 既有 4 处与本 plan Task 2/3 新增用例全部补 `, 30000000`（等于 `blk().gas_limit`）；`OpBlockHarnessTest.cpp:134,162` 补 `, block.gas_limit`；`OpFloorGasTest.cpp:107,120` 补 `, block.gas_limit`。

- [ ] **Step 4: 全量测试**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

- [ ] **Step 5: Commit**

```bash
rtk git add include/bcos-evm-ref/opstack/OpDepositTx.h opstack/OpDepositTx.cpp test/opstack/OpDepositTest.cpp test/opstack/OpBlockHarnessTest.cpp test/opstack/OpFloorGasTest.cpp
rtk git commit -m "fix(bcos-evm-ref): GAS_LIMIT_REACHED 升块级错误 + deposit receipt 0x7E（D-04/D-06）"
```

---

### Task 5: Granite/Holocene 预编译上限表（D-11）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpPrecompiles.h:40`（加声明）
- Modify: `opstack/OpPrecompiles.cpp`（加表）
- Modify: `opstack/OpForkSchedule.cpp:36-54`（granite/holocene 接表）
- Test: `test/opstack/OpForkScheduleTest.cpp`

**Interfaces:**
- Produces: `const PrecompileOverrides& granitePrecompileOverrides() noexcept`——仅 0x08 限长 112687（rev=CANCUN 下 BLS/0x100 不存在，表中不得出现）。

- [ ] **Step 1: 写失败测试（追加到 `OpForkScheduleTest.cpp`）**

```cpp
// D-11：bn256Pairing 112687 输入上限由 Granite 硬分叉引入（op-geth bn256PairingGranite）
TEST(OpForkSchedule, GraniteAndHoloceneEnforceBn256PairingCap)
{
    for (const auto* cfg : {&graniteConfig(), &holoceneConfig()})
    {
        ASSERT_NE(cfg->precompiles, nullptr);
        const auto* bn256 = cfg->precompiles->find(evmc::address{0x08});
        ASSERT_NE(bn256, nullptr);
        EXPECT_EQ(bn256->max_input_size, 112687u);
        EXPECT_EQ(bn256->gas_cost_override, -1);
        // CANCUN 下 0x100 / BLS 不存在，表中不得出现
        EXPECT_FALSE(cfg->precompiles->contains(evmc::address{0x100}));
        EXPECT_FALSE(cfg->precompiles->contains(evmc::address{0x0c}));
    }
    // Ecotone/Fjord 早于 Granite：仍无上限表
    EXPECT_EQ(ecotoneConfig().precompiles, nullptr);
    EXPECT_EQ(fjordConfig().precompiles, nullptr);
}
```

- [ ] **Step 2: 跑确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpForkSchedule.GraniteAndHolocene*'`
Expected: FAIL（`precompiles == nullptr`）

- [ ] **Step 3: 实现**

`OpPrecompiles.h:40` 前加 `const PrecompileOverrides& granitePrecompileOverrides() noexcept;`。

`OpPrecompiles.cpp` 匿名 ns 加表 + 实现：

```cpp
constexpr PrecompileOverrides::Entry kGraniteEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
};
```
```cpp
const PrecompileOverrides& granitePrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kGraniteEntries};
    return overrides;
}
```

`OpForkSchedule.cpp:36-54` 两个 lambda 内各加 `c.precompiles = &granitePrecompileOverrides();`（文件头已 include OpPrecompiles.h）。

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add include/bcos-evm-ref/opstack/OpPrecompiles.h opstack/OpPrecompiles.cpp opstack/OpForkSchedule.cpp test/opstack/OpForkScheduleTest.cpp
rtk git commit -m "fix(bcos-evm-ref): Granite/Holocene bn256Pairing 112687 输入上限（D-11）"
```

---

### Task 6: OpHost::access_account 覆写——override 表地址恒温（D-12）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpHost.h:31`（加覆写声明）
- Modify: `opstack/OpHost.cpp`（加实现）
- Test: `test/opstack/OpHostTest.cpp`

**Interfaces:**
- Produces: `evmc_access_status OpHost::access_account(const evmc::address&) noexcept override`——表内地址返回 `EVMC_ACCESS_WARM` 且**不创建账户**。

- [ ] **Step 1: 写失败测试（追加到 `OpHostTest.cpp`，沿用该文件既有 fixture 的 State/OpHost 构造方式）**

```cpp
// D-12：0x100 P256VERIFY 在 Isthmus（rev=PRAGUE）必须预热（op-geth ActivePrecompiles 预热），
// 且不得因 access_account 产生幽灵空账户进入 state diff。
TEST(OpHost, OverrideTablePrecompilesAreWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    evmone::state::BlockInfo block;
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234,
        &isthmusPrecompileOverrides()};

    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    EXPECT_EQ(host.access_account(k100), EVMC_ACCESS_WARM);
    EXPECT_EQ(state.find(k100), nullptr);  // 无账户插入 → 不进 deleted_accounts

    // 表外普通地址语义不变：首访 COLD
    constexpr auto kPlain = 0x00000000000000000000000000000000000000cd_address;
    EXPECT_EQ(host.access_account(kPlain), EVMC_ACCESS_COLD);
}
```

- [ ] **Step 2: 跑确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpHost.OverrideTable*'`
Expected: FAIL（0x100 返回 COLD——evmone `is_precompile` 对 0x100 门槛为 OSAKA）

- [ ] **Step 3: 实现**

`OpHost.h:31` 后加：`evmc_access_status access_account(const evmc::address& addr) noexcept override;`，类 doc 注释补第 3 点：`/// 3. access_account：override 表地址恒温（op-geth 预热全部活跃 precompile；母本 is_precompile 对 0x100 门槛为 OSAKA），并避免母本 get_or_insert 产生的可擦除空账户。`

`OpHost.cpp` 加：

```cpp
evmc_access_status OpHost::access_account(const evmc::address& addr) noexcept
{
    if (m_overrides != nullptr && m_overrides->contains(addr))
        return EVMC_ACCESS_WARM;
    return evmone::state::Host::access_account(addr);
}
```

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（注意 `OpZeroDiffTest`——0x100 幽灵账户消失后零差断言只会更干净）

```bash
rtk git add include/bcos-evm-ref/opstack/OpHost.h opstack/OpHost.cpp test/opstack/OpHostTest.cpp
rtk git commit -m "fix(bcos-evm-ref): OpHost 覆写 access_account，override 表 precompile 恒温（D-12）"
```

---

### Task 7: finalizeOpBlock 消费 disable_prague_requests（D-10）

**Files:**
- Create: `include/bcos-evm-ref/opstack/OpBlockFinalize.h`
- Create: `opstack/OpBlockFinalize.cpp`
- Modify: `CMakeLists.txt:22-33`（源文件入列）
- Test: 新建 `test/opstack/OpBlockFinalizeTest.cpp` + `test/CMakeLists.txt:15-30` 入列

**Interfaces:**
- Consumes: `evmone::state::finalize(view, rev, coinbase, block_reward, ommers, withdrawals)`（与 `eth/EthTransition.cpp:22` 同一 API）
- Produces: `evmone::state::StateDiff finalizeOpBlock(const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)`——`disable_prague_requests` 为 false 时抛 `std::invalid_argument`（当前全部 OP fork 均为 true；该分支为配置一致性护栏）。

- [ ] **Step 1: 写失败测试**

```cpp
// test/opstack/OpBlockFinalizeTest.cpp
#include <bcos-evm-ref/opstack/OpBlockFinalize.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmc::literals;

// D-10：OP 块收尾不执行 EIP-6110/7002/7251 requests（op-geth OP 链无 execution-layer requests），
// disable_prague_requests 在此被真实消费。
TEST(OpBlockFinalize, IsthmusFinalizeProducesNoSystemSideEffects)
{
    evmone::test::TestState ts;
    constexpr auto kCoinbase = 0x0000000000000000000000000000000000000011_address;
    const auto diff = finalizeOpBlock(ts, isthmusConfig(), kCoinbase);
    EXPECT_TRUE(diff.modified_accounts.empty());
    EXPECT_TRUE(diff.deleted_accounts.empty());
}

TEST(OpBlockFinalize, PragueRequestsEnabledIsRejected)
{
    evmone::test::TestState ts;
    OpForkConfig cfg = isthmusConfig();
    cfg.disable_prague_requests = false;
    EXPECT_THROW(finalizeOpBlock(ts, cfg, evmc::address{}), std::invalid_argument);
}
```

`test/CMakeLists.txt:15-30` 列表加 `opstack/OpBlockFinalizeTest.cpp`。
（`StateDiff` 成员 `modified_accounts` / `deleted_accounts` 已对照 REF `state_diff.hpp:40-48` 核实。）

- [ ] **Step 2: 编译确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`
Expected: 编译 FAIL（头文件不存在）

- [ ] **Step 3: 实现**

```cpp
// include/bcos-evm-ref/opstack/OpBlockFinalize.h
#pragma once

#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// OP 区块收尾：withdrawals 恒空、无 ommers/块奖励；EIP-6110/7002/7251 requests 按
/// cfg.disable_prague_requests 抑制——op-geth 的 OP 链不产生 execution-layer requests，
/// 全部 OP fork 该开关恒为 true；false 抛 std::invalid_argument（配置护栏）。
evmone::state::StateDiff finalizeOpBlock(const evmone::state::StateView& view,
    const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evmref::opstack
```

```cpp
// opstack/OpBlockFinalize.cpp
#include <bcos-evm-ref/opstack/OpBlockFinalize.h>
#include <stdexcept>

namespace bcos::evmref::opstack
{
evmone::state::StateDiff finalizeOpBlock(const evmone::state::StateView& view,
    const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        throw std::invalid_argument("op finalize: prague requests unsupported on OP chains");
    return evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {});
}
}  // namespace bcos::evmref::opstack
```

`CMakeLists.txt` opstack 源列表加 `opstack/OpBlockFinalize.cpp`。

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add include/bcos-evm-ref/opstack/OpBlockFinalize.h opstack/OpBlockFinalize.cpp CMakeLists.txt test/CMakeLists.txt test/opstack/OpBlockFinalizeTest.cpp
rtk git commit -m "fix(bcos-evm-ref): finalizeOpBlock 消费 disable_prague_requests（D-10）"
```

---

### Task 8: 清理——删 *FromState 配对（D-13）+ FastLZ 单次计算（D-14）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpValidate.h:13-18,27-32`、`opstack/OpValidate.cpp:22-42`
- Modify: `include/bcos-evm-ref/opstack/OpTransition.h:21-26`、`opstack/OpTransition.cpp:253-260`
- Modify: `include/bcos-evm-ref/opstack/RollupCost.h`、`opstack/RollupCost.cpp:129-169`
- Modify: `include/bcos-evm-ref/opstack/OpReceiptMeta.h:38-40`、`opstack/OpReceiptMeta.cpp:8-35`
- Test: `test/opstack/OpBlockHarnessTest.cpp:186-200`、`test/opstack/RollupCostTest.cpp`、`test/opstack/OpReceiptMetaTest.cpp`

**Interfaces:**
- Produces:
  - `OpTxProperties` 增 `uint32_t flz_len = 0;`（Ecotone 路径为 0）
  - `intx::uint256 computeL1CostFromFlz(const OpFeeParams&, uint32_t flzLen, const OpForkConfig&) noexcept`（仅 Fjord+ 公式；`flzLen==0` 返 0）
  - `uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept`（`flzLen==0` 返 0）
  - `deriveOpReceiptMeta(cfg, fee, uint32_t flzLen, l1_cost, operator_fee_at_used, fill_operator_scalars)`（envelope 形参替换为 flzLen）
  - **删除** `opValidateFromState` / `opTransitionFromState`

- [ ] **Step 1: 写等价性失败测试（追加到 `RollupCostTest.cpp`）**

```cpp
// D-14：FastLZ 只压一次——分解 API 与原 API 等价
TEST(RollupCost, FromFlzVariantsMatchEnvelopeVariants)
{
    const auto env = evmc::from_hex("f8bb0a8402faf08083061a8094deadbeef00000000000000000000"
                                    "0000000000008405f5e100b8807b7b7b0000000000111111112222"
                                    "22223333333344444444555555556666666677777777888888889999"
                                    "9999aaaaaaaabbbbbbbbccccccccdddddddd").value();
    const auto flz = flzCompressLen(env);
    ASSERT_GT(flz, 0u);
    EXPECT_EQ(estimatedDaSizeFromFlz(flz), estimatedDaSize(env));
    EXPECT_EQ(estimatedDaSizeFromFlz(0), 0u);

    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000};
    fee.base_fee_scalar = 11;
    fee.blob_base_fee = intx::uint256{5};
    fee.blob_base_fee_scalar = 7;
    const auto& cfg = fjordConfig();
    EXPECT_EQ(computeL1CostFromFlz(fee, flz, cfg), computeL1Cost(fee, env, cfg));
    EXPECT_EQ(computeL1CostFromFlz(fee, 0, cfg), intx::uint256{0});
}
```

（`RollupCostTest.cpp` 头部如缺则补 `#include <bcos-evm-ref/opstack/OpForkSchedule.h>`。）

- [ ] **Step 2: 编译确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`
Expected: 编译 FAIL（新函数未声明）

- [ ] **Step 3: RollupCost 分解**

`RollupCost.h` 加（`computeL1Cost` 声明之后）：

```cpp
/// Fjord+ L1 fee 的 FastLZ 后半段（flzLen==0 返 0）；computeL1Cost 的非 Ecotone 路径委托至此。
intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept;

/// estimatedDaSize 的 FastLZ 后半段（flzLen==0 返 0）。
uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept;
```

`RollupCost.cpp`：`estimatedDaSize`（:129-135）与 `computeL1Cost`（:147-169）重构为：

```cpp
uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept
{
    if (flzLen == 0)
        return 0;
    return static_cast<uint64_t>(estimatedDaSizeScaled(flzLen) / intx::uint256{1'000'000});
}

uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept
{
    if (signedTxEnvelope.empty())
        return 0;
    return estimatedDaSizeFromFlz(flzCompressLen(signedTxEnvelope));
}

intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept
{
    (void)cfg;
    if (flzLen == 0)
        return intx::uint256{0};
    const auto calldataPerByte = params.l1_base_fee * intx::uint256{params.base_fee_scalar} *
                                 intx::uint256{kNonzeroByteCost};
    const auto blobPerByte = params.blob_base_fee * intx::uint256{params.blob_base_fee_scalar};
    const auto scaled = estimatedDaSizeScaled(flzLen);
    return scaled * (calldataPerByte + blobPerByte) / intx::uint256{kFjordDivisor};
}

intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept
{
    if (signedTxEnvelope.empty())
        return intx::uint256{0};

    if (cfg.has_ecotone_l1_formula)
    {
        const auto calldataPerByte = params.l1_base_fee * intx::uint256{params.base_fee_scalar} *
                                     intx::uint256{kNonzeroByteCost};
        const auto blobPerByte = params.blob_base_fee * intx::uint256{params.blob_base_fee_scalar};
        // op-geth newL1CostFuncEcotone:
        //   calldataGas*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/16e6
        const auto calldataGas = intx::uint256{bedrockCalldataGasUsed(signedTxEnvelope)};
        return calldataGas * (calldataPerByte + blobPerByte) / intx::uint256{16'000'000};
    }

    return computeL1CostFromFlz(params, flzCompressLen(signedTxEnvelope), cfg);
}
```

- [ ] **Step 4: OpValidate/OpTransition/OpReceiptMeta 接线**

`OpValidate.h:13-18` 的 `OpTxProperties` 加成员 `uint32_t flz_len = 0;  // Fjord+ 时的单次 FastLZ 结果；Ecotone 为 0`。

`OpValidate.cpp:22` 起改为：

```cpp
    uint32_t flzLen = 0;
    intx::uint256 l1Cost;
    if (cfg.has_ecotone_l1_formula)
    {
        l1Cost = computeL1Cost(fee, signedTxEnvelope, cfg);
    }
    else
    {
        flzLen = flzCompressLen(signedTxEnvelope);
        l1Cost = computeL1CostFromFlz(fee, flzLen, cfg);
    }
```

返回改：`return OpTxProperties{std::get<evmone::state::TransactionProperties>(base), l1Cost, opCost, flzLen};`

`OpReceiptMeta.h:38-40` 与 `.cpp:8-10`：形参 `evmc::bytes_view signedTxEnvelope` 改 `uint32_t flzLen`；`.cpp:32` 改 `m.da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar;`。

`OpTransition.cpp:248` 调用改：`auto meta = deriveOpReceiptMeta(cfg, fee, props.flz_len, props.l1_cost, opAtUsed, /*fill_operator_scalars=*/true);`

`OpReceiptMetaTest.cpp`：既有用例把 envelope 实参改为 `flzCompressLen(env)`（头部补 `#include <bcos-evm-ref/opstack/RollupCost.h>`）。

- [ ] **Step 5: 删 *FromState 配对（D-13）**

- 删 `OpValidate.h:27-32`、`OpValidate.cpp:36-42`、`OpTransition.h:21-26`、`OpTransition.cpp:253-260`。
- `OpBlockHarnessTest.cpp:186-200`（"FromState≡注入断言"段）：改为块级一次加载模式——

```cpp
    // OpFeeParams 为块级常量：每块 loadOpFeeParams 一次，注入式 API 逐 tx 复用。
    const auto fee = loadOpFeeParams(ts);
    const auto txV = opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, blockGasLeft);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(txV));
    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm,
        std::get<OpTxProperties>(txV), fee, 1234, {env.data(), env.size()});
```

（以该测试文件中既有注入式调用的实参顺序为准对齐；`opValidate` 形参顺序见 `OpValidate.h:22-25`，`opTransition` 见 `OpTransition.cpp:159-163`。原 `txRFS` 相关断言改为对 `txR` 的原有等值断言。）

- [ ] **Step 6: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add include/bcos-evm-ref/opstack opstack test/opstack
rtk git commit -m "cleanup(bcos-evm-ref): 删 *FromState 配对 + FastLZ 单次计算（D-13/D-14）"
```

---

### Task 9: 收尾——台账回填 + spec 状态 + 全量回归

**Files:**
- Modify: `docs/audits/2026-07-10-opstack-code-review-defect-ledger.md`（总览表加"状态"列）
- Modify: `../bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md`（§7.1 M5 行）

**Interfaces:** 无代码；文档一致性。

- [ ] **Step 1: 全量回归（含 eth 侧，防越界破坏）**

Run: `cmake --build build -j 8 && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS（含 `BcosEvmRefOpstackTests` 与 eth/adapter 各测试目标）

- [ ] **Step 2: 台账回填**

总览表加"状态"列：D-01–D-14 全部标 `✅ FIXED（<对应提交短哈希>）`；每条条目末尾加一行 `- **处置**：FIXED，<commit>（Task N）`。哈希以 `rtk git log --oneline -9` 实际输出为准。

- [ ] **Step 3: spec §7.1 M5 行更新**

"修复清单（…）"句尾追加：`——已全部修复（plan plans/2026-07-10-bcos-evm-ref-opstack-defect-fixes.md，2026-07-10）`。

- [ ] **Step 4: Commit**

```bash
rtk git add docs/audits/2026-07-10-opstack-code-review-defect-ledger.md ../bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md
rtk git commit -m "docs(bcos-evm-ref): 台账 D-01–D-14 回填 FIXED + spec M5 状态更新"
```

---

## 覆盖对照（台账 → task）

| 缺陷 | Task | 缺陷 | Task |
|------|------|------|------|
| D-01 铸币前余额 | 3 | D-08 7702 委托 | 2 |
| D-02 EIP-3607 | 3 | D-09 2929/3651 预热 | 2 |
| D-03 refund | 2 | D-10 死配置 | 7 |
| D-04 块级错误 | 4 | D-11 bn256 上限 | 5 |
| D-05 nonce/CREATE | 2 | D-12 0x100 预热 | 6 |
| D-06 0x7E | 4 | D-13 OpFeeParams 8 读 | 8 |
| D-07 logs bloom | 2 | D-14 重复构造/双压 | 1+8 |

## 已知风险与注意事项

1. **Task 2 的精确 gas 断言**（21206 / 21104）依据 Cancun/Prague gas 表推算；若实测差常数，先核对 SSTORE 冷/重置常量与 intrinsic 组成，**以 evmone 实际执行为准修断言注释**，不得反向改实现凑数。
2. **Task 3 面具的 nonce 语义**：`tx.nonce = preNonce` 与 base view 的 nonce 一致（mint 不动 nonce），validate 的 NONCE_* 检查天然通过；不需要在面具里动 nonce。
3. **Task 7 的 `StateDiff` 成员名**：断言字段名以 REF `state_diff.hpp` 实际定义为准。
4. **本 plan 不做的事**：deposit receipt 的块级 RLP/receipts-root 编码（spec 列为 M6 后可选项）；`OpDepositReceipt.deposit_nonce/version` 进 0x7E envelope 的编码同属其中。D-06 修的是 `receipt.type` 本身的类型标识。
5. 执行技能：worktree 已存在（`feat-evm-refactor`），无需再建。

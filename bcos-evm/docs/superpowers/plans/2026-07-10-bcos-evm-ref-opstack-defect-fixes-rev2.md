# bcos-evm-ref/opstack 缺陷修复 rev.2（台账 D-01–D-15 余量）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消化台账 `bcos-evm-ref/docs/audits/2026-07-10-opstack-code-review-defect-ledger.md`（D-01–D-15）在并行会话 P1 tx-alignment 落定后的**全部余量**：12 条未修缺陷（🔴×11 + 🟡×1）+ 2 条部分项收尾，并按红队审查补齐反作弊测试网。

**rev.2 相对 v1 的变化（v1 已冻结，见其文首 ⛔ 横幅）：**
1. **基线更新**为 P1 落定后的 HEAD（`e2abb6f45`）：D-05 已修（`2327532`，保留其实现与测试）；D-13 已缓解（fee 快照进 `OpTxProperties`，*FromState 保留——**rev.2 不再删除 *FromState**，其一致性理由成立）；`opTransition` 现为 9 参（`props.fee`）。
2. **Task 3 语义更正**（op-geth `state_transition.go:578/:486-513/:498`）：value 超铸币后余额是**共识层错误 → 失败 receipt 收满 gasLimit**，并入 validate 失败同款分支；v1 的"收 intrinsic"作废。
3. **Task 5 重写**（台账 **D-15**）：op-geth 自 **Fjord** 起 0x100 P256VERIFY 活跃（`contracts.go:193`，gas 3450 `params:183`）；bn256 112687 上限自 **Granite** 起。fork×precompile 矩阵：Ecotone=null、Fjord={0x100}、Granite/Holocene={0x08:112687, 0x100}、Isthmus/Jovian 现表不动。
4. **修正 v1 的 6 处代码级 bug**：保留 `delegation.hpp`/`OpForkSchedule.h` include；D-02 测试改用**非委托**代码（`ef0100` 前缀被 evmone `state.cpp:496 is_code_delegated` 天然豁免，v1 用例测不到东西）；`OpTxProperties` 聚合初始化含 `fee` 成员；同步修 `PreIsthmusConfigsPinned`；harness 不再改动（*FromState 保留）。
5. **并入红队 12 个反作弊用例**（coinbase 预热、Δ=2500 差分锚定、refund cap、revert 空 bloom、blockGasLeft 边界、委托→precompile、表外冷→暖、Jovian 恰上限、失败 create、无 mint 供资、联合供资、access-list 护栏）。
6. **风险条款收紧**：改断言数值必须同时提交差分锚定用例证明，禁止孤立改数。

**Architecture:** 与 v1 相同——`runDeposit` 重建到与 `opTransition` 共享的执行核 `OpExecCommon`（= evmone baseline `transition()` 中段照抄面，`state.cpp:600-637`），一步修 D-03/07/08/09 并消灭 D-14 的消息构造重复。

**Tech Stack:** C++20 · evmone REF `3585c2cb` · **GTest** · CMake 目标 `bcos-evm-ref-opstack-tests`。

## Global Constraints

- **op-geth v1.101702.2 是唯一 OP 正确性基准**（本地 `blockchain-impl/op-geth`，tag `e8800cffe`）。
- **尽量复用 evmone**：照抄 baseline `transition()`（`test/state/state.cpp:561-655`）结构与顺序。
- evmone REF `3585c2cb`：无 `gas_refund` 字段；`min_gas_cost` = EIP-7623 floor；refund→floor 顺序 `state.cpp:629-637`；CREATE 约定"调用方先 ++nonce、内部用 nonce-1"（`host.cpp:239-240`）。
- 测试 **GTest**；构建 `cd bcos-evm-ref && cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`；运行 `./build/test/bcos-evm-ref-opstack-tests`。
- 忽略 FISCO 路径。每 task 结束全绿；频繁提交。
- **断言数值纪律（rev.2 收紧）**：若实测 gas 与 plan 断言差常数，只许修注释与推导；**要改断言数值，必须同时提交差分锚定用例**（如 Task 2 的 `WarmColdDifferentialIs2500`）证明新数值，禁止孤立改数，禁止反向改实现凑数。
- 工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref`；分支 `feat-evm-opstack-on-evmone`；**基线 = `e2abb6f45`，执行前先确认 `rtk git status -- .` 干净**（若并行会话再动，停下重新对基线）。

## File Structure

| 文件 | 动作 | 职责 |
|------|------|------|
| `include/bcos-evm-ref/opstack/OpExecCommon.h` + `opstack/OpExecCommon.cpp` | 新建 | 共享执行核（`ExecOutcome` + `executeMessage`） |
| `opstack/OpTransition.cpp` | 改 | 删本地 `build_message`（:136-155），中段（:192-226）改调执行核；**保留 :8 `delegation.hpp` include** |
| `opstack/OpDepositTx.cpp` | 改 | 重建 `runDeposit`（保留既有 nonce-bump 修复）：面具 validate、显式 CanTransfer→gasLimit、执行核、0x7E、bloom、错误分类 |
| `include/bcos-evm-ref/opstack/OpDepositTx.h` | 改 | `kDepositTxType`；`runDeposit` 增 `int64_t blockGasLeft` 末参 |
| `opstack/OpPrecompiles.{h,cpp}` | 改 | 新增 `fjordPrecompileOverrides()`/`granitePrecompileOverrides()` |
| `opstack/OpForkSchedule.cpp` | 改 | fjord/granite/holocene 接表 |
| `opstack/OpHost.{h,cpp}` | 改 | 覆写 `access_account` |
| `include/bcos-evm-ref/opstack/OpBlockFinalize.h` + `opstack/OpBlockFinalize.cpp` | 新建 | OP 块收尾，消费 `disable_prague_requests` |
| `opstack/OpValidate.{h,cpp}`、`opstack/RollupCost.{h,cpp}`、`opstack/OpReceiptMeta.{h,cpp}`、`opstack/OpTransition.cpp:247` | 改 | `flz_len` 单次计算（D-14b） |
| `scripts/upstream-diff.sh` | 改 | 照抄面清单随 OpExecCommon 更新 |
| `test/opstack/*` | 改/新建 | 逐缺陷失败先行 + 红队 12 用例；新建 `OpBlockFinalizeTest.cpp` |
| `CMakeLists.txt:22-33`、`test/CMakeLists.txt` | 改 | 新文件入列 |

## 台账余量 → task 对照

| 缺陷 | 现状（基线 e2abb6f45） | Task |
|------|------|------|
| D-01 pre-mint 余额 / D-02 EIP-3607 | ❌ 未修 | 3 |
| D-03 refund / D-07 bloom / D-08 委托 / D-09 预热 | ❌ 未修 | 2 |
| D-04 块级错误 | ❌ 未修 | 4 |
| D-05 nonce/CREATE | ✅ 已修（`2327532`）——Task 2 保留其实现，重建时不回退 | —（Task 9 回填） |
| D-06 0x7E | ❌ 未修 | 2 |
| D-10 死配置 | ❌ 未修（读取点仍为零） | 7 |
| D-11 granite/holocene bn256 + **D-15 Fjord+ 0x100** | ❌ 未修 | 5 |
| D-12 0x100 预热 | ❌ 未修 | 6 |
| D-13 OpFeeParams 重读 | 🔶 已缓解（8→4 读/tx，fee 快照进 props）——**rev.2 判定接受余量，不立 task** | —（Task 9 回填理由） |
| D-14 消息构造重复 / FastLZ 双压 | 🔶 前者仅被 guardrail 跟踪；后者未动 | 1 + 8 |

---

### Task 1: 护栏测试 + 提取共享执行核 OpExecCommon（D-14a）

**Files:**
- Test: `test/opstack/OpTransitionTest.cpp`（先加护栏用例）
- Create: `include/bcos-evm-ref/opstack/OpExecCommon.h`、`opstack/OpExecCommon.cpp`
- Modify: `opstack/OpTransition.cpp`（删 :136-156 `build_message`（:156 为闭括号，manifest.tsv 同口径）；:192-226 改调核；**:8 的 `#include <evmone/delegation.hpp>` 保留**——`process_authorization_list` 仍用 `is_code_delegated`/:85 与 `DELEGATION_MAGIC`/:121）
- Modify: `CMakeLists.txt:22-33`、`scripts/upstream-diff.sh`（照抄面清单加 OpExecCommon.cpp）

**Interfaces:**
- Produces: `struct ExecOutcome { evmc::Result result; int64_t gas_used; }`；
  `ExecOutcome executeMessage(evmone::state::State& state, OpHost& host, const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase, int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund)`

- [ ] **Step 1: 写重构护栏测试（红队 F-3——现网对 access-list/coinbase 预热检测力为零，重构手滑无人抓）**

追加到 `test/opstack/OpTransitionTest.cpp`（沿用该文件既有 include 与 fixture 风格）：

```cpp
// 重构护栏：共享执行核不得丢 EIP-2930 access_list 预热。
// gas = 21000 + accessList(2400+1900) + PUSH1(3)+SLOAD(warm 100)+POP(2) = 25405；
// 预热被丢时 SLOAD 冷 2100 → 27405。
TEST(OpTransition, AccessListKeepsStorageWarm)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("6000545000").value()};  // PUSH1 0 SLOAD POP STOP
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.access_list = {{dest, {0x00_bytes32}}};

    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm,
        std::get<OpTxProperties>(v), 1234, {env.data(), env.size()});
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(txR.receipt.gas_used, 25405);
}
```

- [ ] **Step 2: 跑护栏用例，确认对当前实现 PASS（它钉的是现状，重构后必须仍绿）**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpTransition.AccessList*'`
Expected: PASS（若不是 25405，按「断言数值纪律」先核推导再动，不得直接改数）

- [ ] **Step 3: 写 OpExecCommon.h**

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

/// baseline transition() 中段的照抄面（evmone test/state/state.cpp:600-637）：
/// 预热（sender/to/access_list/coinbase@Shanghai+）→ build_message + EIP-7702 委托解析 →
/// host.call → refund = min(delegation+result.gas_refund, used/quotient) → 7623 floor。
/// 前置条件：sender 已 get_or_insert 且 nonce 已递增（CREATE 地址派生用 nonce-1，
/// evmone host.cpp:239）。
ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);
}  // namespace bcos::evmref::opstack
```

- [ ] **Step 4: 写 OpExecCommon.cpp（内容 = 从 OpTransition.cpp:136-155 与 :192-226 原样搬移）**

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

- [ ] **Step 5: OpTransition.cpp 改调核**

删 `:136-156` 本地 `build_message`（**保留 :8 delegation.hpp include**），头部加 `#include <bcos-evm-ref/opstack/OpExecCommon.h>`。`:192-226`（自 `sender_acc.access_status = ...` 至 `gas_used = std::max(...)`，`OpHost host{...}` 行保留在前）替换为：

```cpp
    auto outcome = executeMessage(state, host, tx, rev, block.coinbase,
        props.props.execution_gas_limit, props.props.min_gas_cost, delegation_refund);
    const auto gas_used = outcome.gas_used;
```

其后 receipt 构造中 `result.status_code` → `outcome.result.status_code`。

- [ ] **Step 6: CMake + guardrail 清单**

`CMakeLists.txt` opstack 源列表加 `opstack/OpExecCommon.cpp`；`scripts/upstream-diff/manifest.tsv`（按行号跟踪）三处更新：① `build_message` 行改指 OpExecCommon.cpp 的新行区间；② 新增中段（executeMessage 体）行区间条目；③ `transition_buy_gas OpTransition.cpp 174 184` 等其后条目的行号按删除 :136-156 后重算（整体上移 21 行）。

- [ ] **Step 7: 全量测试（纯重构必须全绿，护栏用例在内）+ Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add test/opstack/OpTransitionTest.cpp include/bcos-evm-ref/opstack/OpExecCommon.h opstack/OpExecCommon.cpp opstack/OpTransition.cpp CMakeLists.txt scripts/upstream-diff.sh
rtk git commit -m "refactor(evm-ref): 护栏用例先行，提取共享执行核 OpExecCommon（D-14a）"
```

---

### Task 2: runDeposit 重建于执行核（D-03/06/07/08/09；保留 D-05 既有修复）

**Files:**
- Modify: `opstack/OpDepositTx.cpp`（主体重写）、`include/bcos-evm-ref/opstack/OpDepositTx.h`（加 `kDepositTxType`）
- Test: `test/opstack/OpDepositTest.cpp`

**Interfaces:**
- Consumes: Task 1 `executeMessage`
- Produces: `constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);`（OpDepositTx.h）；`runDeposit` 签名此 task 不变（7 参）。

- [ ] **Step 1: 写失败测试（9 个新用例 + 1 个既有用例强化）**

`test/opstack/OpDepositTest.cpp` 头部补 `#include <test/state/bloom_filter.hpp>`（`compute_create_address` 所需的 `test/state/host.hpp` 若未经传递包含则一并补）。

```cpp
// D-03：SSTORE 清零 refund 从 deposit gasUsed 扣除（op-geth Regolith+ 无条件 calcRefund）
// intrinsic 21000 + PUSH1(3)+PUSH1(3)+SSTORE(2100冷+2900重置=5000) = 26006；
// refund = min(4800, 26006/5=5201) = 4800 → 21206；floor 21000 不抬。
TEST(OpDeposit, RefundLowersDepositGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kClear = 0x00000000000000000000000000000000000000ee_address;
    ts[kClear] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x00_bytes32, 0x01_bytes32}},
        .code = evmc::from_hex("600060005500").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kClear,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21206);
}

// D-03 反作弊（红队 F-4）：refund 受 EIP-3529 /5 上限约束。
// 4 槽清零：pre-refund = 21000 + 4*(3+3+5000) = 41024；cap = 41024/5 = 8204 → 32820。
// /2 或无上限作弊 → 21824，当场暴露。/5 结构性入断言。
TEST(OpDeposit, RefundIsCappedAtOneFifthOfGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kClear4 = 0x00000000000000000000000000000000000000e4_address;
    ts[kClear4] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x00_bytes32, 0x01_bytes32}, {0x01_bytes32, 0x01_bytes32},
            {0x02_bytes32, 0x01_bytes32}, {0x03_bytes32, 0x01_bytes32}},
        .code = evmc::from_hex("600060005560006001556000600255600060035500").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kClear4,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    constexpr int64_t kPreRefund = 41024;
    EXPECT_EQ(r.receipt.gas_used, kPreRefund - kPreRefund / 5);
}

// D-07：有日志的 deposit receipt 携带非零 bloom
TEST(OpDeposit, DepositReceiptCarriesLogsBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kLogger = 0x00000000000000000000000000000000000000ef_address;
    ts[kLogger] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("60006000a000").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kLogger,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    ASSERT_EQ(r.receipt.logs.size(), 1u);
    const auto expected = evmone::state::compute_bloom_filter(r.receipt.logs);
    EXPECT_TRUE(evmc::bytes_view{r.receipt.logs_bloom_filter} == evmc::bytes_view{expected});
    EXPECT_FALSE(evmc::bytes_view{r.receipt.logs_bloom_filter} ==
                 evmc::bytes_view{evmone::state::BloomFilter{}});
}

// D-07 反向（红队 F-5）：LOG 后 REVERT——logs 必须空、bloom 必须全零
TEST(OpDeposit, RevertedDepositHasEmptyLogsAndZeroBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kLogRevert = 0x00000000000000000000000000000000000000e5_address;
    ts[kLogRevert] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("60006000a060006000fd").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kLogRevert,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_REVERT);
    EXPECT_TRUE(r.receipt.logs.empty());
    EXPECT_TRUE(evmc::bytes_view{r.receipt.logs_bloom_filter} ==
                evmc::bytes_view{evmone::state::BloomFilter{}});
}

// D-08：deposit 调用 7702 委托 EOA 执行委托目标代码（storage 落在 EOA 上下文）
TEST(OpDeposit, DepositResolvesEip7702Delegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kImpl = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ab_address;
    ts[kImpl] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("600160005500").value()};
    ts[kEoa] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmone::state::bytes{kImpl.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kEoa,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kEoa).storage.at(0x00_bytes32), 0x01_bytes32);
}

// D-08 反作弊（红队 F-7）：委托指向 0x100——必须带 EVMC_DELEGATED 走空码回退，gas=21000；
// 未设旗的作弊实现派发 P256 override → 24450。
TEST(OpDeposit, DelegationToPrecompileFallsBackToEmptyCode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ac_address;
    ts[kEoa] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmone::state::bytes{k100.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kEoa,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21000);
}

// D-09：sender 预热——BALANCE(ORIGIN) 收 warm 100（修复前 cold 2600 → 23604）
TEST(OpDeposit, DepositWarmsSenderPerEip2929)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000ba_address;
    ts[kProbe] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("32315000").value()};  // ORIGIN BALANCE POP STOP
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kProbe,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21104);
}

// D-09 补强（红队 F-2）：EIP-3651 coinbase 预热——BALANCE(COINBASE) 同价 21104
TEST(OpDeposit, DepositWarmsCoinbasePerEip3651)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000bc_address;
    ts[kProbe] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("41315000").value()};  // COINBASE BALANCE POP STOP
    test::TestBlockHashes hashes;
    auto b = blk();
    b.coinbase = 0x00000000000000000000000000000000000000c1_address;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kProbe,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, b, hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21104);
}

// 差分锚定（红队 F-2，「断言数值纪律」的锚）：同形探针（PUSH20 目标 BALANCE POP STOP），
// 仅目标不同：sender（必暖）vs 表外冷地址。Δ = 2600-100 = 2500（EIP-2929 常数）。
// sender 未预热 → Δ=0；全体乱暖 → Δ=0；均被抓。
TEST(OpDeposit, WarmColdDifferentialIs2500)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    constexpr auto kCold = 0x00000000000000000000000000000000000000fe_address;
    const auto probeCode = [](const evmc::address& target) {
        return evmc::from_hex("73").value() + evmone::state::bytes{target.bytes, 20} +
               evmc::from_hex("315000").value();
    };
    const auto run = [&](const evmc::address& target) {
        test::TestState ts;
        ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
        constexpr auto kProbe = 0x00000000000000000000000000000000000000be_address;
        ts[kProbe] = {.nonce = 1, .balance = intx::uint256{0}, .code = probeCode(target)};
        test::TestBlockHashes hashes;
        DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kProbe,
            .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 100000,
            .is_system_tx = false, .data = {}};
        const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
        EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
        return r.receipt.gas_used;
    };
    EXPECT_EQ(run(kCold) - run(kFrom), 2500);
}
```

- [ ] **Step 2: 跑新用例确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpDeposit.*'`
Expected: 9 个新用例中 **8 个 FAIL**（`WarmColdDifferentialIs2500` 修复前 Δ=0）；**`RevertedDepositHasEmptyLogsAndZeroBloom` 现即 PASS**——`Host::call` 失败自回滚 logs（REF host.cpp:383/:397），它是防回归钉，不是红-绿证据；既有 5 个用例仍 PASS

- [ ] **Step 3: OpDepositTx.h 加类型常量 + D-06 失败断言**

`OpDepositReceipt` 定义前加：

```cpp
/// OP 0x7E deposit 交易/receipt 类型（EIP-2718 typed envelope 前缀）。
constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);
```

既有 `SuccessMintsAndAdvancesNonce` 加一行 D-06 断言（常量已存在，编译过、运行红——现实现 receipt.type=legacy）：

```cpp
    EXPECT_EQ(r.receipt.type, kDepositTxType);
```

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpDeposit.SuccessMints*'`
Expected: FAIL（type==legacy）

- [ ] **Step 4: 重写 runDeposit 主体**

`opstack/OpDepositTx.cpp` 整体替换为（保留既有 `:1-7` 的全部 include——含 `OpForkSchedule.h`——另加 `OpExecCommon.h` 与 `bloom_filter.hpp`；本 task 维持 validate 传原始 `view`，D-01/02 在 Task 3）：

```cpp
#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpExecCommon.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <cassert>
#include <stdexcept>
#include <test/state/bloom_filter.hpp>
#include <test/state/state.hpp>

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
    tx.type = evmone::state::Transaction::Type::legacy;  // 内部执行壳；receipt 用 kDepositTxType
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
    receipt.type = kDepositTxType;

    if (std::holds_alternative<std::error_code>(props))
    {
        // 处理级失败（op-geth Regolith，state_transition.go:486-513）：
        // mint 保留、nonce 强制递增、gasUsed = gasLimit 全额（:498）。
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        // Host::prepare_message 对 depth==0 消息不自行 bump nonce（母本假定调用方已 bump，
        // CREATE 地址派生用 nonce-1 取"执行前" nonce）——保留 2327532 的修复。
        assert(fromAcc.nonce < evmone::state::Account::NonceMax);
        ++fromAcc.nonce;
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

与旧实现的差异：删除 checkpoint/rollback（baseline 不做 tx 级回滚，`Host::call` 失败自回滚——REF `host.cpp:377-400`；mint 与 nonce 失败时保留，与 op-geth 一致，终态与旧代码相同）；执行成功/失败统一由 `executeMessage` 承载。

- [ ] **Step 5: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（重点：既有 `EvmRevertKeepsMintAndChargesActualGas`、`ContractCreationDerivesAddressFromPreExecutionNonce`、`OpFloorGas.DepositGasUsedRaisedToFloor` 不回退）

```bash
rtk git add include/bcos-evm-ref/opstack/OpDepositTx.h opstack/OpDepositTx.cpp test/opstack/OpDepositTest.cpp scripts/upstream-diff/manifest.tsv
rtk git commit -m "fix(evm-ref): runDeposit 重建于执行核（D-03/06/07/08/09）+ 红队反作弊用例"
```

（manifest.tsv：删除 `build_deposit_message OpDepositTx.cpp 13 33` 条目——该照抄段随重建消失，消息构造统一走 OpExecCommon。）

---

### Task 3: Deposit validate 面具 + 显式 CanTransfer（D-01/D-02）

**Files:**
- Modify: `opstack/OpDepositTx.cpp`
- Test: `test/opstack/OpDepositTest.cpp`

**Interfaces:** 行为——桥接 deposit 成功；带（非委托）码 sender 允许；value 超铸币后余额 → 失败 receipt 收**满 gasLimit**（rev.2 更正，op-geth `state_transition.go:578/:498`）。

- [ ] **Step 1: 写失败测试（4 个，含红队 F-1 两个反作弊）**

```cpp
// D-01：标准 L1→L2 桥接——from 余额 0，mint 供资再转给收款人，必须成功
TEST(OpDeposit, BridgeDepositSpendsMintedValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kTo,
        .mint = intx::uint256{100}, .value = intx::uint256{60}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{40});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

// D-01 反作弊（红队 F-1）：value 由既有余额供资（mint=nullopt）——
// 可支付性对象必须是铸币后余额，不是 mint 本身
TEST(OpDeposit, ValueFundedByPreexistingBalanceWithoutMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{100}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f2_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kTo,
        .mint = std::nullopt, .value = intx::uint256{60}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{40});
}

// D-01 反作弊（红队 F-1）：余额+mint 联合供资（value > mint 但 ≤ 铸币后余额）
TEST(OpDeposit, ValueFundedJointlyByBalanceAndMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{50}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f3_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kTo,
        .mint = intx::uint256{20}, .value = intx::uint256{60}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{10});
}

// D-02：带（非委托）code 的 sender 发 deposit——op-geth 对 deposit 跳过 EOA 检查。
// 注意不得用 ef0100 委托码：evmone state.cpp:496 对委托码本来就豁免，测不到面具。
TEST(OpDeposit, SenderWithCodeIsAllowed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 3, .balance = intx::uint256{0},
        .code = evmc::from_hex("00").value()};  // 任意非委托字节码（STOP）
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kFrom,
        .mint = intx::uint256{10}, .value = intx::uint256{0}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
}

// D-01 边界（rev.2 更正）：value 超铸币后余额 = op-geth 共识层错误
// （state_transition.go:578 clause 6）→ 失败 receipt 收满 gasLimit（:498），非 intrinsic。
TEST(OpDeposit, ValueOverPostMintBalanceFailsWithFullGasLimit)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kTo,
        .mint = intx::uint256{5}, .value = intx::uint256{60}, .gas_limit = 100000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 100000);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{5});  // mint 保留，value 未动
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}
```

- [ ] **Step 2: 跑新用例确认失败**

Run: `./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpDeposit.Bridge*:OpDeposit.ValueFunded*:OpDeposit.SenderWithCode*:OpDeposit.ValueOver*'`（先构建）
Expected: **3 个 FAIL**（Bridge / ValueFundedJointly / SenderWithCode——validate 用 pre-mint view 或 EIP-3607）；**2 个现即 PASS 的防回归钉**：`ValueFundedByPreexistingBalanceWithoutMint`（无 mint 时 pre/post 无差）、`ValueOverPostMintBalanceFailsWithFullGasLimit`（pre-mint 下 5<60 同进失败分支收 gasLimit）——记录实际结果

- [ ] **Step 3: 实现面具 + 显式 CanTransfer**

`opstack/OpDepositTx.cpp` 匿名 namespace 加（文件头部补 `#include <limits>`、`#include <optional>`）：

```cpp
namespace
{
/// validate_transaction 的 view 面具：对 depositor 呈现「余额充足 + 空代码」，
/// 镜像 op-geth 对 deposit 跳过 preCheck 的 EOA/余额检查。value 可支付性由
/// runDeposit 内显式检查（对铸币后余额，op-geth state_transition.go:578 clause 6）。
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

validate 调用改为：

```cpp
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(
        maskedView, validateBlock, tx, cfg.rev, block.gas_limit, 0);
```

成功分支开头（`const auto& p = ...` 之后、`++fromAcc.nonce` 之前）插入：

```cpp
        if (state.get(dep.from).balance < dep.value)
        {
            // op-geth clause 6（state_transition.go:578）：共识层错误 → failed-deposit
            // 分支（:486-513），gasUsed = gasLimit 全额（:498）。与 validate 失败同款。
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_FAILURE;
            receipt.gas_used = dep.gas_limit;
        }
        else
        {
            // Host::prepare_message 对 depth==0 消息不自行 bump nonce（母本假定调用方已
            // bump，CREATE 地址派生用 nonce-1 取"执行前" nonce）——保留 2327532 的修复。
            assert(fromAcc.nonce < evmone::state::Account::NonceMax);
            ++fromAcc.nonce;
            OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
            auto outcome = executeMessage(state, host, tx, cfg.rev, block.coinbase,
                p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
            receipt.status = outcome.result.status_code;
            receipt.gas_used = outcome.gas_used;
            receipt.logs = host.take_logs();
        }
```

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（`EntryFailureChargesFullGasLimitButKeepsMint` 是承重墙：20999 触发 INTRINSIC_GAS_TOO_LOW，仍走失败分支收 gasLimit——不得改动）

```bash
rtk git add opstack/OpDepositTx.cpp test/opstack/OpDepositTest.cpp
rtk git commit -m "fix(evm-ref): deposit validate 面具（铸币后余额+跳过 EIP-3607）+ CanTransfer 收 gasLimit（D-01/D-02）"
```

---

### Task 4: 块级错误分类 + blockGasLeft 形参（D-04）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpDepositTx.h`、`opstack/OpDepositTx.cpp`
- Test: `test/opstack/OpDepositTest.cpp`（新增 3 用例）+ 全部既有 `runDeposit` 调用处补末参（`rtk grep -rn "runDeposit(" test/ | wc -l` 先清点，OpDepositTest / OpBlockHarnessTest / OpFloorGasTest 及 Task 2/3 新增用例，逐处补 `, 30000000` 或 `, block.gas_limit`）

**Interfaces:**
- Produces: `runDeposit(view, block, hashes, dep, cfg, vm, chainId, int64_t blockGasLeft)`；`GAS_LIMIT_REACHED` 抛 `std::runtime_error`。

- [ ] **Step 1: 写失败测试**

```cpp
// D-04：deposit gasLimit 超块剩余 gas = 块级错误（op-geth 豁免名单恰两个：
// ErrSystemTxNotSupported 与 ErrGasLimitReached，state_transition.go:486）
TEST(OpDeposit, GasLimitOverBlockBudgetIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kFrom,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 60000,
        .is_system_tx = false, .data = {}};
    EXPECT_THROW(
        runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, /*blockGasLeft=*/50000),
        std::runtime_error);
}

// D-04 边界（红队 F-6）：恰等于块剩余 gas 必须接受——">=" 作弊在此暴露
TEST(OpDeposit, GasLimitExactlyBlockBudgetIsAccepted)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = kFrom,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 60000,
        .is_system_tx = false, .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234,
        /*blockGasLeft=*/60000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21000);
}

// D-04×D-05 交界（红队 F-11）：create 型 deposit intrinsic 失败——
// nonce 仍 +1、mint 保留、不得部署任何合约
TEST(OpDeposit, FailedCreateDepositStillBumpsNonceAndDeploysNothing)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32, .from = kFrom, .to = std::nullopt,
        .mint = intx::uint256{7}, .value = intx::uint256{0},
        .gas_limit = 21000,  // create intrinsic 53006（21000+32000+data4+initcode2）> 21000 → INTRINSIC_GAS_TOO_LOW
        .is_system_tx = false, .data = evmc::from_hex("00").value()};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 21000);  // 处理级失败收 gasLimit（此处恰 21000）
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{7});
    EXPECT_EQ(ts.count(evmone::state::compute_create_address(kFrom, 5)), 0u);
}
```

- [ ] **Step 2: 编译确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`
Expected: 编译 FAIL（8 参重载不存在）

- [ ] **Step 3: 实现**

- `OpDepositTx.h`：`runDeposit` 加末参 `int64_t blockGasLeft`，doc 注释补 `/// gas_limit 超 blockGasLeft 抛 std::runtime_error（op-geth ErrGasLimitReached，块级错误）。`
- `OpDepositTx.cpp`：定义同步；validate 的 `block.gas_limit` 实参改 `blockGasLeft`；失败分支改为（头部补 `#include <test/state/errors.hpp>`）：

```cpp
    if (const auto* err = std::get_if<std::error_code>(&props))
    {
        if (*err == evmone::state::make_error_code(evmone::state::GAS_LIMIT_REACHED))
            throw std::runtime_error("op deposit: block gas limit reached (block error)");
        // 处理级失败（op-geth Regolith，state_transition.go:486-513）：…原分支体不变…
    }
```

- 全部既有调用处补第 8 实参（`blk()` 场景用 `30000000`，有 `block` 变量的用 `block.gas_limit`）。

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add include/bcos-evm-ref/opstack/OpDepositTx.h opstack/OpDepositTx.cpp test/opstack/
rtk git commit -m "fix(evm-ref): GAS_LIMIT_REACHED 升块级错误 + blockGasLeft 形参（D-04）"
```

---

### Task 5: fork×precompile 矩阵（D-11 + D-15）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpPrecompiles.h`（加两个声明）、`opstack/OpPrecompiles.cpp`（加两张表）
- Modify: `opstack/OpForkSchedule.cpp`（fjord 接表；granite/holocene 派生处覆写 precompiles）
- Test: `test/opstack/OpForkScheduleTest.cpp`（新用例 + **同步修改 `PreIsthmusConfigsPinned`**（:42-61，现断言 granite/holocene `precompiles == nullptr`，与本修复直接冲突）；文件头部补 `#include <bcos-evm-ref/opstack/OpPrecompiles.h>`——现文件只前向声明，解引用 `precompiles->find` 会 incomplete type）
- Test: `test/opstack/OpHostTest.cpp`（红队 F-9 恰上限用例）

**Interfaces:**
- Produces: `fjordPrecompileOverrides()` = {0x100:3450}；`granitePrecompileOverrides()` = {0x08:cap112687, 0x100:3450}。**正确矩阵**（op-geth `contracts.go:193/:206/:220/:251`、`params:172/:183/:194`）：Ecotone=nullptr；Fjord={0x100}；Granite/Holocene={0x08+0x100}；Isthmus/Jovian 现表已对，不动。

- [ ] **Step 1: 写失败测试**

```cpp
// D-15：op-geth 自 Fjord 起 0x100 P256VERIFY 活跃（contracts.go:193，gas 3450 params:183）；
// D-11：bn256Pairing 112687 上限自 Granite 起（params:172，Holocene 沿用）
TEST(OpForkSchedule, FjordOnwardCarryP256VerifyAndGraniteCapsBn256)
{
    EXPECT_EQ(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于两者

    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        ASSERT_NE(cfg->precompiles, nullptr);
        const auto* p256 = cfg->precompiles->find(evmc::address{0x100});
        ASSERT_NE(p256, nullptr);
        EXPECT_EQ(p256->gas_cost_override, 3450);
    }
    EXPECT_FALSE(fjordConfig().precompiles->contains(evmc::address{0x08}));  // cap 是 Granite 的
    for (const auto* cfg : {&graniteConfig(), &holoceneConfig()})
    {
        const auto* bn256 = cfg->precompiles->find(evmc::address{0x08});
        ASSERT_NE(bn256, nullptr);
        EXPECT_EQ(bn256->max_input_size, 112687u);
        EXPECT_EQ(bn256->gas_cost_override, -1);
        EXPECT_FALSE(cfg->precompiles->contains(evmc::address{0x0c}));  // BLS 是 PRAGUE 的
    }
}
```

`test/opstack/OpHostTest.cpp` 加（沿用该文件 `makeBlock()`/`kSender` fixture，对照既有 `JovianBn256PairingInputOverLimitFails` :161 的构造方式）：

```cpp
// D-11 边界（红队 F-9）：恰在 81984（=427×192）上限的输入必须执行（全零点对 → pairing 成功）。
// 挡住 OpHost 限长比较符 > 被改成 >= 的回归。gas = 45000 + 427*34000 = 14,563,000。
TEST(OpHost, JovianBn256PairingInputAtLimitExecutes)
{
    constexpr auto kBn256 = 0x0000000000000000000000000000000000000008_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = intx::uint256{0}};  // prepare_message(depth==0) 会
                                                              // get(sender)，必须先入账
                                                              //（同 OpHostTest.cpp:139 先例）
    evmone::state::State st{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &jovianPrecompileOverrides()};

    std::vector<uint8_t> input(81984, 0x00);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kBn256;
    msg.code_address = kBn256;
    msg.sender = kSender;
    msg.gas = 15'000'000;
    msg.input_data = input.data();
    msg.input_size = input.size();
    const auto r = host.call(msg);
    EXPECT_EQ(r.status_code, EVMC_SUCCESS);
    EXPECT_GT(r.gas_left, 0);
}
```

同步修改 `PreIsthmusConfigsPinned`（:42-61）——**注意其真实结构**：`precompiles == nullptr` 是**单条循环内断言**，循环体覆盖 ecotone+fjord+granite+holocene 四个 config（并非 granite/holocene 各一条）。正确改法：把该断言从循环中拆出——**仅 ecotone** 保留 `EXPECT_EQ(ecotoneConfig().precompiles, nullptr)`；fjord/granite/holocene 改为循环外（或新循环）`EXPECT_NE(cfg->precompiles, nullptr)`（明细已由新用例覆盖）。其余循环内 pin（rev=CANCUN、operator/da/l1 flags、`disable_prague_requests`）保持不动。

- [ ] **Step 2: 跑确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpForkSchedule.FjordOnward*:OpHost.JovianBn256PairingInputAtLimit*'`
Expected: `FjordOnward*` FAIL（precompiles==nullptr）；`AtLimit*` 修复前即应 PASS（比较符现为 `>`，防回归钉）

- [ ] **Step 3: 实现**

`OpPrecompiles.h` 加声明（现有两个声明旁）：

```cpp
const PrecompileOverrides& fjordPrecompileOverrides() noexcept;
const PrecompileOverrides& granitePrecompileOverrides() noexcept;
```

`OpPrecompiles.cpp` 匿名 ns 加表 + 实现：

```cpp
// op-geth：0x100 P256VERIFY 自 Fjord 活跃（contracts.go:193）；bn256 112687 上限自 Granite
//（params:172）。Fjord 无 bn256 上限、无 BLS（CANCUN）。
constexpr PrecompileOverrides::Entry kFjordEntries[] = {
    {.addr = evmc::address{0x100}, .gas_cost_override = 3450, .max_input_size = 0},
};

constexpr PrecompileOverrides::Entry kGraniteEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = evmc::address{0x100}, .gas_cost_override = 3450, .max_input_size = 0},
};
```

```cpp
const PrecompileOverrides& fjordPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kFjordEntries};
    return overrides;
}

const PrecompileOverrides& granitePrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kGraniteEntries};
    return overrides;
}
```

`OpForkSchedule.cpp`：`fjordConfig()` 的 `.precompiles = nullptr` 改 `= &fjordPrecompileOverrides()`；granite/holocene 两个派生 lambda 内各加 `c.precompiles = &granitePrecompileOverrides();`。

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（`OpZeroDiffTest` 若引用 pre-Isthmus config 需复核其假设）

```bash
rtk git add include/bcos-evm-ref/opstack/OpPrecompiles.h opstack/OpPrecompiles.cpp opstack/OpForkSchedule.cpp test/opstack/OpForkScheduleTest.cpp test/opstack/OpHostTest.cpp
rtk git commit -m "fix(evm-ref): fork×precompile 矩阵——Fjord 起 0x100、Granite 起 bn256 上限（D-11/D-15）"
```

---

### Task 6: OpHost::access_account 覆写（D-12）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/OpHost.h`（类内加覆写声明；doc 注释补第 3 点）、`opstack/OpHost.cpp`
- Test: `test/opstack/OpHostTest.cpp`

**Interfaces:**
- Produces: `evmc_access_status OpHost::access_account(const evmc::address&) noexcept override`——override 表地址恒温（op-geth `statedb.Prepare` 预热 `ActivePrecompiles`，Isthmus 含 0x100，`contracts.go:344/:231`）且不插入账户；表外委托基类。基类可覆写已核实（REF `host.hpp:94-96` 为 `override` 的虚函数、public）。

- [ ] **Step 1: 写失败测试（含红队 F-8 反作弊）**

```cpp
// D-12：0x100 在 Isthmus（rev=PRAGUE）必须预热（evmone is_precompile 门槛 OSAKA），
// 且不得产生幽灵空账户进入 state diff 的 deleted_accounts。
TEST(OpHost, OverrideTablePrecompilesAreWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    EXPECT_EQ(host.access_account(k100), EVMC_ACCESS_WARM);
    EXPECT_EQ(state.find(k100), nullptr);
}

// D-12 反作弊（红队 F-8）：覆写不得吞掉基类冷→暖迁移（「不委托基类」的手滑在此暴露）
TEST(OpHost, OffTableAccessTransitionsColdToWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto kPlain = 0x00000000000000000000000000000000000000ce_address;
    EXPECT_EQ(host.access_account(kPlain), EVMC_ACCESS_COLD);
    EXPECT_EQ(host.access_account(kPlain), EVMC_ACCESS_WARM);
}
```

- [ ] **Step 2: 跑确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpHost.OverrideTable*:OpHost.OffTable*'`
Expected: `OverrideTable*` FAIL（0x100 返回 COLD + 账户被插入）；`OffTable*` PASS（防回归钉）

- [ ] **Step 3: 实现**

`OpHost.h` 类内 `call` 声明旁加：`evmc_access_status access_account(const evmc::address& addr) noexcept override;`

`OpHost.cpp` 加：

```cpp
evmc_access_status OpHost::access_account(const evmc::address& addr) noexcept
{
    // override 表内地址恒温：op-geth statedb.Prepare 预热全部活跃 precompile（Isthmus 含
    // 0x100，母本 is_precompile 对 0x100 门槛为 OSAKA）；early-return 同时避免母本
    // get_or_insert(erase_if_empty) 产生的幽灵空账户进入 state diff。
    if (m_overrides != nullptr && m_overrides->contains(addr))
        return EVMC_ACCESS_WARM;
    return evmone::state::Host::access_account(addr);
}
```

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS

```bash
rtk git add include/bcos-evm-ref/opstack/OpHost.h opstack/OpHost.cpp test/opstack/OpHostTest.cpp
rtk git commit -m "fix(evm-ref): OpHost 覆写 access_account，override 表 precompile 恒温（D-12）"
```

---

### Task 7: finalizeOpBlock 消费 disable_prague_requests（D-10）

**Files:**
- Create: `include/bcos-evm-ref/opstack/OpBlockFinalize.h`、`opstack/OpBlockFinalize.cpp`
- Modify: `CMakeLists.txt:22-33`；`test/CMakeLists.txt`（测试文件入列）
- Test: 新建 `test/opstack/OpBlockFinalizeTest.cpp`

**Interfaces:**
- Consumes: `evmone::state::finalize(view, rev, coinbase, block_reward, ommers, withdrawals)`（签名已对照 REF `state.hpp:136-138` 核实）
- Produces: `evmone::state::StateDiff finalizeOpBlock(const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)`

- [ ] **Step 1: 写失败测试**

```cpp
// test/opstack/OpBlockFinalizeTest.cpp
#include <bcos-evm-ref/opstack/OpBlockFinalize.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmc::literals;

// D-10：OP 块收尾不执行 EIP-6110/7002/7251 requests（op-geth core/state_processor.go:140-156
// 对 OP Isthmus 显式禁用）；disable_prague_requests 在此被真实消费。
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

`test/CMakeLists.txt` 源列表加 `opstack/OpBlockFinalizeTest.cpp`。

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
/// cfg.disable_prague_requests 抑制——op-geth 对 OP Isthmus 显式禁用
/// （state_processor.go:140-156），全部 OP fork 该开关恒为 true；false 抛
/// std::invalid_argument（配置护栏）。
/// 范围注记：op-geth 在 OP Isthmus 仍执行 EIP-4788/2935 **执行前**系统调用
/// （state_processor.go:90-95）——那是块级编排（§4.4）接入时的前置步骤，不在本收尾函数内。
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
rtk git commit -m "fix(evm-ref): finalizeOpBlock 消费 disable_prague_requests（D-10）"
```

---

### Task 8: FastLZ 单次计算（D-14b）

**Files:**
- Modify: `include/bcos-evm-ref/opstack/RollupCost.h`、`opstack/RollupCost.cpp:126-175`
- Modify: `include/bcos-evm-ref/opstack/OpValidate.h`（`OpTxProperties` 加 `flz_len`）、`opstack/OpValidate.cpp`
- Modify: `include/bcos-evm-ref/opstack/OpReceiptMeta.h:38-40`、`opstack/OpReceiptMeta.cpp`（envelope 形参 → `uint32_t flzLen`）
- Modify: `opstack/OpTransition.cpp:247`（deriveOpReceiptMeta 调用改传 `props.flz_len`）
- Test: `test/opstack/RollupCostTest.cpp`、`test/opstack/OpReceiptMetaTest.cpp`（既有用例 envelope 实参改 `flzCompressLen(env)`）

**Interfaces:**
- Produces:
  - `OpTxProperties` 尾部新增 `uint32_t flz_len = 0;`（**在 `fee` 成员之后**——现结构体已含 `fee`，聚合初始化顺序 `{props, l1_cost, operator_cost_at_gas_limit, fee, flz_len}`）
  - `intx::uint256 computeL1CostFromFlz(const OpFeeParams&, uint32_t flzLen, const OpForkConfig&) noexcept`
  - `uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept`
  - `deriveOpReceiptMeta(cfg, fee, uint32_t flzLen, l1_cost, operator_fee_at_used, fill_operator_scalars)`

- [ ] **Step 1: 写等价性失败测试（追加 `RollupCostTest.cpp`）**

```cpp
// D-14b：FastLZ 只压一次——分解 API 与原 API 等价
TEST(RollupCost, FromFlzVariantsMatchEnvelopeVariants)
{
    std::vector<uint8_t> envBytes(120);
    for (size_t i = 0; i < envBytes.size(); ++i)
        envBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    const evmc::bytes_view env{envBytes.data(), envBytes.size()};
    const auto flz = flzCompressLen(env);
    ASSERT_GT(flz, 0u);
    EXPECT_EQ(estimatedDaSizeFromFlz(flz), estimatedDaSize(env));
    EXPECT_EQ(estimatedDaSizeFromFlz(0), 0u);

    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000};
    fee.base_fee_scalar = 11;
    fee.blob_base_fee = intx::uint256{5};
    fee.blob_base_fee_scalar = 7;
    EXPECT_EQ(computeL1CostFromFlz(fee, flz, fjordConfig()), computeL1Cost(fee, env, fjordConfig()));
    EXPECT_EQ(computeL1CostFromFlz(fee, 0, fjordConfig()), intx::uint256{0});
}
```

- [ ] **Step 2: 编译确认失败** → Expected: 编译 FAIL（新函数未声明）

- [ ] **Step 3: RollupCost 分解**

`RollupCost.h` 加两个声明（对应注释：Fjord+ 后半段 / `flzLen==0` 返 0）。`RollupCost.cpp`：

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
    // op-geth Fjord+: estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar+blobBaseFee*blobScalar)/1e12
    const auto scaled = estimatedDaSizeScaled(flzLen);
    return scaled * (calldataPerByte + blobPerByte) / intx::uint256{kFjordDivisor};
}
```

`computeL1Cost` 保持对外签名：Ecotone 分支原样；非 Ecotone 分支改 `return computeL1CostFromFlz(params, flzCompressLen(signedTxEnvelope), cfg);`（空 envelope 早退保持）。

- [ ] **Step 4: 接线**

- `OpValidate.h` 的 `OpTxProperties` 在 `fee` 成员后加 `uint32_t flz_len = 0;  // Fjord+ 单次 FastLZ 结果；Ecotone 为 0`。
- `OpValidate.cpp`（现 `computeL1Cost` 调用处）改为：

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

返回改 `return OpTxProperties{std::get<evmone::state::TransactionProperties>(base), l1Cost, opCost, fee, flzLen};`（**注意 `fee` 在 `flzLen` 前**——按现结构体成员序）。
- `OpReceiptMeta.h/.cpp`：`signedTxEnvelope` 形参改 `uint32_t flzLen`；`.cpp` 的 da_footprint 行改 `m.da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar;`（头部 include RollupCost.h 已有）。
- `OpTransition.cpp:247` 调用改 `deriveOpReceiptMeta(cfg, props.fee, props.flz_len, props.l1_cost, opAtUsed, /*fill_operator_scalars=*/true)`。
- `OpReceiptMetaTest.cpp` 既有用例的 envelope 实参改为 `flzCompressLen(env)`（头部补 `#include <bcos-evm-ref/opstack/RollupCost.h>` 若缺）。

- [ ] **Step 5: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（Jovian 路径的 da_footprint 数值不变——同一 flz 只算一次）

```bash
rtk git add include/bcos-evm-ref/opstack opstack test/opstack/RollupCostTest.cpp test/opstack/OpReceiptMetaTest.cpp
rtk git commit -m "cleanup(evm-ref): FastLZ 单次计算贯穿 validate→receipt-meta（D-14b）"
```

---

### Task 9: 收尾——台账回填 + spec 状态 + 全量回归

**Files:**
- Modify: `docs/audits/2026-07-10-opstack-code-review-defect-ledger.md`
- Modify: `../bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md`（§7.1 M5 行；§3.2 照抄面清单——加 `OpExecCommon.cpp`，含照抄代码的文件由两个变三个）

**Interfaces:** 文档一致性；无代码。

- [ ] **Step 1: 全量回归（含 eth 侧）**

Run: `cmake --build build -j 8 && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS

- [ ] **Step 2: 台账回填（如实、不夸大）**

总览表加"状态"列，按下述口径逐条：
- D-01/02/03/04/06/07/08/09/11/12/14/15：`✅ FIXED（<本 plan 对应提交短哈希>，rev.2 Task N）`
- D-05：`✅ FIXED（2327532，并行会话 P1；rev.2 Task 2 保留并加固）`
- D-10：`🔶 已消费（finalizeOpBlock），接线待 §4.4 块级编排——当前无生产调用方，不构成闭环`（**不得标 FIXED**）
- D-13：`🔶 已缓解（255e71a fee 快照进 props，8→4 读/tx）；余量（块级一次加载）留待块级编排，接受`
- 附录 B 补一行：op-geth 在 OP Isthmus 仍执行 EIP-4788/2935 执行前系统调用（`state_processor.go:90-95`），属块级编排范围，本轮未实现。

- [ ] **Step 3: spec 更新**

- §7.1 M5 行修复清单句尾追加：`——rev.2 plan（plans/2026-07-10-bcos-evm-ref-opstack-defect-fixes-rev2.md）执行完毕：D-05 由并行会话先行修复，D-10/D-13 部分闭环（详见台账状态列），其余全部修复（2026-07-10）`。
- §3.2「含照抄代码的文件恰好两个」更新为三个（加 `OpExecCommon.cpp`，注明照抄源 `state.cpp:600-637`），§9 的 upstream-diff 护栏范围同步（scripts/upstream-diff.sh 已在 Task 1 更新）。

- [ ] **Step 4: Commit**

```bash
rtk git add docs/audits/2026-07-10-opstack-code-review-defect-ledger.md ../bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md
rtk git commit -m "docs(evm-ref): 台账 D-01–D-15 回填（含 D-10/D-13 部分闭环如实标注）+ spec M5/§3.2 更新"
```

---

## 覆盖对照（台账 → task）

| 缺陷 | Task | 缺陷 | Task |
|------|------|------|------|
| D-01/D-02 | 3 | D-09 | 2 |
| D-03 | 2 | D-10 | 7（部分闭环，如实标注） |
| D-04 | 4 | D-11 | 5 |
| D-05 | 已修（回填 Task 9） | D-12 | 6 |
| D-06 | 2 | D-13 | 已缓解（回填 Task 9） |
| D-07 | 2 | D-14 | 1 + 8 |
| D-08 | 2 | D-15 | 5 |

红队 12 用例落位：F-1×2→Task 3；F-2×2→Task 2（coinbase 探针 + Δ2500 差分）；F-3→Task 1；F-4→Task 2；F-5→Task 2；F-6→Task 4；F-7×1→Task 2（委托→precompile）；F-8→Task 6；F-9→Task 5；F-11→Task 4；另 2 个为缺陷主用例挂红队编号（sender 预热 21104、refund 21206）。

## 已知风险与注意事项

1. **断言数值纪律**见 Global Constraints——改数必须附差分锚定（`WarmColdDifferentialIs2500` 是锚），禁止孤立改数、禁止反向改实现凑数。
2. **基线**：执行前确认工作树干净且 HEAD 含 `e2abb6f45`；若并行会话再次活跃，停下重新对基线（v1 的教训）。
3. **D-08 的 `DelegationToPrecompileFallsBackToEmptyCode` 依赖 OpHost.cpp:114-116 的 EVMC_DELEGATED 回退**——若 Step 落地顺序中 Task 2 在 Task 6 之前（本 plan 即如此），该用例在 Task 2 时点即可判定（0x100 派发走 `call` 覆写，与 `access_account` 无关）。
4. **本 plan 不做**：deposit receipt 的块级 RLP/receipts-root 编码（M6 后可选项）；EIP-4788/2935 执行前系统调用（块级编排 §4.4 范围，已在 OpBlockFinalize.h 注明）；**本模块自己的 OP t8n 硬 gate**（spec rev.8.1 §7.1 M6 行的交付，不在本 plan）；`ReadAmplification.cpp` 的 `std::stoul`（台账附录 A 除名项，顺手修不入清单）。
5. **可选增强用例**（审查建议、非必需，实施者可顺手加）：value 超铸币后余额的 **create 变体**（op-geth clause 6 对 CREATE/CALL 同路径，`state_transition.go:574-580`）；**委托指向不存在目标**（空码即返 21000 + 无幽灵账户，op-geth `evm.go:315-316`）。
5. 执行技能：worktree 已存在，无需再建。

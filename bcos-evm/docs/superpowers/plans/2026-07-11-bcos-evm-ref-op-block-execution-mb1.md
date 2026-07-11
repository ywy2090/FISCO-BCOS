# M-B1 OP 块执行编排核心 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 实现 spec `2026-07-11-bcos-evm-ref-op-block-execution-design.md`（rev.2）的 M-B1：库级块执行入口 `processOpBlock`——pre-block 系统调用 → L1 attributes 首笔 → 逐笔账务 → 块尾 finalize（D-10 闭环），含自加严块结构校验（用户已裁定）。

**Architecture:** 新文件 `bcos-evm-ref/opstack/OpBlockExecute.{h,cpp}` 组装既有件（`system_call_block_start`/`runDeposit`/`opValidate`/`opTransition`/`finalizeOpBlock`），自身无照抄面（不入 manifest.tsv）。写回经调用方回调，state 管理沿"每 tx 重建 evmone State"现模式。

**Tech Stack:** C++20 · evmone REF `3585c2cb` · GTest · 目标 `bcos-evm-ref-opstack-tests`。

## Global Constraints

- **op-geth v1.101702.2 唯一基准**。本 plan 的行号引用已全部经 rev.2 spec 审查钉死，勿凭记忆另引。
- **普通 tx 的任何 validate 错误 = 块级错误**（op-geth 中普通 tx `preCheck` 失败 → `Process` 返回错误整块作废，`state_processor.go:109-113`——普通 tx 没有失败 receipt 机制）；deposit 的失败 receipt 机制已在 `runDeposit` 内。
- gas pool 语义：逐笔预算 = `block.gas_limit − Σ(已完成笔 gasUsed)`；`tx.gas_limit > 剩余` 即块级错误（与 op-geth SubGas/ReturnGas 净效应等价——rev.2 spec 审查项 3）。
- **系统调用静默跳过语义**：`system_call_block_start` 对目标账户无 code 时静默 continue（EIP-4788 规范，REF `system_contracts.cpp`）——预部署缺失不是错误；测试必须显式种 code 才能断言效果。
- 自加严校验（用户裁定）：deposit 整体在前、首笔 = attributes（`to==OP_L1_BLOCK && from==OP_DEPOSITOR`，常量已在 `OpPredeploys.h:15/:26`）。
- **spec §4.1 草图的一处细化**（本 plan 显式记录，非静默改写）：`OpBlockResult` 的 receipt 改为**单一有序序列** `std::vector<std::variant<...>>`——M-B2 的 receipts-root/块级 bloom 需要块内原始顺序，spec 草图的双 vector 丢序。Task 3 将此细化回写 spec。
- 断言数值纪律（差分锚定方可改数）；每 task 全绿；GTest；rtk git；工作树基线须干净。
- 工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref`（双名布局：源码在 `bcos-evm-ref/opstack/`）。

## File Structure

| 文件 | 动作 |
|---|---|
| `bcos-evm-ref/opstack/OpBlockExecute.h` + `.cpp` | 新建：`OpBlockTx`/`OpBlockResult`/`processOpBlock` |
| `test/opstack/OpBlockExecuteTest.cpp` | 新建 + `test/CMakeLists.txt` 入列 |
| `CMakeLists.txt` | opstack 源列表加一行 |
| `test/opstack/OpBlockHarnessTest.cpp` | Task 3 升级为调库 + `:119` 过时注释勘正 |
| `docs/audits/2026-07-10-opstack-code-review-defect-ledger.md` | Task 3 回填 D-10 ✅ |
| 主 spec §7.1 / 块执行 spec §4.1 | Task 3 状态与细化回写 |

---

### Task 1: processOpBlock 骨架——系统调用 + 结构校验 + deposit-only 路径

**Files:**
- Create: `bcos-evm-ref/opstack/OpBlockExecute.h`、`bcos-evm-ref/opstack/OpBlockExecute.cpp`
- Modify: `CMakeLists.txt`（源列表）、`test/CMakeLists.txt`（测试入列）
- Test: `test/opstack/OpBlockExecuteTest.cpp`

**Interfaces:**
- Consumes: `evmone::state::system_call_block_start`（REF 导出）、`runDeposit`（8 参）、`finalizeOpBlock`、`OP_L1_BLOCK`/`OP_DEPOSITOR`（OpPredeploys.h:15/:26）、`bcos::evmref::applyStateDiff`
- Produces（Task 2/3 与 M-B2 依赖）：

```cpp
// OpBlockExecute.h
#pragma once

#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <functional>
#include <span>
#include <variant>
#include <vector>

namespace bcos::evmref::opstack
{
/// 块内一笔交易：deposit 或普通 tx（普通 tx 须附签名 envelope 供 L1 fee 计算）。
struct OpBlockTx
{
    std::variant<DepositTx, evmone::state::Transaction> tx;
    evmc::bytes signedEnvelope;  // deposit 传空
};

/// 块执行结果。receipts 保持块内原始顺序（M-B2 的 receipts-root/块级 bloom 依赖此序；
/// cumulative_gas_used 已按混排顺序填好）。
struct OpBlockResult
{
    std::vector<std::variant<OpDepositReceipt, OpTxReceipt>> receipts;
    int64_t gasUsed = 0;                    // = 末笔 cumulative
    evmone::state::StateDiff finalizeDiff;  // 块尾 finalize 产出（已经 applyDiff 回调）
};

/// 执行整块（spec §4.1 顺序）：system_call_block_start → 首笔 L1 attributes deposit →
/// loadOpFeeParams → 逐笔（gas pool / cumulative / 逐笔写回）→ finalizeOpBlock。
/// 写回回调：每段 diff 产出后立即调用；下一步读的 view 必须已反映之。
/// 抛 std::runtime_error（块级错误）：txs 为空或首笔非 L1 attributes deposit
/// （to==OP_L1_BLOCK && from==OP_DEPOSITOR，自加严）；deposit 出现在非 deposit 之后
/// （自加严）；任一 tx gasLimit 超剩余块 gas；is_system_tx；普通 tx 任何 validate 错误
/// （op-geth 中普通 tx 无失败 receipt 机制，state_processor.go:109-113）。
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);
}  // namespace bcos::evmref::opstack
```

- [ ] **Step 1: 写失败测试（本 task 覆盖：throw 路径 ×4、deposit-only 块、系统调用接线 ×3、finalize 接线）**

```cpp
// test/opstack/OpBlockExecuteTest.cpp
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/opstack/OpBlockExecute.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/state/system_contracts.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 7;
    b.timestamp = 1'700'000'000;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = OP_SEQUENCER_FEE_VAULT;
    b.parent_beacon_block_root = 0xbe_bytes32;  // 4788 输入
    return b;
}

/// L1 attributes deposit（最小体：to=L1Block、from=DEPOSITOR；data 为 setter 调用，
/// 本测试用 harness 同款直写槽的 stub code——见 seedL1BlockStub）。
DepositTx attributesTx()
{
    return DepositTx{.source_hash = 0x01_bytes32,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1000000,
        .is_system_tx = false,
        .data = {}};
}

/// L1Block stub：空跑成功即可（槽值由测试直接预置，与 OpBlockHarnessTest 现行做法一致）。
void seedL1BlockStub(test::TestState& ts)
{
    ts[OP_L1_BLOCK] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("00").value()};  // STOP
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};
}

OpBlockTx wrap(DepositTx d) { return {.tx = std::move(d), .signedEnvelope = {}}; }
}  // namespace

// 自加严：空块 / 首笔非 attributes（普通 deposit、错误 from、错误 to）→ 块级错误
TEST(OpBlockExecute, RejectsEmptyOrBadFirstTx)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };

    EXPECT_THROW(processOpBlock(ts, blk(), hashes, {}, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);

    auto badFrom = attributesTx();
    badFrom.from = 0x00000000000000000000000000000000000000cc_address;
    std::vector<OpBlockTx> v1{wrap(badFrom)};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, v1, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);

    auto badTo = attributesTx();
    badTo.to = 0x00000000000000000000000000000000000000cc_address;
    std::vector<OpBlockTx> v2{wrap(badTo)};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, v2, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);
}

// deposit-only 块（sequencer 空块）：attributes 一笔即完整块；receipts=1、gasUsed=其 gasUsed、
// finalize diff 空（干净 state 上 finalizeOpBlock 无副作用——rev.2 Task 7 已钉）
TEST(OpBlockExecute, DepositOnlyBlockSucceeds)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 1u);
    const auto& dep = std::get<OpDepositReceipt>(r.receipts[0]);
    EXPECT_EQ(dep.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(dep.receipt.cumulative_gas_used, dep.receipt.gas_used);
    EXPECT_EQ(r.gasUsed, dep.receipt.gas_used);
    EXPECT_TRUE(r.finalizeDiff.modified_accounts.empty());
    EXPECT_EQ(ts.at(OP_DEPOSITOR).nonce, 1u);  // 写回生效
}

// 系统调用接线①：预部署有 code（测试替身：SSTORE(timestamp, calldata)）→ 槽被写。
// 替身语义仅测接线；真实 4788 合约行为由 M-B3 差分兜底（spec §4.3）。
// 替身 code: CALLDATALOAD(0) TIMESTAMP SSTORE STOP = 600035425500
TEST(OpBlockExecute, BlockStartSystemCallWritesBeaconSlot)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    ts[state::BEACON_ROOTS_ADDRESS] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("600035425500").value()};
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto b = blk();
    processOpBlock(ts, b, hashes, txs, isthmusConfig(), vm, 1234, apply);

    // 替身把 calldata(=parent_beacon_block_root) 存到 key=timestamp 的槽
    const auto key = intx::be::store<evmc::bytes32>(intx::uint256{b.timestamp});
    EXPECT_EQ(ts.at(state::BEACON_ROOTS_ADDRESS).storage.at(key), 0xbe_bytes32);
}

// 系统调用接线②：预部署无 code → 静默跳过（EIP-4788 规范语义），块照常成功
TEST(OpBlockExecute, MissingSystemContractIsSilentlySkipped)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);  // 不种 BEACON_ROOTS
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    EXPECT_NO_THROW(processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply));
    EXPECT_EQ(ts.count(state::BEACON_ROOTS_ADDRESS), 0u);
}

// 系统调用接线③（fork 门控负向断言）：Fjord（CANCUN）下 2935 不发生——
// HISTORY_STORAGE 种同款替身，Fjord 跑完其 storage 仍空；Isthmus（PRAGUE）下被写。
TEST(OpBlockExecute, HistoryStorageOnlyWrittenFromIsthmus)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto runWith = [&](const OpForkConfig& cfg) {
        test::TestState ts;
        seedL1BlockStub(ts);
        ts[state::HISTORY_STORAGE_ADDRESS] = {.nonce = 1, .balance = intx::uint256{0},
            .code = evmc::from_hex("600035425500").value()};
        const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };
        std::vector<OpBlockTx> txs{wrap(attributesTx())};
        processOpBlock(ts, blk(), hashes, txs, cfg, vm, 1234, apply);
        return ts.at(state::HISTORY_STORAGE_ADDRESS).storage.empty();
    };
    EXPECT_TRUE(runWith(fjordConfig()));    // CANCUN：2935 未激活
    EXPECT_FALSE(runWith(isthmusConfig())); // PRAGUE：被系统调用写入
}
```

`test/CMakeLists.txt` 入列 `opstack/OpBlockExecuteTest.cpp`。

- [ ] **Step 2: 编译确认失败**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests`
Expected: 编译 FAIL（OpBlockExecute.h 不存在）

- [ ] **Step 3: 实现（本 task 版：单笔 attributes + 系统调用 + finalize；逐笔循环骨架就位但普通 tx 分支 Task 2 填）**

```cpp
// bcos-evm-ref/opstack/OpBlockExecute.cpp
#include <bcos-evm-ref/opstack/OpBlockExecute.h>
#include <bcos-evm-ref/opstack/OpBlockFinalize.h>
#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <bcos-evm-ref/opstack/OpTransition.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <stdexcept>
#include <test/state/system_contracts.hpp>

namespace bcos::evmref::opstack
{
namespace
{
[[nodiscard]] bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    // 自加严（spec §6 决策点 2，用户裁定）：按内容校验。规范常量对照 op-node
    // derive/l1_block_info.go:40（DEPOSITOR）；op-geth EL 不做此校验（CL 层职责下沉）。
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}
}  // namespace

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // §4.1 步骤 1：pre-block 系统调用（4788/2935；rev 门控与无 code 静默跳过均在 evmone 内）。
    applyDiff(evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm));

    // §4.1 步骤 2 前置：首笔必须是 L1 attributes deposit（自加严，spec §6 决策点 1/2）。
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw std::runtime_error("op block: deposit after non-deposit tx");
            auto receipt = runDeposit(view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // §4.1 步骤 2：fee params 取自本块 attributes 执行后的槽值；惰性到首笔
                // 普通 tx 与 op-geth per-block 缓存等价（rollup_cost.go:162-164/:199-207）。
                fee = loadOpFeeParams(view);
                feeLoaded = true;
            }
            /* 普通 tx 分支：Task 2 实现（本 task 先 throw 占位，Task 1 测试不触达）。*/
            throw std::runtime_error("op block: non-deposit path lands in Task 2");
        }
    }

    // §4.1 步骤 4：块尾 finalize（D-10 接线闭环点）。
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiff(result.finalizeDiff);
    result.gasUsed = cumulative;
    return result;
}
}  // namespace bcos::evmref::opstack
```

`CMakeLists.txt` opstack 源列表加 `bcos-evm-ref/opstack/OpBlockExecute.cpp`。

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（既有 85 + 新 5 = 90）

```bash
rtk git add bcos-evm-ref/opstack/OpBlockExecute.h bcos-evm-ref/opstack/OpBlockExecute.cpp CMakeLists.txt test/CMakeLists.txt test/opstack/OpBlockExecuteTest.cpp
rtk git commit -m "feat(evm-ref): processOpBlock 骨架——系统调用/结构校验/deposit-only/finalize 接线（M-B1 Task 1）"
```

---

### Task 2: 逐笔循环完整——普通 tx 分支 + 混排账务

**Files:**
- Modify: `bcos-evm-ref/opstack/OpBlockExecute.cpp`（替换 Task 1 的占位 throw）
- Test: `test/opstack/OpBlockExecuteTest.cpp`（新增 4 用例）

**Interfaces:** 无签名变化；行为——普通 tx 经 `opValidate`（任何错误 → 块级）→ `opTransition`；cumulative 混排累计；gas pool 贯穿。

- [ ] **Step 1: 写失败测试**

普通 tx 的构造沿用 `OpBlockHarnessTest.cpp` 既有 boilerplate（sender 预热余额、`seedOpPredeploys(ts)`、eip1559 型 tx、50 字节 envelope、fee 槽预置——照抄其 `:140-190` 段的构造常量），以下只列断言骨架（实施者按该文件实际字段补全构造，构造值不得自创——与 harness 同值）：

```cpp
// 混排 cumulative：attributes + 1 deposit + 2 普通 tx，逐笔断言 cumulative 单调累计
TEST(OpBlockExecute, CumulativeGasAccumulatesAcrossMixedTxs)
{
    /* 构造：attributesTx + 普通 deposit(to=EOA, mint=1e9) + 普通 tx ×2（同 harness 构造）。
       断言：
       - receipts.size()==4，顺序与输入一致（variant 依次 Deposit/Deposit/Tx/Tx）
       - r1.cumulative == r0.gas + r1.gas；r2.cumulative == 前和 + r2.gas；……
       - result.gasUsed == r3.cumulative */
}

// gas pool 贯穿 + 恰等边界（块级化 rev.2 的 GasLimitExactly 口径）：
// 第二笔普通 tx 的 gasLimit 恰 == 剩余 → 接受；再多 1 → 整块 throw
TEST(OpBlockExecute, BlockGasPoolExactBoundary)
{
    /* 构造：block.gas_limit 收紧为 attributes.gasUsed + tx1.gasUsed + tx2.gasLimit 的精确和
      （先用宽限跑一遍取实测 gasUsed，再以实测值收紧——两段式，断言用差分而非魔数）。
       断言：恰等 → 全块成功；block.gas_limit 减 1 → EXPECT_THROW */
}

// 普通 tx validate 错误 = 块级（op-geth 无失败 receipt 机制，state_processor.go:109-113）：
// nonce 错配的普通 tx → 整块 throw（不是失败 receipt）
TEST(OpBlockExecute, InvalidNormalTxIsBlockError)
{
    /* 构造：合法 attributes + nonce=99 的普通 tx。断言 EXPECT_THROW(runtime_error) */
}

// 写回时序：第一笔普通 tx SSTORE，第二笔 SLOAD 读到——跨笔依赖经 applyDiff 生效
TEST(OpBlockExecute, LaterTxSeesEarlierTxWrites)
{
    /* 构造：tx1 调 SSTORE 合约写 slot0=1；tx2 调同合约 SLOAD(0) 并按值分支
      （code: 600054600057...，值为 0 时 REVERT、非 0 时 STOP）。
       断言：tx2 receipt SUCCESS（读到 1）；若写回时序破坏则 REVERT 当场暴露 */
}
```

- [ ] **Step 2: 跑确认失败**（前两个撞 Task 1 占位 throw；后两个同理）

- [ ] **Step 3: 实现普通 tx 分支（替换占位）**

```cpp
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                fee = loadOpFeeParams(view);
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // op-geth：普通 tx 校验失败无失败-receipt 机制，Process 直接整块作废
                // （state_transition preCheck → state_processor.go:109-113）。
                throw std::runtime_error(
                    "op block: invalid non-deposit tx: " + err->message());
            auto receipt = opTransition(
                view, block, hashes, tx, cfg, vm, std::get<OpTxProperties>(v), chainId, env);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
```

- [ ] **Step 4: 全量测试 + Commit**

Run: `cmake --build build -j 8 --target bcos-evm-ref-opstack-tests && ./build/test/bcos-evm-ref-opstack-tests`
Expected: 全部 PASS（90 + 4 = 94）

```bash
rtk git add bcos-evm-ref/opstack/OpBlockExecute.cpp test/opstack/OpBlockExecuteTest.cpp
rtk git commit -m "feat(evm-ref): processOpBlock 逐笔循环——混排账务/gas pool/块级错误/写回时序（M-B1 Task 2）"
```

---

### Task 3: harness 升级 + 文档闭环

**Files:**
- Modify: `test/opstack/OpBlockHarnessTest.cpp`（改调 `processOpBlock`；`:119` 过时注释勘正——evmone 实际导出 `system_call_block_start`）
- Modify: `docs/audits/2026-07-10-opstack-code-review-defect-ledger.md`（D-10 状态 🔶 → ✅，引接线 commit）
- Modify: 主 spec §7.1（M-B1 行标完成）、块执行 spec §4.1（`OpBlockResult` 有序序列细化回写 + M-B1 状态）

- [ ] **Step 1: harness 升级**——`OpBlockHarnessTest` 的手工顺序段（`:119-200` 区域：手工 runDeposit×2 + opValidate/opTransition + 手工写回）替换为一次 `processOpBlock` 调用，**原有全部数值断言保持不变**（fee 分账、vault 余额、gas 值——它们现在断言的是库入口而非手工序列，等值通过即证明库化无语义漂移）；`:119` 注释改为：`// pre-block 系统调用（EIP-4788/2935）由 processOpBlock 内部执行（evmone system_call_block_start，REF 已导出——旧注释"未导出"有误，2026-07-11 勘正）。`
- [ ] **Step 2: 全量测试**（Expected: 94 全 PASS，harness 断言零改动）
- [ ] **Step 3: 文档回填**——台账 D-10：`🔶` → `✅ FIXED（<Task 1 commit 短哈希> 接线 processOpBlock，rev.2 Task 7 消费 + M-B1 闭环）`，总览表与条目处置行同步；主 spec §7.1 加 M-B1 行 ✅；块执行 spec §4.1 的 `OpBlockResult` 草图更新为有序序列并注明"M-B1 plan 细化"。
- [ ] **Step 4: Commit**

```bash
rtk git add test/opstack/OpBlockHarnessTest.cpp docs/audits/2026-07-10-opstack-code-review-defect-ledger.md ../bcos-evm/docs/superpowers/specs/
rtk git commit -m "test+docs(evm-ref): harness 调库化 + D-10 闭环回填 + spec M-B1 状态（M-B1 Task 3）"
```

---

## 覆盖对照（spec §4.3 M-B1 单测清单 → task）

| spec 要求 | 用例 | Task |
|---|---|---|
| cumulative 混排累计 | CumulativeGasAccumulatesAcrossMixedTxs | 2 |
| gas pool 递减与恰等边界 | BlockGasPoolExactBoundary | 2 |
| deposit-only 块 | DepositOnlyBlockSucceeds | 1 |
| 4788 槽写入 | BlockStartSystemCallWritesBeaconSlot（替身，接线级；真合约行为归 M-B3） | 1 |
| Fjord 下 2935 不发生 | HistoryStorageOnlyWrittenFromIsthmus | 1 |
| 块结构违规/首笔非 attributes → throw | RejectsEmptyOrBadFirstTx（+Task 2 的 deposit-after-normal 场景并入 InvalidNormalTx 组） | 1/2 |
| 写回时序 | LaterTxSeesEarlierTxWrites | 2 |

## 已知风险与注意事项

1. **系统调用替身 vs 真合约**：Task 1 的 4788/2935 断言用测试替身（SSTORE 入参）只证接线与门控；真实合约字节码行为（ring buffer 等）**不在 M-B1 置信范围**，由 M-B3 块级差分兜底——台账/报告不得声称"4788 语义已验证"。
2. Task 2 的 `BlockGasPoolExactBoundary` 用两段式实测取值（先宽跑取 gasUsed 再收紧），**不写魔数**——符合断言数值纪律（实测值即锚）。
3. `deposit after non-deposit` 的 throw 在 Task 1 代码已实现（`seenNonDeposit` 检查），但可测路径要等 Task 2 普通 tx 分支就位——其用例放 Task 2。
4. harness 升级（Task 3）若发现任何数值断言失手——**这是库化引入语义漂移的信号**，停下分析，禁止改断言迁就。
5. `OpBlockHarnessTest` 中 `FromState≡` 相关段若与调库路径冗余，保留不删（它们钉的是 *FromState API 本身）。
6. **🔴 Task 2 执行前必须钉死的账务疑点**：op-geth `state_transition.go:360` 注释 "gas used by deposits may not be used by other txs"——提示 **deposit 对 gas pool 的净消耗可能是 gasLimit（不退未用）而非 gasUsed**，与普通 tx（退还未用）不同；而本 plan 当前对两类 tx 统一 `blockGasLeft -= gas_used`。执行 Task 2 前先对 op-geth 钉：deposit 路径 `returnGas`（`:667-673` Regolith / `:634` pre-Regolith）到底有没有把 `gasRemaining` 加回 `gp`——若没有，Task 2 的 deposit 分支改为 `blockGasLeft -= dep.gas_limit`（cumulative 仍用 gasUsed，两个口径分开），并补一条"deposit 未用 gas 不释放给后续 tx"的边界用例。**钉死结果写回本条**。

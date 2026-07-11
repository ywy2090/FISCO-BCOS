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
/// **弃写契约**：本函数任何 throw 之后，调用方必须弃掉本块内已经 applyDiff 的全部写集
/// （op-geth Process 报错时整个 statedb 被丢弃，state_processor.go:109-113 同语义）。
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

- [ ] **Step 1: 写失败测试（7 个 TEST：throw 路径（含首笔为普通 tx 变体）、deposit-only、系统调用接线 ×3、系统调用顺序探针、finalize 接线证明）**

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

    // 红队 F7：首笔是普通 tx（variant 另一臂）→ 同为块级错误。挡住"首笔若是 deposit
    // 才查 attributes、否则落普通分支"的实现变体。
    state::Transaction firstNormal;
    firstNormal.type = state::Transaction::Type::eip1559;
    firstNormal.sender = 0x00000000000000000000000000000000000000aa_address;
    firstNormal.to = 0x00000000000000000000000000000000000000bb_address;
    firstNormal.gas_limit = 100000;
    firstNormal.max_gas_price = 1000;
    std::vector<uint8_t> env(50, 0x11);
    std::vector<OpBlockTx> v3{
        OpBlockTx{.tx = firstNormal, .signedEnvelope = evmc::bytes{env.begin(), env.end()}}};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, v3, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);
}

// 红队 F2：系统调用**顺序**探针（先写后抄）。BEACON_ROOTS 替身按 CALLER 分支：
// SYSTEM_ADDRESS(0xff..fe) 调用时 SSTORE(0,1)；其他调用者把 SLOAD(0) 现值抄进 slot1。
// attributes 替身 CALL BEACON_ROOTS。系统调用先于 attributes（且其 diff 已写回）
// → slot1==1；实现若把系统调用挪到 tx 之后或不写回其 diff → slot1==0 → 翻红。
TEST(OpBlockExecute, SystemCallRunsBeforeAttributesTx)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    ts[state::BEACON_ROOTS_ADDRESS] = {.nonce = 1, .balance = intx::uint256{0},
        .code = evmc::from_hex("3373fffffffffffffffffffffffffffffffffffffffe"
                               "14602157600054600155005b600160005500").value()};
    // attributes 替身：CALL(gas=剩余, BEACON_ROOTS, 0,0,0,0,0) POP STOP
    ts[OP_L1_BLOCK].code = evmc::from_hex(
        "6000600060006000600073000f3df6d732807ef1319fb7b8bb8522d0beac025af15000").value();
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
    ASSERT_EQ(std::get<OpDepositReceipt>(r.receipts[0]).receipt.status, EVMC_SUCCESS);

    evmc::bytes32 slot1{};
    slot1.bytes[31] = 1;
    evmc::bytes32 one{};
    one.bytes[31] = 1;
    EXPECT_EQ(ts.at(state::BEACON_ROOTS_ADDRESS).storage.at(slot1), one);
}

// 红队 F3：finalize **被调**的证明——借 finalizeOpBlock 的护栏异常
// （disable_prague_requests=false → std::invalid_argument，OpBlockFinalize.cpp）。
// "不调 finalize"的作弊实现无从抛出 → 翻红。异常型 invalid_argument（logic_error 系）
// 与结构校验的 runtime_error 异族，不会误绿。
// 局限（D-10 回填措辞按此降级）：证明"被调且 tx 全跑完后可达"，不证明其 diff 被
// applyDiff、也不证明发生在末笔之后——OP 下 finalize diff 恒空，原理上不可观测。
TEST(OpBlockExecute, FinalizeIsActuallyWired)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evmref::applyStateDiff(ts, d); };
    OpForkConfig cfg = isthmusConfig();
    cfg.disable_prague_requests = false;
    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, txs, cfg, vm, 1234, apply),
        std::invalid_argument);
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
Expected: 全部 PASS（既有 85 + 新 7 = 92）

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

- [ ] **Step 1: 写失败测试（7 个 TEST；红队 F8 纪律：断言语句必须逐条落码，勾框前 `grep -c "EXPECT_\|ASSERT_" test/opstack/OpBlockExecuteTest.cpp` 对照本骨架断言计数）**

**构造事实（基线审查钉定，不得自创值）**：普通 tx 构造照抄 `OpBlockHarnessTest.cpp:172-181`（eip1559、gas_limit=100000、max_gas_price=1000、priority=10、50 字节 0x11 envelope）；sender 余额同 `:101`；`seedOpPredeploys(ts)` 同 `:104`；L1Block setter code 同 `:106-107`（`60003560015560203560035560403560075560603560085500`，calldata 四个 32 字节字依次写 slot 1/3/7/8）；attributes 打包数据同 `:123-125`；**普通 deposit 的 mint=1000（harness 同值，v1 草图的 1e9 系笔误）**；harness 只有一个普通 tx 构造——**第二笔普通 tx = 同 sender、nonce=1（必要偏离，如实记录）**，且带 100 字节非零 calldata（红队 F6：gas 不对称钉顺序）。「fee 槽值」经 attributes deposit 执行 setter 写入（非直接预置——v1 措辞勘正）。

```cpp
// 混排 cumulative + receipts 顺序（红队 F6 强化）：attributes + 普通 deposit + 普通 tx ×2
TEST(OpBlockExecute, CumulativeGasAccumulatesAcrossMixedTxs)
{
    /* 构造见上（attributes 用 harness 真 setter+打包数据；tx4 带 100 字节非零 calldata） */
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 4u);
    const auto& r0 = std::get<OpDepositReceipt>(r.receipts[0]).receipt;
    const auto& r1 = std::get<OpDepositReceipt>(r.receipts[1]).receipt;
    const auto& r2 = std::get<OpTxReceipt>(r.receipts[2]).receipt;
    const auto& r3 = std::get<OpTxReceipt>(r.receipts[3]).receipt;
    EXPECT_EQ(r0.cumulative_gas_used, r0.gas_used);
    EXPECT_EQ(r1.cumulative_gas_used, r0.gas_used + r1.gas_used);
    EXPECT_EQ(r2.cumulative_gas_used, r1.cumulative_gas_used + r2.gas_used);
    EXPECT_EQ(r3.cumulative_gas_used, r2.cumulative_gas_used + r3.gas_used);
    EXPECT_EQ(r.gasUsed, r3.cumulative_gas_used);
    EXPECT_LT(r2.gas_used, r3.gas_used);  // 非对称 gas 钉 receipts[i]↔txs[i] 映射（防自洽换序）
}

// gas pool 恰等边界（两段式实测取锚，断言差分不写魔数）
TEST(OpBlockExecute, BlockGasPoolExactBoundary)
{
    /* 第一段：宽限（30M）跑 attributes+tx1 取实测 gasUsed；
       第二段：b.gas_limit = attr.gasUsed + tx1.gasUsed + tx2.gas_limit 恰等 */
    EXPECT_EQ(rExact.receipts.size(), 3u);                       // 恰等 → 接受
    /* 第三段：b.gas_limit 减 1 */
    EXPECT_THROW(processOpBlock(ts3, bMinus1, hashes, txs, isthmusConfig(), vm, 1234, apply3),
        std::runtime_error);                                     // 超 1 gas → 块级错误
}

// 红队 F1 固化（风险 6 钉死结论 (a) 落码）：deposit 未用 gas **已释放**给后续 tx——
// attributes gas_limit=1'000'000 实用 ~21k；b.gas_limit = attr.实测gasUsed + tx.gas_limit
// （远小于 attr.gas_limit + tx.gas_limit）。gasUsed 口径 → 接受；错误的 gasLimit 口径
// 实现（重构回潮）→ tx 超"剩余" → throw → 翻红。
TEST(OpBlockExecute, DepositUnusedGasReleasedToPool)
{
    /* 构造：attributes(gas_limit=1'000'000) + 普通 tx(gas_limit=100000)；
       第一段宽限实测 attr.gasUsed；第二段 b.gas_limit = attr.gasUsed + 100000 */
    EXPECT_EQ(r.receipts.size(), 2u);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[1]).receipt.status, EVMC_SUCCESS);
}

// 红队 F4（覆盖表虚标修复）：deposit 出现在普通 tx 之后 → 块级错误（自加严，用户裁定）
TEST(OpBlockExecute, DepositAfterNormalTxIsBlockError)
{
    /* 构造：attributes + 普通 tx + 普通 deposit(mint=1000, to=自身) */
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);
}

// 红队 F5：fee params 解包时序——pre-state slot1 预置陈旧值 A，attributes 经 setter 写 B；
// oracle 用 B 的字节独立构造（unpackOpFeeParams + computeL1Cost，均已被既有测试钉死），
// 不经被测路径。块首（attributes 前）取参的作弊实现按 A/零 scalar 计费 → 翻红。
TEST(OpBlockExecute, FeeParamsLoadedAfterAttributesExecution)
{
    /* 构造：seedOpPredeploys + setter code；ts[OP_L1_BLOCK].storage[slot1] =
       store<bytes32>(uint256{5'000'000'000})（陈旧 A）；attributes.data = harness :123-125
       打包（值 B）；+ 1 笔普通 tx（50 字节 envelope） */
    const auto feeB = loadOpFeeParams(ts);  // 块后重取 = B（setter 已执行）
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance,
        computeL1Cost(feeB, {env.data(), env.size()}, isthmusConfig()));
    EXPECT_NE(feeB.l1_base_fee, intx::uint256{5'000'000'000});  // 确认 A 已被 B 覆盖（防真空）
}

// 普通 tx validate 错误 = 块级（nonce=99）。断言异常**消息**（基线审查 Finding 6：
// Task 1 占位 throw 同为 runtime_error，只断类型在红相是假红）
TEST(OpBlockExecute, InvalidNormalTxIsBlockError)
{
    /* 构造：attributes + nonce=99 普通 tx */
    try
    {
        processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("invalid non-deposit tx"), std::string::npos);
    }
}

// 写回时序（计数器探针）：kSeq 合约 code = SLOAD(0)+1 → SSTORE(0)
// （hex 60005460010160005500）；tx1/tx2（同 sender，nonce 0/1）先后调它。
// 每笔 diff 及时写回 → tx2 读到 1 再 +1 → post-state slot0==2；
// 写回失效 → 两笔都从 0 起算 → slot0==1 → 翻红。
TEST(OpBlockExecute, LaterTxSeesEarlierTxWrites)
{
    /* 构造：attributes + tx1(to=kSeq) + tx2(to=kSeq, nonce=1)，其余同构造事实段 */
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[1]).receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[2]).receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kSeq).storage.at(0x00_bytes32), 0x02_bytes32);
}
```

- [ ] **Step 2: 跑确认失败**

Expected：`Cumulative`/`GasPool`/`DepositUnused`/`DepositAfter`/`FeeParams`/`LaterTx` 六个撞 Task 1 占位 throw 而 FAIL；`InvalidNormalTx` 因断言**消息**而 FAIL（占位消息是 "lands in Task 2"，非 "invalid non-deposit tx"——真红）。

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
Expected: 全部 PASS（92 + 7 = 99）

```bash
rtk git add bcos-evm-ref/opstack/OpBlockExecute.cpp test/opstack/OpBlockExecuteTest.cpp
rtk git commit -m "feat(evm-ref): processOpBlock 逐笔循环——混排账务/gas pool 口径钉扎/fee 时序/结构校验（M-B1 Task 2）"
```

---

### Task 3: harness 升级 + 文档闭环

**Files:**
- Modify: `test/opstack/OpBlockHarnessTest.cpp`（改调 `processOpBlock`；`:119` 过时注释勘正——evmone 实际导出 `system_call_block_start`）
- Modify: `docs/audits/2026-07-10-opstack-code-review-defect-ledger.md`（D-10 状态 🔶 → ✅，引接线 commit）
- Modify: 主 spec §7.1（M-B1 行标完成）、块执行 spec §4.1（`OpBlockResult` 有序序列细化回写 + M-B1 状态）

- [ ] **Step 1: harness 升级**——`OpBlockHarnessTest` 的手工顺序段（实测 `:116-210` 区域：手工 runDeposit×2 + opValidate/opTransition + 手工写回）替换为一次 `processOpBlock` 调用，数值断言按以下口径迁移（基线审查 Finding 8 的处置）：
  - fee 六值、vault 余额、cumulative、depFrom 余额断言**数值不变**，锚改为库产出：`props.l1_cost` 的引用改 `OpTxReceipt.meta.l1_fee`（字段已核存在）；**`OP_OPERATOR_FEE_VAULT` 的断言保持对绝对期望值比较，禁止改成对 receipt meta 的自洽比较**（红队 F5：自洽比较会掩护"块首取参"作弊）；
  - **`FromState≡` 段（`:190-198`）拆出为独立 TEST 保留手工最小序列**（attributes 手工 runDeposit + 写回 → `opValidateFromState/opTransitionFromState` 对拍）——它钉的是 *FromState API 本身，需要块中间态，不能也不必调库化；
  - `:119` 注释改为：`// pre-block 系统调用（EIP-4788/2935）由 processOpBlock 内部执行（evmone system_call_block_start，REF 已导出——旧注释"未导出"有误，2026-07-11 勘正）。`
- [ ] **Step 2: 全量测试**（Expected: 99 全 PASS——harness 数值断言等值通过即证明库化无语义漂移；任何断言失手 = 漂移信号，停下分析，禁止改断言迁就）
- [ ] **Step 3: 文档回填**——台账 D-10：`🔶` → `✅ FIXED（<Task 1 commit 短哈希> 接线 processOpBlock；调用存在性由 FinalizeIsActuallyWired 护栏用例钉住——该用例证明"被调且 tx 全跑完后可达"，diff 恒空故 applyDiff 与末位时序原理上不可观测，diff 语义由 rev.2 Task 7 的单测钉）`（红队 F3 的如实降级措辞）；主 spec §7.1 加 M-B1 行 ✅；块执行 spec §4.1 的 `OpBlockResult` 草图更新为有序序列并注明"M-B1 plan 细化"。
- [ ] **Step 4: Commit（点名文件——基线审查 Finding 9：目录级 add 会卷入无关 untracked 文件）**

```bash
rtk git add test/opstack/OpBlockHarnessTest.cpp docs/audits/2026-07-10-opstack-code-review-defect-ledger.md \
  ../bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md \
  ../bcos-evm/docs/superpowers/specs/2026-07-11-bcos-evm-ref-op-block-execution-design.md
rtk git commit -m "test+docs(evm-ref): harness 调库化 + D-10 闭环回填 + spec M-B1 状态（M-B1 Task 3）"
```

---

## 覆盖对照（spec §4.3 M-B1 单测清单 → task）

| spec 要求 / 审查补强 | 用例 | Task |
|---|---|---|
| cumulative 混排累计 + receipts 顺序钉扎（F6） | CumulativeGasAccumulatesAcrossMixedTxs | 2 |
| gas pool 递减与恰等边界 | BlockGasPoolExactBoundary | 2 |
| deposit 池口径钉扎（风险 6 结论 (a) 落码，F1） | DepositUnusedGasReleasedToPool | 2 |
| deposit-only 块 | DepositOnlyBlockSucceeds | 1 |
| 4788 槽写入 | BlockStartSystemCallWritesBeaconSlot（替身，接线级；真合约行为归 M-B3） | 1 |
| **系统调用顺序 + 其 diff 写回**（F2） | SystemCallRunsBeforeAttributesTx | 1 |
| Fjord 下 2935 不发生 | HistoryStorageOnlyWrittenFromIsthmus | 1 |
| 块结构违规/首笔非 attributes（含首笔为普通 tx 变体，F7） | RejectsEmptyOrBadFirstTx | 1 |
| **deposit 在普通 tx 之后 → throw**（F4，v1 覆盖表虚标修复） | DepositAfterNormalTxIsBlockError | 2 |
| 普通 tx validate 错误 = 块级（真红：断言消息） | InvalidNormalTxIsBlockError | 2 |
| **finalize 被调证明**（F3，D-10 依据） | FinalizeIsActuallyWired | 1 |
| **fee params 解包时序**（F5） | FeeParamsLoadedAfterAttributesExecution | 2 |
| 写回时序（计数器探针） | LaterTxSeesEarlierTxWrites | 2 |

## 已知风险与注意事项

1. **系统调用替身 vs 真合约**：Task 1 的 4788/2935 断言用测试替身（SSTORE 入参）只证接线与门控；真实合约字节码行为（ring buffer 等）**不在 M-B1 置信范围**，由 M-B3 块级差分兜底——台账/报告不得声称"4788 语义已验证"。
2. Task 2 的 `BlockGasPoolExactBoundary` 用两段式实测取值（先宽跑取 gasUsed 再收紧），**不写魔数**——符合断言数值纪律（实测值即锚）。
3. `deposit after non-deposit` 的 throw 在 Task 1 代码已实现（`seenNonDeposit` 检查），但可测路径要等 Task 2 普通 tx 分支就位——其用例放 Task 2。
4. harness 升级（Task 3）若发现任何数值断言失手——**这是库化引入语义漂移的信号**，停下分析，禁止改断言迁就。
5. `OpBlockHarnessTest` 中 `FromState≡` 相关段若与调库路径冗余，保留不删（它们钉的是 *FromState API 本身）。
6. **✅ 账务疑点已钉死（2026-07-11 对抗审查，结论 (a)）**：**deposit 对 gas pool 的净消耗 = `receipt.gas_used`，plan 现行统一扣法正确**。证据链：preCheck `gp.SubGas(gasLimit)`（`state_transition.go:360`，其注释描述的是 pre-Regolith/失败路径）；**Regolith 成功 deposit 在 `:681` 早退之前经过 `:672 gp.ReturnGas(gasRemaining, gasUsed)`**，未用 gas 回池，净消耗 = gasUsed；失败 deposit `:502 ReturnGas(0, gasUsed)` 且 `:498 gasUsed=gasLimit`——其 receipt.gas_used 本身就是 gasLimit，统一按 `receipt.gas_used` 扣在两种情形下都精确复现净效应。cumulative（`gaspool.go:67` 恒按 gasUsed 累计）与池扣法解耦但 pre-Amsterdam 数值恒等。**口径已固化为 Task 2 的 `DepositUnusedGasReleasedToPool` 用例**（红队 F1：钉死结论必须落码，否则重构改口径无人翻红）。
7. **调用方弃写契约**（op-geth `Process` 报错时整个 statedb 被丢弃）：`processOpBlock` 任何 throw 后，调用方**必须弃掉本块已 applyDiff 的全部写集**——已写入 OpBlockExecute.h 头注释（Task 1）。
8. **一项待用户裁定（对抗审查发现，不阻塞执行）**：op-node 还校验 attributes 的 **data 可解析性**（selector + fork 精确长度，`payload_util.go:37-40` → `l1_block_info.go:467-479`），本 plan 的自加严只查 to/from。补的话 Task 1 的 `data = {}` 构造要连动改。**默认不补**（M-B1 维持 to/from 校验；data 解析属 M-B2/编排上游职责候选），用户可改判。

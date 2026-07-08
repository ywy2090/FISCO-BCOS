# Eth Fee Settlement — Remove ledger Dependency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Eth 参考路径的费用结算从 `EVMAccount`（`bcos-framework/ledger`）迁移到 `state::State`，使 `bcos-evm-eth` 库不再链接或 `#include` ledger；TE 只调用一次 `applyStateDiff`。

**Architecture:** 镜像 OpStack 已落地的分层：`EthSettlementProjection`（只读 facade）→ `EthFeeSettlement`（State 余额变更）→ `EthTxFinalize`（gas 计量）→ `EthNormalTxFeeCoordinator`（buyGas → execute → refund 生命周期）。`applyEthMessage` 内编排 fee + pipeline，**单层 pre-buyGas checkpoint**；`EthTransactionExecutorImpl` 瘦身为 apply + 单次落账 + makeReceipt。

**Tech Stack:** C++20，Boost.Test，CMake/CTest，bcos-evm-eth，transaction-executor，evmone。

**Prerequisite ADR:** ADR-005（eth/ 不含 bcos/op）、ADR-015（included-tx vmerr）、ADR-025（phantom fee）、ADR-026（TxFeeSettlement 纯函数 + adapter 改余额）、ADR-028（consensus reject entry failure，本计划 defer）。

**Design spec:** [2026-07-03-eth-fee-settlement-remove-ledger-dependency-design.md](../specs/2026-07-03-eth-fee-settlement-remove-ledger-dependency-design.md)（含 geth 基线 §1.3、交叉审阅决议附录 A）。

> **关键设计前提（spec §1.3）：** 顶层 EVM overlay 的 vmerr 回退**已由 kernel 完成**
> （`EvmCallFrame::finalizeFrame` 非 SUCCESS → `state.revert()`；`InnerExecute::finalizeAfterFrame`
> SUCCESS → `state.commit()`），等价 geth `evm.Call` Snapshot/RevertToSnapshot。故 fee 层
> **不做** hard-fail revert，只用**单层 pre-buyGas checkpoint** 供 pre-exec reject 撤销 buyGas。

## Global Constraints

- **`eth/` seam discipline（ADR-005 Rule 1）：** `bcos-evm/eth/**` **不得** `#include` `bcos-framework/ledger/*`、`bcos-ledger/*`、`bcos/`、`opstack/`。
- **纯函数层不变：** `eth/gas/TxFeeSettlement.h`、`eth/eip/Eip1559.h` 保持 State-free；adapter 只读 plan 写 State。
- **单层 checkpoint（spec §6 / Q3）：** 只在 buyGas 前做 **一次** `ctx.state.checkpoint()`；**无** post-buyGas checkpoint、fee 层**无** `commit()` 调用。顶层帧的 commit/revert 由 kernel（`finalizeAfterFrame`/`finalizeFrame`）负责。
- **Eth buyGas penalty（ADR-026 D5）：** 余额不足时扣 `min(balance, TX_BASE_GAS × effectiveGasPrice)` 并返回 `NotEnoughCash` — **保留**，在 `EthFeeSettlement::buyGas` 实现；penalty diff 保留，**不** revert。
- **Vmerr / hard fail 语义（ADR-015 + spec §1.3）：** `status ∉ {SUCCESS, REVERT}`（含 included vmerr）时 EVM 执行体回退**已由 kernel 完成**；fee 层对所有非 pre-exec-reject 分支**一律 refund（gas 照收 + tip）、不 revert、不 commit**。included vmerr 仅 `topLevelIncludedTxVmError` 传播用于 receipt normalize，**不** commit EVM。
- **Pre-execution reject（ADR-025）：** intrinsic / gas-afford reject → 单层 `state.revert()` 撤销 buyGas，gasUsed=0、空 diff（对齐 `OpStackNormalTxFeeCoordinator::completeAfterPipeline`）。
- **`ctx.gasPrice` 注入：** buyGas 成功后、`stateTransitionExecute` 前，apply 层设 `ctx.gasPrice = sidecar.effectiveGasPrice`（对齐 OpStack `ApplyOpStackMessage.cpp:150`）。
- **Base fee：** Eth 路径 implicit burn（只扣 sender、给 coinbase tip；`baseFeeAmount` 不进任何账户）。
- **FISCO 路径不在本计划范围（ADR-026 non-goals）：** `bcos/FiscoTxFeeSettlement.h` 仍用 `EVMAccount`；`bcos-evm-bcos` 可继续 transitively 依赖 ledger。
- **命令前缀：** `rtk`
- **构建目录：** `build-bcos-evm-check/`（已存在则复用；否则 `-DTESTS=ON` 配置一次）

---

## Scope Check

| 子系统 | 本计划 | 理由 |
|--------|--------|------|
| `bcos-evm-eth`（`eth/`） | ✅ 在范围 | 去除 ledger 的唯一必要路径 |
| `transaction-executor` Eth TE | ✅ 在范围 | 编排收敛到 `applyEthMessage` |
| `bcos-evm-bcos`（`bcos/`） | ❌ 不在范围 | 生产 storage 适配层；`StateDiffApplier` 仍需要 `EVMAccount` |
| FISCO `FiscoTxFeeSettlement` | ❌ follow-up | 独立计划 |

---

## Semantic Matrix（Phase 0 — 实现前冻结，对齐 spec §6）

EVM 执行体回退由 **kernel** 负责（spec §1.3）；「EVM overlay」列指 `stateTransitionExecute` 返回时 `ctx.state` 中执行体改动的存留，fee 层不干预。

| 场景 | coordinator 分支 | EVM overlay（kernel 结果） | fee 层动作 | stateDiff | gasUsed |
|------|------------------|----------------------------|-----------|-----------|---------|
| `eth_call`（`isCall=true`） | 跳过 coordinator | 保留 | 无 buyGas/refund | TE **不** apply（Q2） | 0 |
| buyGas 余额不足（penalty） | `buyGas` → false | 未进入 EVM | penalty 扣减，**不** revert | penalty diff | `penalty / effectiveGasPrice` |
| intrinsic / gas-afford reject | `abortEthAfterBuyGas` | 未进入 EVM | `revert()` 撤销 buyGas | 空 | 0 |
| `EVMC_SUCCESS` | 正常 | kernel `commit` 保留 | refund + tip | fee + EVM | finalize |
| `EVMC_REVERT` | 正常 | kernel `revert` 丢执行体 | refund + tip | fee + pre-frame | finalize |
| vmerr（OOG/INVALID，非 included） | 正常 | kernel `revert` 丢执行体 | refund + tip | fee + pre-frame | finalize |
| included vmerr（`topLevelIncludedTxVmError`） | 正常 | kernel `revert` 丢执行体 | refund + tip；receipt normalize→success | fee + pre-frame | finalize |
| `ExceptionHandled` | 正常 | errorPolicy 已 `revert` | refund + tip | fee + pre-frame | finalize |
| `effectiveGasPrice == 0` | buyGas/refund 早退 | 按上表 | 无 | 按 EVM | 按 EVM |

---

## File Structure

| 文件 | 职责 | 动作 |
|------|------|------|
| `bcos-evm/eth/policy/EthChainPolicy.h` | Eth 链策略 | Modify — 移除 `ledger::Features` |
| `bcos-evm/eth/settlement/EthFeeSidecar.h` | buyGas 快照（effectiveGasPrice 等） | Create |
| `bcos-evm/eth/settlement/EthSettlementProjection.h` | ctx + request facade | Create |
| `bcos-evm/eth/settlement/EthFeeSettlement.h` | 声明 buyGas/refundGas | Create |
| `bcos-evm/eth/settlement/EthFeeSettlement.cpp` | State 余额变更 | Create |
| `bcos-evm/eth/settlement/EthTxFinalize.h` | gas 计量 + abort 辅助 | Create |
| `bcos-evm/eth/settlement/EthTxFinalize.cpp` | finalizeNormal / abort | Create |
| `bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.h` | 生命周期编排 | Create |
| `bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.cpp` | buyGas → completeAfterPipeline | Create |
| `bcos-evm/eth/apply/ApplyEthMessage.h` | 扩展 request/result | Modify |
| `bcos-evm/eth/apply/ApplyEthMessage.cpp` | 接入 coordinator | Modify |
| `bcos-evm/eth/apply/EthTxFeeSettlement.h` | 旧 EVMAccount adapter | Delete（Task 8） |
| `bcos-evm/include/bcos-evm/eth_executor.hpp` | 公共 re-export | Modify |
| `bcos-evm/CMakeLists.txt` | `bcos-evm-eth` link | Modify — 移除 `ledger` |
| `bcos-evm/test/eth/EthFeeSettlementStateTest.cpp` | State 路径单测 | Create |
| `bcos-evm/test/cmake/EthTests.cmake` | 注册测试 | Modify |
| `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h` | 瘦 TE | Modify |
| `tools/ci/check-eth-pr-slices.sh` | eth PR slice 列表 | Modify |
| `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md` | adapter 表更新 | Modify |
| `bcos-evm/eth/README.md` | 目录说明 | Modify |

**不变（仍合法依赖 ledger）：** `bcos-evm/bcos/FiscoStateView.h`、`StateDiffApplier.h`、`FiscoBlockInfo.h`、`FiscoTxFeeSettlement.h`。

---

### Task 1: Decouple EthChainPolicy from ledger

**Files:**
- Modify: `bcos-evm/eth/policy/EthChainPolicy.h`

**Interfaces:**
- Consumes: nothing
- Produces: `EthChainPolicy` without `#include "bcos-framework/ledger/Features.h"`

- [ ] **Step 1: Write the failing compile check**

在 `bcos-evm/eth/policy/EthChainPolicy.h` 中删除 `#include "bcos-framework/ledger/Features.h"` 及 `features()` 方法（Eth 参考路径无调用方；`TestStandardEthPolicy` 不测此方法）。

- [ ] **Step 2: Verify eth lib builds without ledger Features**

Run:
```bash
rtk grep -l 'bcos-framework/ledger' bcos-evm/eth/
```
Expected: **无匹配**（Task 1 完成后仍可能有 `EthTxFeeSettlement.h`，Task 8 后应为零）

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target bcos-evm-eth -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```
Expected: PASS

- [ ] **Step 3: Run existing policy test**

Run:
```bash
cd build-bcos-evm-check && ctest -R EthChainPolicy -V
```
Expected: PASS（若 TE 测试未注册此名，运行 `TestStandardEthPolicy` 可执行文件）

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/policy/EthChainPolicy.h
rtk git commit -m "$(cat <<'EOF'
refactor(eth): remove ledger Features from EthChainPolicy

Eth reference path does not use feature flags; drop the only non-fee
ledger include precursor before settlement migration.
EOF
)"
```

---

### Task 2: Eth settlement projection types

**Files:**
- Create: `bcos-evm/eth/settlement/EthFeeSidecar.h`
- Create: `bcos-evm/eth/settlement/EthSettlementProjection.h`
- Create: `bcos-evm/eth/settlement/EthSettlementProjection.cpp`

**Interfaces:**
- Consumes: `StateTransitionContext`, `EthMessageRequest`
- Produces: `EthFeeSidecar`, `EthSettlementProjection` with `pipelineContext()`, fee cap accessors

- [ ] **Step 1: Create EthFeeSidecar.h**

```cpp
#pragma once
#include <bcos-utilities/Common.h>

namespace bcos::evm
{
struct EthFeeSidecar
{
    bcos::u256 effectiveGasPrice{0};
    int64_t penaltyGasUsed{0};  // buyGas 余额不足时 = penalty / effectiveGasPrice
};
}  // namespace bcos::evm
```

> **说明（spec §5.5 落地）：** spec §5.1 sidecar 仅列 `effectiveGasPrice`；`penaltyGasUsed` 是本计划为兑现「coordinator 在 buyGas fail 时写 `output.gasUsed`」而增的实现载体，等价现 `EthTxFeeSettlement::buyGas` 的 `data.m_gasUsed = penalty / effectiveGasPrice`。

- [ ] **Step 2: Create EthSettlementProjection.h**

```cpp
#pragma once
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/settlement/EthFeeSidecar.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"

namespace bcos::evm
{
struct EthSettlementProjection
{
    StateTransitionContext& ctx;
    EthMessageRequest const& input;
    EthFeeSidecar& sidecar;

    StateTransitionContext& pipelineContext() noexcept { return ctx; }
    bool isCall() const noexcept { return input.isCall; }
    bcos::u256 gasTipCap() const noexcept { return input.gasTipCap; }
    bcos::u256 gasFeeCap() const noexcept { return input.gasFeeCap; }
    bcos::u256 gasPriceLegacy() const noexcept { return input.gasPrice; }
    uint8_t web3TypedTxKind() const noexcept { return input.web3TypedTxKind; }
    bool hasExplicitFeeCaps() const noexcept { return input.hasExplicitFeeCaps; }
    state::BlockInfo const& blockInfo() const noexcept { return input.blockInfo; }
    bcos::u256 txValue() const noexcept { return input.txValue; }
    int64_t gasLimit() const noexcept { return ctx.originalGasLimit; }
};
}  // namespace bcos::evm
```

- [ ] **Step 3: Extend EthMessageRequest in ApplyEthMessage.h**

在 `EthMessageRequest` 增加：
```cpp
    bool isCall{false};
    bcos::u256 txValue{0};
```

> **不加 `isWeb3`（ADR-005）：** EIP-7623 结算 gate 由 `IntrinsicDebitMode::Eip7623` → `snapshot.gasLimit > 0` 表达，不透传 FISCO 交易类型进 eth 层。

在 `EthMessageResult` 增加：
```cpp
    int64_t gasUsed{0};
    bcos::u256 effectiveGasPrice{0};
    std::string gasPriceStr;
```

- [ ] **Step 4: Build**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target bcos-evm-eth -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/settlement/ bcos-evm/eth/apply/ApplyEthMessage.h
rtk git commit -m "$(cat <<'EOF'
feat(eth): add settlement projection types for State-based fees

Mirror OpStackSettlementProjection with Eth-specific fee sidecar and
extend EthMessageRequest/Result for coordinator outputs.
EOF
)"
```

---

### Task 3: EthFeeSettlement State adapter (TDD)

**Files:**
- Create: `bcos-evm/eth/settlement/EthFeeSettlement.h`
- Create: `bcos-evm/eth/settlement/EthFeeSettlement.cpp`
- Create: `bcos-evm/test/eth/EthFeeSettlementStateTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`

**Interfaces:**
- Consumes: `gas::toFeeInputs`, `gas::planPreExecution`, `gas::planPostExecution`, `EthSettlementProjection`
- Produces:
  - `task::Task<bool> EthFeeSettlement::buyGas(EthSettlementProjection view)`
  - `task::Task<gas::FeeSettlementPlan> EthFeeSettlement::refundGas(EthSettlementProjection& view, EthTxFinalizeResult const& settled)`

- [ ] **Step 1: Write the failing test**

`bcos-evm/test/eth/EthFeeSettlementStateTest.cpp`:

```cpp
#define BOOST_TEST_MODULE EthFeeSettlementStateTest
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}
}  // namespace

BOOST_AUTO_TEST_CASE(buyGas_debits_sender_on_state)
{
    InMemoryStateView base;
    base.setBalance(addr(1), 1'000'000);
    state::State state(base);

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.gasLimit = 21'000;
    input.txValue = 0;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(state.get_balance(addr(1)), 1'000'000 - 21'000 * 100);
    BOOST_CHECK_EQUAL(sidecar.effectiveGasPrice, 100);
}

BOOST_AUTO_TEST_CASE(buyGas_insufficient_balance_applies_penalty)
{
    InMemoryStateView base;
    base.setBalance(addr(1), 500);
    state::State state(base);

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.gasFeeCap = 100;
    input.gasTipCap = 2;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 2;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(!ok);
    // penalty = min(500, TX_BASE_GAS * effective) — balance reduced
    BOOST_CHECK(state.get_balance(addr(1)) < 500);
    BOOST_CHECK(ctx.evmcResult.status == protocol::TransactionStatus::NotEnoughCash);
}
}  // namespace bcos::evm::test
```

`EthTests.cmake` 追加：
```cmake
add_executable(EthFeeSettlementStateTest eth/EthFeeSettlementStateTest.cpp)
target_include_directories(EthFeeSettlementStateTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthFeeSettlementStateTest PRIVATE bcos-evm-eth bcos-task evmone::evmone)
add_test(NAME EthFeeSettlementState COMMAND EthFeeSettlementStateTest)
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target EthFeeSettlementStateTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./bcos-evm/test/EthFeeSettlementStateTest
```
Expected: FAIL — `EthFeeSettlement` 未定义

- [ ] **Step 3: Implement EthFeeSettlement**

`EthFeeSettlement.h`:
```cpp
#pragma once
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{
struct EthTxFinalizeResult;

struct EthFeeSettlement
{
    task::Task<bool> buyGas(EthSettlementProjection view);
    task::Task<gas::FeeSettlementPlan> refundGas(
        EthSettlementProjection& view, EthTxFinalizeResult const& settled);
};
}  // namespace bcos::evm
```

`EthFeeSettlement.cpp` 核心逻辑（镜像 `OpStackFeeSettlement.cpp` + 现 `EthTxFeeSettlement.h` penalty）：

```cpp
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/eth/settlement/EthTxFinalize.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"

namespace bcos::evm
{
namespace
{
void addBalance(state::State& state, evmc_address const& address, bcos::u256 const& delta)
{
    if (delta == 0) return;
    state.set_balance(address, state.get_balance(address) + delta);
}
}  // namespace

task::Task<bool> EthFeeSettlement::buyGas(EthSettlementProjection view)
{
    auto& ctx = view.pipelineContext();
    auto& sidecar = view.sidecar;

    if (view.isCall())
        co_return true;
    if (ctx.originalGasLimit <= 0)
        co_return true;

    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{view.gasPriceLegacy(), view.gasTipCap(), view.gasFeeCap(),
            view.web3TypedTxKind(), view.hasExplicitFeeCaps()},
        ctx.originalGasLimit);
    auto const plan = gas::planPreExecution(feeInputs);
    sidecar.effectiveGasPrice = plan.effectiveGasPrice;
    if (sidecar.effectiveGasPrice == 0)
        co_return true;

    auto const totalRequired = plan.maxBalanceDebit + view.txValue();
    auto const senderBalance = ctx.state.get_balance(ctx.message.sender);
    if (senderBalance < totalRequired)
    {
        auto const intrinsicCost = bcos::u256(gas::TX_BASE_GAS) * sidecar.effectiveGasPrice;
        auto const penalty = std::min(senderBalance, intrinsicCost);
        if (penalty > 0)
            ctx.state.set_balance(ctx.message.sender, senderBalance - penalty);

        sidecar.penaltyGasUsed = (penalty / sidecar.effectiveGasPrice).convert_to<int64_t>();

        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash);
        co_return false;
    }

    ctx.state.set_balance(ctx.message.sender, senderBalance - plan.preDebitAmount);
    co_return true;
}

task::Task<gas::FeeSettlementPlan> EthFeeSettlement::refundGas(
    EthSettlementProjection& view, EthTxFinalizeResult const& settled)
{
    auto& ctx = view.pipelineContext();
    if (view.isCall() || view.sidecar.effectiveGasPrice == 0)
        co_return gas::FeeSettlementPlan{};

    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{view.gasPriceLegacy(), view.gasTipCap(), view.gasFeeCap(),
            view.web3TypedTxKind(), view.hasExplicitFeeCaps()},
        ctx.originalGasLimit);

    auto const plan = gas::planPostExecution(
        feeInputs, settled.gasUsed, static_cast<int64_t>(settled.gasRemaining));

    addBalance(ctx.state, ctx.message.sender, plan.unusedRefund);
    addBalance(ctx.state, view.blockInfo().coinbase, plan.coinbaseTip);
    // baseFeeAmount implicitly burned on Eth — no credit

    co_return plan;
}
}  // namespace bcos::evm
```

- [ ] **Step 4: Run tests**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target EthFeeSettlementStateTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk test ./bcos-evm/test/EthFeeSettlementStateTest
rtk test ./bcos-evm/test/TxFeeSettlementTest
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/settlement/EthFeeSettlement.* bcos-evm/test/eth/EthFeeSettlementStateTest.cpp bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(eth): State-based EthFeeSettlement buyGas and refundGas

Replace EVMAccount adapter with state::State mutations; preserve ADR-026
buyGas penalty semantics with unit tests.
EOF
)"
```

---

### Task 4: EthTxFinalize gas metering

**Files:**
- Create: `bcos-evm/eth/settlement/EthTxFinalize.h`
- Create: `bcos-evm/eth/settlement/EthTxFinalize.cpp`
- Modify: `bcos-evm/test/eth/EthFeeSettlementStateTest.cpp`（追加 finalize 测试）

**Interfaces:**
- Consumes: `StateTransitionContext`, `StateTransitionExitKind`, `gas::TxGasSettlementContext`
- Produces:
  - `struct EthTxFinalizeResult { int64_t gasUsed; uint64_t gasRemaining; }`
  - `bool isEthPreExecutionReject(StateTransitionExitKind) noexcept`
  - `void abortEthAfterBuyGas(StateTransitionContext&, EthMessageResult&, int64_t originalGasLimit)`
  - `EthTxFinalizeResult finalizeEthNormal(StateTransitionContext const&, StateTransitionExitKind, gas::TxGasSettlementContext const&, bool topLevelIncludedTxVmError)`（spec §5.4：**无 isWeb3**；7623 gate 用 `ctx.revisionConfig.eip7623 && snapshot.gasLimit > 0`。`topLevelIncludedTxVmError` 由 coordinator 传入，included-vmerr peak-gas 修正沿用现 TE 行为 — 纯搬迁零行为变化）

- [ ] **Step 1: Write failing test for EIP-7623 gasUsed**

在 `EthFeeSettlementStateTest.cpp` 追加：

```cpp
BOOST_AUTO_TEST_CASE(finalizeEthNormal_eip7623_uses_settlement_snapshot)
{
    evmc_message msg{};
    msg.gas = 1000;
    RevisionConfig rev{};
    rev.revision = EVMC_PRAGUE;
    rev.eip7623 = true;
    rev.calldata_floor_per_token = 10;
    InMemoryStateView base;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(1));

    ctx.evmcResult.status_code = EVMC_SUCCESS;
    ctx.evmcResult.gas_left = 800;
    ctx.snapshot.evmGasRefund = 0;
    ctx.snapshot.calldata = {10, 0, 0}; // floor path exercised in real snapshot

    gas::TxGasSettlementContext snap = ctx.snapshot;
    snap.gasLimit = 1000; // 模拟 Eip7623 mode 已捕获 snapshot（captureSettlementSnapshot）
    auto const out = finalizeEthNormal(ctx, StateTransitionExitKind::Completed, snap,
        /*topLevelIncludedTxVmError=*/false);
    BOOST_CHECK(out.gasUsed > 0);
    BOOST_CHECK_EQUAL(out.gasRemaining, 800);
}
```

- [ ] **Step 2: Run test — expect link error**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target EthFeeSettlementStateTest && ./bcos-evm/test/EthFeeSettlementStateTest
```
Expected: FAIL — `finalizeEthNormal` undefined

- [ ] **Step 3: Implement EthTxFinalize.cpp**

从 `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` **搬迁**逻辑：

```cpp
#include "bcos-evm/eth/settlement/EthTxFinalize.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"

namespace bcos::evm
{
bool isEthPreExecutionReject(StateTransitionExitKind exitKind) noexcept
{
    return exitKind == StateTransitionExitKind::IntrinsicRejected ||
           exitKind == StateTransitionExitKind::GasAffordRejected;
}

void abortEthAfterBuyGas(StateTransitionContext& ctx, EthMessageResult& output, int64_t)
{
    if (ctx.state.has_checkpoint())
        ctx.state.revert();
    output.gasUsed = 0;
    output.stateDiff = ctx.state.build_diff();
}

EthTxFinalizeResult finalizeEthNormal(StateTransitionContext const& ctx,
    StateTransitionExitKind exitKind, gas::TxGasSettlementContext const& snapshot,
    bool /*topLevelIncludedTxVmError*/)
{
    // topLevelIncludedTxVmError 目前不参与 peak-gas 修正（沿用现 TE 行为，纯搬迁）；
    // 保留形参以便 ADR-015 GAP-TE-002 后续单点修改。
    EthTxFinalizeResult out{};
    if (isEthPreExecutionReject(exitKind))
    {
        out.gasUsed = 0;
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        return out;
    }

    if (exitKind == StateTransitionExitKind::Completed ||
        exitKind == StateTransitionExitKind::ExceptionHandled)
    {
        auto const& evmcResult = ctx.evmcResult;
        // 7623 gate（ADR-005）：snapshot.gasLimit>0 已等价 IntrinsicDebitMode::Eip7623 mode
        // 已激活（captureSettlementSnapshot），无需 isWeb3 —— tx-type 策略在 hooks 层表达。
        if (ctx.revisionConfig.eip7623 && snapshot.gasLimit > 0)
        {
            out.gasUsed = gas::settleTopLevelTransactionGas(ctx.originalGasLimit,
                evmcResult.gas_left, snapshot.evmGasRefund,
                ctx.revisionConfig.calldata_floor_per_token, snapshot.calldata);
        }
        else
        {
            out.gasUsed = ctx.originalGasLimit - evmcResult.gas_left;
        }
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, evmcResult.gas_left));
    }
    return out;
}
}  // namespace bcos::evm
```

- [ ] **Step 4: Run tests — PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/settlement/EthTxFinalize.* bcos-evm/test/eth/EthFeeSettlementStateTest.cpp
rtk git commit -m "$(cat <<'EOF'
feat(eth): extract EthTxFinalize gas metering from TE

Centralize EIP-7623 settleTopLevelTransactionGas in eth/settlement for
coordinator reuse.
EOF
)"
```

---

### Task 5: EthNormalTxFeeCoordinator

**Files:**
- Create: `bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.h`
- Create: `bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.cpp`

**Interfaces:**
- Consumes: `EthFeeSettlement`, `EthTxFinalize`, `EthSettlementProjection`
- Produces:
  - `task::Task<bool> buyGas(EthSettlementProjection, EthMessageResult&)`
  - `task::Task<void> completeAfterPipeline(EthSettlementProjection, EthMessageResult&)`

- [ ] **Step 1: Implement coordinator（镜像 OpStackNormalTxFeeCoordinator.cpp）**

```cpp
// EthNormalTxFeeCoordinator.cpp
task::Task<bool> EthNormalTxFeeCoordinator::buyGas(
    EthSettlementProjection view, EthMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    if (!co_await ledger.buyGas(view))
    {
        // penalty 失败：【不 revert】—— 保留 penalty diff（spec §6 / §5.5）。
        // 对齐现 EthTxFeeSettlement::buyGas：gasUsed = penalty / effectiveGasPrice。
        output.evmcResult = std::move(ctx.evmcResult);
        output.effectiveGasPrice = view.sidecar.effectiveGasPrice;
        output.gasUsed = view.sidecar.penaltyGasUsed;
        if (output.effectiveGasPrice != 0)
            output.gasPriceStr = "0x" + output.effectiveGasPrice.str(256, std::ios_base::hex);
        co_return false;
    }
    co_return true;
}

task::Task<void> EthNormalTxFeeCoordinator::completeAfterPipeline(
    EthSettlementProjection view, EthMessageResult& output)
{
    auto& ctx = view.pipelineContext();

    // ① pre-exec reject（intrinsic / gas-afford）→ 单层 revert 撤销 buyGas（ADR-025）
    if (isEthPreExecutionReject(ctx.exitKind))
    {
        abortEthAfterBuyGas(ctx, output, ctx.originalGasLimit);
        co_return;
    }

    // ② 其它（SUCCESS / REVERT / vmerr / included-vmerr / ExceptionHandled）：
    //    顶层帧的 commit/revert 已由 kernel 完成（spec §1.3）——
    //    fee 层【不 revert、不 commit】，一律 refund（gas 照收 + tip）。
    auto const settled = finalizeEthNormal(ctx, ctx.exitKind,
        output.gasSettlementSnapshot, output.topLevelIncludedTxVmError);
    co_await ledger.refundGas(view, settled);

    output.gasUsed = settled.gasUsed;
    output.effectiveGasPrice = view.sidecar.effectiveGasPrice;
    if (output.effectiveGasPrice != 0)
        output.gasPriceStr = "0x" + output.effectiveGasPrice.str(256, std::ios_base::hex);
    output.stateDiff = ctx.state.build_diff();
}
```

- [ ] **Step 2: Build bcos-evm-eth**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target bcos-evm-eth -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```
Expected: PASS

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.*
rtk git commit -m "$(cat <<'EOF'
feat(eth): add EthNormalTxFeeCoordinator lifecycle orchestrator

Wire buyGas, pre-exec-reject revert, refundGas, and stateDiff build.
Kernel owns EVM overlay commit/revert; fee layer never reverts on vmerr.
EOF
)"
```

---

### Task 6: Wire fee orchestration into applyEthMessage

**Files:**
- Modify: `bcos-evm/eth/apply/ApplyEthMessage.cpp`

**Interfaces:**
- Consumes: Task 3–5 types
- Produces: `applyEthMessage` that runs coordinator when `!input.isCall`

- [ ] **Step 1: Update ApplyEthMessage.cpp**

在 `stateTransitionExecute` **之前**（**单层** checkpoint — spec §6 时序 / Q3）：

```cpp
EthFeeSidecar sidecar;
EthSettlementProjection feeView{ctx, input, sidecar};
EthFeeSettlement feeLedger;
EthNormalTxFeeCoordinator coordinator{feeLedger};

if (!input.isCall)
{
    ctx.state.checkpoint(); // 唯一 checkpoint（pre-buyGas，供 pre-exec reject 撤销 buyGas）
    if (!co_await coordinator.buyGas(feeView, output))
    {
        output.stateDiff = ctx.state.build_diff(); // penalty diff（coordinator 已填 evmcResult/gasUsed）
        co_return output;
    }
    ctx.gasPrice = sidecar.effectiveGasPrice; // 注入供 kernel GASPRICE/tip（OpStack 对齐）
    // 【无】第二 checkpoint：顶层帧 commit/revert 由 kernel 负责（spec §1.3）
}

stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

// ... existing output field copies（含 topLevelIncludedTxVmError 从 ctx 传播）...

if (!input.isCall)
{
    if (bindings.hooks.getIntrinsicGasParams().mode == IntrinsicDebitMode::Eip7623)
        output.gasSettlementSnapshot = ctx.snapshot;
    co_await coordinator.completeAfterPipeline(feeView, output);
}
else
{
    output.stateDiff = ctx.state.build_diff(); // eth_call: no fee, existing behavior
}
```

- [ ] **Step 2: Run eth integration tests**

Run:
```bash
cd build-bcos-evm-check && ctest -R 'InnerExecute|StateTransitionExecute|EthFeeSettlementState' -V
```
Expected: PASS

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/apply/ApplyEthMessage.cpp
rtk git commit -m "$(cat <<'EOF'
feat(eth): orchestrate fee settlement inside applyEthMessage

Move buyGas/refund lifecycle into apply layer using State checkpoints;
eth_call skips coordinator.
EOF
)"
```

---

### Task 7: Slim EthTransactionExecutorImpl

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`

**Interfaces:**
- Consumes: extended `EthMessageResult`（gasUsed, effectiveGasPrice, gasPriceStr, unified stateDiff）
- Produces: TE without `m_txExecutor.buyGas/refundGas`, without `m_afterBuyGasSavepoint`

- [ ] **Step 1: Remove EthTxFeeSettlement template parameter and fee calls**

关键变更：

1. 删除 `#include "bcos-evm/eth/apply/EthTxFeeSettlement.h"`
2. 删除 `TxExec m_txExecutor` 及 template 参数
3. `Execute` phase 改为：

```cpp
auto output = co_await applyEthMessageTx();
m_data->m_message = std::move(output.message);
m_data->m_revisionConfig = std::move(output.revisionConfig);
m_data->m_receiptLogs = std::move(output.receiptLogs);
m_data->m_gasSettlementSnapshot = output.gasSettlementSnapshot;
m_data->m_evmcResult.emplace(std::move(output.evmcResult));
m_data->m_gasUsed = output.gasUsed;
m_data->m_effectiveGasPrice = output.effectiveGasPrice;
m_data->m_gasPriceStr = std::move(output.gasPriceStr);
m_data->m_topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;

// 门控收敛为 !m_call && !empty（spec §5.9 / Q2）：
// pre-exec reject 时 coordinator 已 revert，diff 空自然跳过；
// vmerr 时 kernel 已回退 EVM，diff 仅含 fee + pre-frame，应落账；eth_call 不落账。
if (!m_data->m_call && !output.stateDiff.accounts.empty())
{
    co_await state::applyStateDiff(m_data->m_rollbackableStorage, output.stateDiff,
        false, *m_data->m_executor.get().m_hashImpl, m_data->m_transaction.get().abi());
}
// 删除 settleGasUsedFromEvmResult() — gasUsed 来自 applyEthMessage
// 删除 buyGas / refundGas 调用
```

4. `applyEthMessageTx` 填充新字段：

```cpp
input.isCall = m_data->m_call;
input.txValue = u256(m_data->m_transaction.get().value());
// 【不填 isWeb3】：7623 gate 由 IntrinsicDebitMode（hooks）→ snapshot 表达（ADR-005）
```

5. `makeReceipt` 保留在 TE（从 `EthTxFeeSettlement` 内联搬迁为 private 方法 `makeReceiptFromData`）

- [ ] **Step 2: Run TE 1559 regression**

Run:
```bash
cd build-bcos-evm-check && cmake --build . --target EthTxFeeLedger1559Test -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk test ./transaction-executor/tests/EthTxFeeLedger1559Test
```
Expected: PASS（E2E 仍通过 EVMAccount **读** storage 验证余额）

- [ ] **Step 3: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h
rtk git commit -m "$(cat <<'EOF'
refactor(te): delegate Eth fee lifecycle to applyEthMessage

Single applyStateDiff per tx; remove TE-layer buyGas/refundGas and
storage savepoint for fee/EVM split.
EOF
)"
```

---

### Task 8: Remove legacy adapter + CMake ledger link

**Files:**
- Delete: `bcos-evm/eth/apply/EthTxFeeSettlement.h`
- Modify: `bcos-evm/include/bcos-evm/eth_executor.hpp`
- Modify: `bcos-evm/CMakeLists.txt`
- Modify: `tools/ci/check-eth-pr-slices.sh`
- Modify: `bcos-evm/eth/README.md`
- Modify: `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md`

- [ ] **Step 1: Delete EthTxFeeSettlement.h and fix includes**

`eth_executor.hpp` 改为：
```cpp
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
```

- [ ] **Step 2: Remove ledger from bcos-evm-eth CMake**

`bcos-evm/CMakeLists.txt`：
```cmake
target_link_libraries(bcos-evm-eth PUBLIC
    evmone::evmone
    bcos-task
    bcos-framework
    bcos-protocol
    bcos-utilities
    bcos-crypto
    codec
)
```

- [ ] **Step 3: Verify zero ledger includes in eth/**

Run:
```bash
rtk grep -r 'bcos-framework/ledger\|bcos-ledger' bcos-evm/eth/
```
Expected: **无输出**

Run:
```bash
rtk grep -r 'bcos-framework/ledger\|bcos-ledger' bcos-evm/opstack/
```
Expected: **无输出**（opstack 已 State-first）

- [ ] **Step 4: Update check-eth-pr-slices.sh**

将 `EthTxFeeSettlement.h` 替换为：
```
EthFeeSettlement.h
EthFeeSettlement.cpp
EthNormalTxFeeCoordinator.h
EthNormalTxFeeCoordinator.cpp
EthTxFinalize.h
EthTxFinalize.cpp
EthSettlementProjection.h
EthFeeSidecar.h
```

- [ ] **Step 5: Full eth test sweep**

Run:
```bash
cd build-bcos-evm-check && ctest -R 'Eth|TxFeeSettlement|InnerExecute|StateTransition' --output-on-failure
rtk test ./transaction-executor/tests/EthTxFeeLedger1559Test
rtk test ./bcos-evm/test/cross/FeeSettlementCharacterizationTest
```
Expected: PASS

- [ ] **Step 6: Commit**

```bash
rtk git add -u bcos-evm/ tools/ci/check-eth-pr-slices.sh
rtk git commit -m "$(cat <<'EOF'
refactor(eth): drop EthTxFeeSettlement and bcos-evm-eth ledger link

Eth reference fee path is State-only; ledger remains in bcos/ storage
adapter by design.
EOF
)"
```

---

### Task 9: Documentation + ADR table update

**Files:**
- Modify: `bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md` §2 adapter 表
- Modify: `bcos-evm/eth/README.md` settlement 目录

- [ ] **Step 1: Update ADR-026 adapter row**

| Path | Applies |
| --- | --- |
| `EthFeeSettlement` | sync `ctx.state`; burn base; penalty on buyGas fail |

- [ ] **Step 2: Add eth/settlement/ section to eth/README.md**

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/docs/adr/026-tx-fee-settlement-deepening.md bcos-evm/eth/README.md
rtk git commit -m "$(cat <<'EOF'
docs(eth): record State-based EthFeeSettlement in ADR-026
EOF
)"
```

---

## Self-Review

**1. Spec coverage**

| 要求（spec） | Task |
|------|------|
| eth/ 无 ledger include | Task 1, 8 |
| State buyGas/refund | Task 3 |
| penalty 保留 + 不 revert | Task 3 test + impl |
| 单层 pre-buyGas checkpoint（Q3） | Task 6 |
| vmerr/included-vmerr：fee 层不 revert/commit（kernel 负责，§1.3） | Task 5 `completeAfterPipeline` |
| pre-exec reject 单层 revert buyGas（ADR-025） | Task 5 |
| `finalizeEthNormal` 无 isWeb3（ADR-005）；7623 gate 用 `eip7623 && snapshot>0` | Task 4 |
| `ctx.gasPrice` 注入（§5.5） | Task 6 |
| `topLevelIncludedTxVmError` 传播（§5.6） | Task 5 + Task 7 |
| TE 单次 applyStateDiff，门控 `!m_call && !empty` | Task 7 |
| CMake 去 ledger link | Task 8 |
| OpStack 不受影响 | 无 opstack 改动 |
| FISCO / ADR-028 follow-up | Scope Check + Follow-up 标明 |

**2. Placeholder scan:** 无 TBD/TODO/“similar to Task N”。

**3. Type consistency:** `EthTxFinalizeResult`、`EthSettlementProjection`、`EthMessageResult` 字段在各 Task Interfaces 块一致。

**Gap:** `makeReceipt` 从 `EthTxFeeSettlement` 迁至 TE private — Task 7 Step 1 需内联完整 `makeReceipt` 实现（从现 `EthTxFeeSettlement.h:134-168` 复制，无 ledger 依赖）。

---

## Follow-up（独立计划，不在本 PR）

- **ADR-028 共识拒绝语义（spec Q4，defer）：** `RulesRejected` 入 abort 集、`consensusOutcome` 传播、Finalize nullptr 等，建于本 State-first 基础之上。
- `bcos/FiscoTxFeeSettlement.h` → State 或统一 `StateDiffApplier` 单通道
- `bcos/FiscoBlockInfo.h` 对 `bcos-ledger/LedgerMethods.h` 的依赖 — 考虑 block hash port 注入

---

Plan complete and saved to `docs/superpowers/plans/2026-07-03-eth-fee-settlement-remove-ledger-dependency.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派 fresh subagent，Task 间 review，迭代快

**2. Inline Execution** — 本会话用 executing-plans 批量执行，checkpoint Review

Which approach?

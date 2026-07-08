# OpStack Jovian DA Footprint (Receipt Phase) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 OpStack 执行层补齐 Jovian 单笔 receipt 的 `daFootprintGasScalar` 与 `blobGasUsed`（per-tx DA footprint），与 op-geth `deriveOPStackFields` 单笔公式对齐。

**Architecture:** 抽出与 op-geth 一致的 `estimatedDASize` 公式（有符号中间量）；执行时在 `projectNormalReceiptMeta`（普通 tx 非拒绝路径）按 Jovian 门控写入 `OpStackReceiptMeta`；经 `TransactionReceipt` sidecar + TE `makeReceipt` 暴露。不触碰块级 / 出块 / RPC。

**Tech Stack:** C++20，evmone，Boost.Test，CMake/CTest，bcos-evm-op / bcos-tars-protocol / transaction-executor。

**Spec:** `docs/superpowers/specs/2026-07-01-opstack-jovian-da-footprint-design.md`

## Global Constraints

- **仅 Receipt 阶段（Phase 1）**：不改 `beginBlock`/`endBlock`、block header、`ExecutionPayload`、PBFT sealer、bcos-rpc。
- **Jovian 门控**：仅当 `isOpStackJovian(schedule, blockTime)` 为 true 且**非 deposit** 时写入；pre-Jovian / deposit **零行为变化**。
- **恒写**：Jovian + 非 deposit 恒写两字段（即便 scalar=0 → 写 `0x0`），对齐 op-geth `receipt_opstack.go:48-51` 无条件赋值。
- **有符号中间量**：DA size 公式中间量必须用 `s256`（`L1_COST_INTERCEPT = -42'585'600` 为负），**禁止** `uint64_t` 直接算（否则 underflow 成天文数字）。
- **sidecar 不入序列化**：新字段为执行期内存字段，与 `l1Fee` / `depositReceiptVersion` 一致，不进 tars `encode`/`decode`。
- **op-geth 基线**：v1.101702.2 @ `e8800cffe`，`/Users/octopus/octo/code/blockchain-impl/op-geth`。
- **公式定义**：`estimatedDASizeScaled = max(MIN_TX_SIZE_SCALED, L1_COST_INTERCEPT + L1_COST_FASTLZ_COEF × fastLzSize)`；`estimatedDASize = estimatedDASizeScaled / 1'000'000`；`daFootprint = estimatedDASize × daFootprintGasScalar`。

---

## File Structure

| 文件 | 职责 | 动作 |
|------|------|------|
| `bcos-evm/opstack/fee/RollupCost.h` | 声明 `estimatedDASizeScaled` / `estimatedDASize` | Modify |
| `bcos-evm/opstack/fee/RollupCost.cpp` | 实现二者 | Modify |
| `bcos-evm/opstack/fee/OpStackFeeParams.cpp` | `l1CostFjord` 复用 `estimatedDASizeScaled`；`loadOpStackFeeParams` 读 DA scalar | Modify |
| `bcos-evm/opstack/fee/OpStackFeeParams.h` | `OpStackFeeParams` 加 `daFootprintGasScalar` 字段 | Modify |
| `bcos-evm/opstack/types/OpStackReceiptMeta.h` | 加 `daFootprintGasScalar` / `daFootprint` | Modify |
| `bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.cpp` | `projectNormalReceiptMeta` 写入（Jovian 门控） | Modify |
| `bcos-framework/bcos-framework/protocol/TransactionReceipt.h` | sidecar 接口 + 缓冲常量 | Modify |
| `bcos-tars-protocol/.../TransactionReceiptImpl.h/.cpp` | 成员 + getter/setter + static_assert | Modify |
| `bcos-tars-protocol/.../TransactionReceiptFactoryImpl.cpp` | 拷贝路径 | Modify |
| `transaction-executor/.../OpStackTransactionExecutorImpl.h` | `makeReceipt` 写 hex | Modify |
| `bcos-evm/test/opstack/EstimatedDASizeTest.cpp` | 公式单测 | Create |
| `bcos-evm/test/opstack/DaFootprintReceiptTest.cpp` | meta 写入分支单测 | Create |
| `bcos-evm/test/cmake/OpStackTests.cmake` | 注册两个新测试 | Modify |

**已知风险（不阻塞本 plan 前 4 个 Task）：** `test-opstack-transaction-executor-fixture` 当前因无关的 `bcos-evm/eth/eip/TxIntrinsicGas.h` 缺失而无法构建（见工作区未提交的 StateTransitionContext 重构）。Task 6 的 TE fixture 依赖它；若构建仍失败，Task 6 的断言改为在 `bcos-evm` 层（`applyOpStackMessage` 输出的 `receiptMeta`）验证，并在 commit 说明中标注 TE 层验证阻塞。

---

## Task 1: `estimatedDASize` 公式抽出与复用

**Files:**
- Modify: `bcos-evm/opstack/fee/RollupCost.h`
- Modify: `bcos-evm/opstack/fee/RollupCost.cpp`
- Modify: `bcos-evm/opstack/fee/OpStackFeeParams.cpp:106-113`
- Create: `bcos-evm/test/opstack/EstimatedDASizeTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Produces:
  - `bcos::s256 bcos::evm::estimatedDASizeScaled(RollupCostData const& data) noexcept;`
  - `uint64_t bcos::evm::estimatedDASize(RollupCostData const& data) noexcept;`

- [ ] **Step 1: 写失败测试**

Create `bcos-evm/test/opstack/EstimatedDASizeTest.cpp`:

```cpp
#define BOOST_TEST_MODULE EstimatedDASizeTest

#include "bcos-evm/opstack/fee/RollupCost.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
// op-geth EstimatedDASize = max(MIN_TX_SIZE_SCALED, INTERCEPT + FASTLZ_COEF*fastLzSize) / 1e6
// MIN_TX_SIZE_SCALED = 100'000'000; INTERCEPT = -42'585'600; FASTLZ_COEF = 836'500
BOOST_AUTO_TEST_CASE(small_fastlz_floors_to_min)
{
    // fastLzSize=0 -> intercept negative -> floored to MIN -> 100'000'000/1e6 = 100
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 0}), 100u);
    // fastLzSize=64 -> -42'585'600 + 53'536'000 = 10'950'400 < MIN -> 100
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 64}), 100u);
}

BOOST_AUTO_TEST_CASE(large_fastlz_exceeds_min)
{
    // fastLzSize=200 -> 836'500*200 - 42'585'600 = 124'714'400 -> /1e6 = 124
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 200}), 124u);
}

BOOST_AUTO_TEST_CASE(scaled_is_not_divided)
{
    // scaled 版本保留 1e6 放大（供 l1CostFjord 复用），fastLzSize=200 -> 124'714'400
    BOOST_CHECK_EQUAL(estimatedDASizeScaled(RollupCostData{.fastLzSize = 200}), s256(124'714'400));
    // 负中间量被 MIN 兜底（验证有符号，防 uint64 underflow）
    BOOST_CHECK_EQUAL(estimatedDASizeScaled(RollupCostData{.fastLzSize = 0}), s256(100'000'000));
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: 注册测试并确认构建失败**

在 `bcos-evm/test/cmake/OpStackTests.cmake` 末尾（`add_test(... DepositMint ...)` 同款结构）追加：

```cmake
set(ESTIMATED_DA_SIZE_TEST_BINARY_NAME EstimatedDASizeTest)

add_executable(${ESTIMATED_DA_SIZE_TEST_BINARY_NAME}
    opstack/EstimatedDASizeTest.cpp
)

target_include_directories(${ESTIMATED_DA_SIZE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${ESTIMATED_DA_SIZE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME EstimatedDASize
    COMMAND ${ESTIMATED_DA_SIZE_TEST_BINARY_NAME}
)
```

Run: `cd build && cmake --build . --target EstimatedDASizeTest 2>&1 | tail -5`
Expected: FAIL — `estimatedDASize` / `estimatedDASizeScaled` 未声明。

- [ ] **Step 3: 声明函数**

在 `bcos-evm/opstack/fee/RollupCost.h` 的 `newRollupCostData` 声明之后、`}  // namespace` 之前加入：

```cpp
// Fjord 线性回归估算 tx 在 DA batch 中的字节数（放大 1e6）。中间量用 s256：
// L1_COST_INTERCEPT 为负，fastLzSize 小时中间和为负，随后由 MIN_TX_SIZE_SCALED 兜底。
bcos::s256 estimatedDASizeScaled(RollupCostData const& data) noexcept;

// = estimatedDASizeScaled / 1'000'000（对齐 op-geth RollupCostData.EstimatedDASize）。
uint64_t estimatedDASize(RollupCostData const& data) noexcept;
```

确认 `RollupCost.h` 顶部已 `#include <bcos-utilities/Common.h>`（提供 `s256`）——已存在（当前第 3 行）。

- [ ] **Step 4: 实现函数**

在 `bcos-evm/opstack/fee/RollupCost.cpp` 的 `newRollupCostData` 之后、`}  // namespace bcos::evm` 之前加入：

```cpp
bcos::s256 estimatedDASizeScaled(RollupCostData const& data) noexcept
{
    s256 scaled = s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(data.fastLzSize);
    if (scaled < s256(MIN_TX_SIZE_SCALED))
    {
        scaled = s256(MIN_TX_SIZE_SCALED);
    }
    return scaled;
}

uint64_t estimatedDASize(RollupCostData const& data) noexcept
{
    return static_cast<uint64_t>(estimatedDASizeScaled(data) / s256(1'000'000));
}
```

在 `RollupCost.cpp` 顶部 include 区加入（若尚无）：

```cpp
#include "bcos-evm/opstack/policy/OpStackConstants.h"
```

（`L1_COST_INTERCEPT` 等常量定义于 `OpStackConstants.h`。）

- [ ] **Step 5: 运行测试确认通过**

Run: `cd build && cmake --build . --target EstimatedDASizeTest 2>&1 | tail -3 && ./bcos-evm/test/EstimatedDASizeTest`
Expected: PASS，`*** No errors detected`。

- [ ] **Step 6: 复用到 `l1CostFjord`（消除重复公式）**

在 `bcos-evm/opstack/fee/OpStackFeeParams.cpp` 将 `l1CostFjord` 内联的 estimatedSize 计算（106-113 行）替换为调用：

```cpp
    auto const scaled = estimatedDASizeScaled(data);
    return u256(scaled) * l1FeeScaled / u256(FJORD_DIVISOR);
```

即删除原 `s256 estimatedSize = ...; if (...) {...}` 三段，替换为上面两行（保留其上的 `l1FeeScaled` 计算不动）。`OpStackFeeParams.cpp` 已 `#include "bcos-evm/opstack/fee/RollupCost.h"`（当前第 11 行经由 `OpStackFeeParams.h`）——确认可见，否则补 include。

- [ ] **Step 7: 运行既有 L1 fee 回归确认无漂移**

Run: `cd build && cmake --build . --target OpStackFeeTest EstimatedDASizeTest 2>&1 | tail -3 && ./bcos-evm/test/OpStackFeeTest && ./bcos-evm/test/EstimatedDASizeTest`
Expected: 两者均 PASS（`l1CostFjord` 数值不变）。

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/opstack/fee/RollupCost.h bcos-evm/opstack/fee/RollupCost.cpp bcos-evm/opstack/fee/OpStackFeeParams.cpp bcos-evm/test/opstack/EstimatedDASizeTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "feat(opstack): extract estimatedDASize with signed intermediate and reuse in l1CostFjord"
```

---

## Task 2: `OpStackFeeParams` 读取 DA footprint scalar

**Files:**
- Modify: `bcos-evm/opstack/fee/OpStackFeeParams.h:21-29`
- Modify: `bcos-evm/opstack/fee/OpStackFeeParams.cpp:140-164`
- Modify: `bcos-evm/test/opstack/OpStackFeeTest.cpp`

**Interfaces:**
- Produces: `OpStackFeeParams::daFootprintGasScalar`（`bcos::u256`），`loadOpStackFeeParams` 填充之。
- Consumes: `unpackDaFootprintGasScalar(evmc_bytes32)`（已存在 `L1BlockStorage.h:47`）。

- [ ] **Step 1: 写失败测试**

在 `bcos-evm/test/opstack/OpStackFeeTest.cpp` 的 `namespace bcos::evm::test { ... }` 内、末尾 `}  // namespace ...` 之前追加。**复用本文件已有的 `MockStateView`（line 57-84，带 `.setSlot`）与 `packOperatorFeeParams`**（该文件已 include `L1BlockStorage.h` / `OpStackConstants.h`，无需新增头）：

```cpp
BOOST_AUTO_TEST_CASE(loadOpStackFeeParams_reads_da_footprint_scalar)
{
    MockStateView state;
    // packOperatorFeeParams 的第 3 参把 daFootprintGasScalar 写入 slot 的 bytes[18:20)。
    // scalar 非零 → loadOpStackFeeParams 不会在 isZeroBytes32 处提前返回。
    state.setSlot(OPERATOR_FEE_PARAMS_SLOT,
        packOperatorFeeParams(/*operatorScalar*/ 1'439'103'868,
            /*operatorConstant*/ 1'256'417'826'609'331'460ULL, /*daFootprintGasScalar*/ 400));

    auto const params = loadOpStackFeeParams(state);
    BOOST_CHECK_EQUAL(params.daFootprintGasScalar, u256(400));
}
```

> 校验依据：`MockStateView` 的 `get_storage` 只对 `OP_L1_BLOCK_PREDEPLOY` 返回已设槽位（本文件 line 67-80）；`loadOpStackFeeParams` 吃 `state::StateView const&`（`OpStackFeeParams.cpp:140`），`MockStateView` 正是 `StateView` 子类。`packOperatorFeeParams` 第 3 参 `uint16_t daFootprintGasScalar` 默认 0（`L1BlockStorage.h:38`）。

- [ ] **Step 2: 运行确认失败**

Run: `cd build && cmake --build . --target OpStackFeeTest 2>&1 | tail -5`
Expected: FAIL — `OpStackFeeParams` 无 `daFootprintGasScalar` 成员。

- [ ] **Step 3: 加字段**

在 `bcos-evm/opstack/fee/OpStackFeeParams.h` 的 `OpStackFeeParams` 结构末尾（`operatorFeeConstant` 之后）加：

```cpp
    u256 daFootprintGasScalar;
```

- [ ] **Step 4: 填充字段**

在 `bcos-evm/opstack/fee/OpStackFeeParams.cpp` 的 `loadOpStackFeeParams` 中，`operatorFeeParams` 读取之后填充。将 155-163 行改为：

```cpp
    auto const operatorFeeParams = readSlot(OPERATOR_FEE_PARAMS_SLOT);
    if (state::isZeroBytes32(operatorFeeParams))
    {
        return params;
    }

    params.operatorFeeScalar = unpackOperatorFeeScalar(operatorFeeParams);
    params.operatorFeeConstant = unpackOperatorFeeConstant(operatorFeeParams);
    params.daFootprintGasScalar = unpackDaFootprintGasScalar(operatorFeeParams);
    return params;
```

确认 `OpStackFeeParams.cpp` 已 include `L1BlockStorage.h`（提供 `unpackDaFootprintGasScalar`）；若无则在 include 区加 `#include "bcos-evm/opstack/l1/L1BlockStorage.h"`。

- [ ] **Step 5: 运行确认通过**

Run: `cd build && cmake --build . --target OpStackFeeTest 2>&1 | tail -3 && ./bcos-evm/test/OpStackFeeTest`
Expected: PASS。

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/fee/OpStackFeeParams.h bcos-evm/opstack/fee/OpStackFeeParams.cpp bcos-evm/test/opstack/OpStackFeeTest.cpp
rtk git commit -m "feat(opstack): load daFootprintGasScalar into OpStackFeeParams"
```

---

## Task 3: `OpStackReceiptMeta` 字段 + `projectNormalReceiptMeta` 写入

**Files:**
- Modify: `bcos-evm/opstack/types/OpStackReceiptMeta.h`
- Modify: `bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.cpp:18-34`
- Create: `bcos-evm/test/opstack/DaFootprintReceiptTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Consumes: `estimatedDASize` (Task 1)、`OpStackFeeParams::daFootprintGasScalar` (Task 2)、`isOpStackJovian` (`OpStackForkSchedule.h:40`)、`view.rollupCostData()` (`OpStackSettlementProjection.h:47`)、`view.blockInfo()`、`view.input.forkSchedule`。
- Produces: `OpStackReceiptMeta::daFootprintGasScalar`、`OpStackReceiptMeta::daFootprint`（均 `std::optional<uint64_t>`）。

- [ ] **Step 1: 写失败测试**

Create `bcos-evm/test/opstack/DaFootprintReceiptTest.cpp`（结构参照 `L1AttributesDepositTest.cpp`：先跑 jovian setter deposit 写 slot，再跑 user tx；断言 `receiptMeta`）：

```cpp
#define BOOST_TEST_MODULE DaFootprintReceiptTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/ApplyStateDiffToView.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <fstream>

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

bytes loadFixture(std::string_view name)
{
    auto const path = std::string(OPSTACK_FIXTURES_DIR) + "/" + std::string(name);
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(input.is_open(), "missing fixture: " << path);
    return {std::istreambuf_iterator<char>(input), {}};
}

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

// 跑 Jovian L1 attributes deposit，把 daFootprintGasScalar=400 写入 L1Block slot。
void seedJovianL1Block(state::test::InMemoryStateView& stateView, evmc::VM& vm, crypto::Hash& hash)
{
    stateView.insert_account(OP_DEPOSITOR_ACCOUNT, state::Account{.nonce = 0});
    auto calldata = loadFixture("jovian_l1_attributes.bin");
    evmc_message m{};
    m.kind = EVMC_CALL;
    m.gas = 500'000;
    m.sender = OP_DEPOSITOR_ACCOUNT;
    m.recipient = OP_L1_BLOCK_PREDEPLOY;
    m.code_address = OP_L1_BLOCK_PREDEPLOY;
    m.input_data = calldata.data();
    m.input_size = calldata.size();

    OpStackMessageRequest in;
    in.stateView = &stateView;
    in.vm = &vm;
    in.hashImpl = &hash;
    in.message = m;
    in.blockInfo.baseFee = 1;
    in.gasTipCap = 1;
    in.gasFeeCap = 1;
    in.forkSchedule = makeJovianPlusForkSchedule();
    in.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    in.depositTx =
        OpStackDepositTx{.from = OP_DEPOSITOR_ACCOUNT, .to = OP_L1_BLOCK_PREDEPLOY, .gas = 500'000};
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);
    applyStateDiffToView(out.stateDiff, stateView);
}

OpStackMessageRequest makeUserTx(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    crypto::Hash& hash, OpStackForkSchedule schedule)
{
    auto const user = addressFromLastByte(0x71);
    auto const target = addressFromLastByte(0x72);
    stateView.insert_account(
        user, state::Account{.balance = u256("1000000000000000000000000000000"), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    evmc_message m{};
    m.kind = EVMC_CALL;
    m.gas = 100'000;
    m.sender = user;
    m.recipient = target;
    m.code_address = target;

    OpStackMessageRequest in;
    in.stateView = &stateView;
    in.vm = &vm;
    in.hashImpl = &hash;
    in.message = m;
    in.blockInfo.baseFee = 1;
    in.gasTipCap = 1;
    in.gasFeeCap = 2;
    in.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
    in.txProps.warmDestination = true;
    in.forkSchedule = schedule;
    in.rollupCostData = RollupCostData{.ones = 8, .fastLzSize = 200};
    return in;
}
}  // namespace

// Jovian + 非 deposit：恒写；scalar=400、fastLzSize=200 -> estimatedDASize=124 -> footprint=49600
BOOST_AUTO_TEST_CASE(jovian_user_tx_writes_da_footprint)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    seedJovianL1Block(stateView, vm, hash);

    auto in = makeUserTx(stateView, vm, hash, makeJovianPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_REQUIRE(out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprintGasScalar, 400u);
    BOOST_REQUIRE(out.receiptMeta.daFootprint.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprint, 124u * 400u);
}

// pre-Jovian（Isthmus 默认）：不写
BOOST_AUTO_TEST_CASE(pre_jovian_user_tx_omits_da_footprint)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto in = makeUserTx(stateView, vm, hash, makeIsthmusPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_CHECK(!out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK(!out.receiptMeta.daFootprint.has_value());
}

// Jovian 但 L1Block 未写 scalar（=0）：恒写 0
BOOST_AUTO_TEST_CASE(jovian_zero_scalar_writes_zero)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto in = makeUserTx(stateView, vm, hash, makeJovianPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_REQUIRE(out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprintGasScalar, 0u);
    BOOST_REQUIRE(out.receiptMeta.daFootprint.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprint, 0u);
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: 注册测试并确认失败**

在 `bcos-evm/test/cmake/OpStackTests.cmake` 末尾追加（同 Task 1 结构）：

```cmake
set(DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME DaFootprintReceiptTest)

add_executable(${DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME}
    opstack/DaFootprintReceiptTest.cpp
)

target_include_directories(${DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME} PRIVATE
    OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)

target_link_libraries(${DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME DaFootprintReceipt
    COMMAND ${DA_FOOTPRINT_RECEIPT_TEST_BINARY_NAME}
)
```

> `OPSTACK_FIXTURES_DIR` 定义方式对照现有 `L1AttributesDepositTest` 目标（在同 cmake 文件内 grep `OPSTACK_FIXTURES_DIR` 复制其写法；若已有全局定义则删去本 Step 的 `target_compile_definitions`）。

Run: `cd build && cmake --build . --target DaFootprintReceiptTest 2>&1 | tail -6`
Expected: FAIL — `receiptMeta.daFootprintGasScalar` / `daFootprint` 无此成员。

- [ ] **Step 3: 加 meta 字段**

在 `bcos-evm/opstack/types/OpStackReceiptMeta.h` 的 `depositReceiptVersion` 之后加：

```cpp
    std::optional<uint64_t> daFootprintGasScalar;
    std::optional<uint64_t> daFootprint;  // op-geth Receipt.BlobGasUsed 的 Jovian 语义
```

- [ ] **Step 4: 在 `projectNormalReceiptMeta` 写入**

在 `bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.cpp` 的 `projectNormalReceiptMeta`（18-34 行）末尾、`}` 之前加入：

```cpp
    if (isOpStackJovian(input.forkSchedule, view.blockInfo().timestamp))
    {
        auto const scalar = static_cast<uint64_t>(feeParams.daFootprintGasScalar);
        output.receiptMeta.daFootprintGasScalar = scalar;
        auto const& rollup = view.rollupCostData();
        auto const size = rollup.has_value() ? estimatedDASize(*rollup) : 0;
        output.receiptMeta.daFootprint = size * scalar;
    }
```

在该文件 include 区加入（若缺）：

```cpp
#include "bcos-evm/opstack/fee/RollupCost.h"
```

（`isOpStackJovian` 来自已 include 的 `OpStackForkSchedule.h`；`estimatedDASize` 来自 `RollupCost.h`。deposit tx 不经 `projectNormalReceiptMeta`，天然不写。）

- [ ] **Step 5: 运行确认通过**

Run: `cd build && cmake --build . --target DaFootprintReceiptTest 2>&1 | tail -3 && ./bcos-evm/test/DaFootprintReceiptTest`
Expected: PASS（3 个用例）。

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/types/OpStackReceiptMeta.h bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.cpp bcos-evm/test/opstack/DaFootprintReceiptTest.cpp bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "feat(opstack): populate Jovian DA footprint receipt meta on normal txs"
```

---

## Task 4: `TransactionReceipt` sidecar（接口 + impl + factory + 缓冲）

**Files:**
- Modify: `bcos-framework/bcos-framework/protocol/TransactionReceipt.h`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.cpp`
- Modify: `bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.cpp`

**Interfaces:**
- Produces（`TransactionReceipt` 纯虚 + `TransactionReceiptImpl` 实现）:
  - `std::optional<std::string> daFootprintGasScalar() const`
  - `void setDaFootprintGasScalar(std::string)`
  - `std::optional<std::string> blobGasUsed() const`
  - `void setBlobGasUsed(std::string)`

- [ ] **Step 1: 加纯虚接口**

在 `bcos-framework/bcos-framework/protocol/TransactionReceipt.h` 的 `setDepositReceiptVersion` 之后加：

```cpp
    virtual std::optional<std::string> daFootprintGasScalar() const = 0;
    virtual void setDaFootprintGasScalar(std::string daFootprintGasScalar) = 0;
    virtual std::optional<std::string> blobGasUsed() const = 0;  // Jovian: per-tx DA footprint
    virtual void setBlobGasUsed(std::string blobGasUsed) = 0;
```

- [ ] **Step 2: 提升缓冲常量**

在同文件将 `AnyHolder<TransactionReceipt, 272>` 改为 `AnyHolder<TransactionReceipt, 336>`：

```cpp
using AnyTransactionReceipt =
    AnyHolder<TransactionReceipt, 336>;  // 多平台TransactionReceiptImpl的最大尺寸 (Maximum size of
                                         // TransactionReceiptImpl across platforms)
```

- [ ] **Step 3: 加 impl 声明与成员**

在 `TransactionReceiptImpl.h` 的 `setDepositReceiptVersion` 声明之后加：

```cpp
    std::optional<std::string> daFootprintGasScalar() const override;
    void setDaFootprintGasScalar(std::string daFootprintGasScalar) override;
    std::optional<std::string> blobGasUsed() const override;
    void setBlobGasUsed(std::string blobGasUsed) override;
```

在 private 成员区 `m_depositReceiptVersion` 之后加：

```cpp
    std::optional<std::string> m_daFootprintGasScalar;
    std::optional<std::string> m_blobGasUsed;
```

同文件 `static_assert(sizeof(TransactionReceiptImpl) <= 272, ...)` 改为 `<= 336`，并同步更新括号内提示文字里的 272 → 336。

- [ ] **Step 4: 加 impl 定义**

在 `TransactionReceiptImpl.cpp` 的 `setDepositReceiptVersion` 定义之后加：

```cpp
std::optional<std::string> bcostars::protocol::TransactionReceiptImpl::daFootprintGasScalar() const
{
    return m_daFootprintGasScalar;
}
void bcostars::protocol::TransactionReceiptImpl::setDaFootprintGasScalar(
    std::string daFootprintGasScalar)
{
    m_daFootprintGasScalar = std::move(daFootprintGasScalar);
}
std::optional<std::string> bcostars::protocol::TransactionReceiptImpl::blobGasUsed() const
{
    return m_blobGasUsed;
}
void bcostars::protocol::TransactionReceiptImpl::setBlobGasUsed(std::string blobGasUsed)
{
    m_blobGasUsed = std::move(blobGasUsed);
}
```

- [ ] **Step 5: 加 factory 拷贝路径**

在 `TransactionReceiptFactoryImpl.cpp` 的 `createReceipt(TransactionReceipt& input)` 里，`depositReceiptVersion` 拷贝块之后、`return receipt;` 之前加：

```cpp
    if (auto daFootprintGasScalar = input.daFootprintGasScalar(); daFootprintGasScalar.has_value())
    {
        receipt->setDaFootprintGasScalar(*daFootprintGasScalar);
    }
    if (auto blobGasUsed = input.blobGasUsed(); blobGasUsed.has_value())
    {
        receipt->setBlobGasUsed(*blobGasUsed);
    }
```

- [ ] **Step 6: 构建确认无编译错误 + static_assert 通过**

Run: `cd build && cmake --build . --target protocol-tars 2>&1 | tail -5`
Expected: 构建成功；无 `static_assert` 触发。若 static_assert 报「exceeds 336」，按报错实测 sizeof 向上取整调 Step 2/Step 3 的常量后重编。

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-framework/bcos-framework/protocol/TransactionReceipt.h bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.cpp bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.cpp
rtk git commit -m "feat(protocol): add daFootprintGasScalar and blobGasUsed receipt sidecar fields"
```

---

## Task 5: TE `makeReceipt` 写入 hex

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h:296-301`

**Interfaces:**
- Consumes: `OpStackReceiptMeta::daFootprintGasScalar` / `daFootprint` (Task 3)、`TransactionReceipt::setDaFootprintGasScalar` / `setBlobGasUsed` (Task 4)。

- [ ] **Step 1: 加 wiring**

在 `OpStackTransactionExecutorImpl.h` 的 `makeReceipt` 内，`depositReceiptVersion` 写入块（296-300 行）之后、`co_return receipt;` 之前加：

```cpp
            if (m_data->m_receiptMeta.daFootprintGasScalar.has_value())
            {
                auto const scalar = bcos::u256(*m_data->m_receiptMeta.daFootprintGasScalar);
                receipt->setDaFootprintGasScalar("0x" + scalar.str(0, std::ios_base::hex));
            }
            if (m_data->m_receiptMeta.daFootprint.has_value())
            {
                auto const footprint = bcos::u256(*m_data->m_receiptMeta.daFootprint);
                receipt->setBlobGasUsed("0x" + footprint.str(0, std::ios_base::hex));
            }
```

- [ ] **Step 2: 构建确认通过**

Run: `cd build && cmake --build . --target transaction-executor 2>&1 | tail -5`
Expected: 构建成功。

> 若因无关的 `bcos-evm/eth/eip/TxIntrinsicGas.h` 缺失导致 `transaction-executor` 无法构建，记录该阻塞（见 File Structure 风险注），本 Task 的验证顺延到 Task 6，并在 commit message 注明「TE 层构建受既有 worktree 问题阻塞」。

- [ ] **Step 3: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h
rtk git commit -m "feat(te): wire Jovian DA footprint fields into opstack makeReceipt"
```

---

## Task 6: TE fixture E2E 断言（deposit 不写 + user tx 写）

**Files:**
- Modify: `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

**Interfaces:**
- Consumes: `receipt->daFootprintGasScalar()`、`receipt->blobGasUsed()` (Task 4/5)。

- [ ] **Step 1: 加 deposit 不写断言（复用现有 deposit 用例）**

在 `TestOpStackTransactionExecutorFixture.cpp` 的 `l1_attributes_deposit_via_te` 用例中，`depositReceiptVersion` 断言之后加（deposit receipt 不得有 footprint 字段）：

```cpp
        BOOST_CHECK(!receipt->daFootprintGasScalar().has_value());
        BOOST_CHECK(!receipt->blobGasUsed().has_value());
```

- [ ] **Step 2: 构建并运行**

Run: `cd build && cmake --build . --target test-opstack-transaction-executor-fixture 2>&1 | tail -5 && ./transaction-executor/tests/test-opstack-transaction-executor-fixture --run_test="*deposit*" 2>&1 | tail -10`
Expected: PASS。

> 若构建因既有 `TxIntrinsicGas.h` 问题失败：跳过本 Step 的运行，保留断言代码，在 commit message 注明「等 worktree TxIntrinsicGas 修复后启用」；Task 3 的 `DaFootprintReceiptTest` 已在 bcos-evm 层覆盖核心行为，plan 的功能验证不受阻。

- [ ] **Step 3: Commit**

```bash
rtk git add transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp
rtk git commit -m "test(te): assert deposit receipts carry no Jovian DA footprint fields"
```

---

## Task 7: 更新 spec 状态与审计闭合标注

**Files:**
- Modify: `docs/superpowers/specs/2026-07-01-opstack-jovian-da-footprint-design.md`
- Modify: `bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md`

- [ ] **Step 1: 标 spec 为已实现**

将 spec 顶部 `**Status:** 待评审` 改为 `**Status:** 已实现（Phase 1）`，并在 §0.1 成功标准下补一行：`实现提交见 plan 2026-07-02-opstack-jovian-da-footprint-receipt.md`。

- [ ] **Step 2: 更新审计条目**

在 round2 审计文档中，将 N2 / D10b 行的状态从「🟡 CONFIRMED 缺失」更新为「🟡 部分闭合（receipt 层已实现，块级/出块待 Phase 2）」，并在「建议行动 P0」第 1 条 DA footprint 后注明「receipt 部分已闭合」。

- [ ] **Step 3: Commit**

```bash
rtk git add docs/superpowers/specs/2026-07-01-opstack-jovian-da-footprint-design.md bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md
rtk git commit -m "docs(opstack): mark Jovian DA footprint receipt phase implemented"
```

---

## Self-Review

**Spec 覆盖：**
- §5 公式（有符号中间量）→ Task 1 ✅
- D2 标量来源（扩展 `OpStackFeeParams` + `unpackDaFootprintGasScalar`）→ Task 2 ✅
- §4.1 meta 字段 + §3.2 写入位置（`projectNormalReceiptMeta`，Jovian 门控，deposit 不写）→ Task 3 ✅
- §4.2 sidecar + §4.3 缓冲 + factory 拷贝路径 → Task 4 ✅
- §3.1 TE `makeReceipt` → Task 5 ✅
- §7 测试：T1（含 MIN 兜底）→ Task 1；T2/T5(scalar=0)/T7(pre-Jovian) → Task 3；T3 → Task 3 的 `jovian_user_tx`；T4/T6(deposit 不写) → Task 6；T8 回归 → 各 Task 的既有测试重跑 ✅
- §6 边界全分支（pre-Jovian / deposit / scalar=0 / Jovian 恒写）→ Task 3 + Task 6 ✅
- §4.4 序列化边界 → 沿用 `l1Fee` 模式，无需额外代码（Task 4 sidecar 天然不入 encode）✅

**Placeholder 扫描：** 无 TBD/TODO；每个代码 Step 均含完整代码与确切命令。

**类型一致性：** `estimatedDASize -> uint64_t` / `estimatedDASizeScaled -> s256`（Task 1）在 Task 3 消费一致；meta 字段 `daFootprintGasScalar`/`daFootprint`（`optional<uint64_t>`）在 Task 3 定义、Task 5 消费一致；sidecar `setDaFootprintGasScalar`/`setBlobGasUsed`（Task 4）在 Task 5 调用、Task 6 读取一致。

**已知偏差（非阻断）：** 缓冲常量 336 为估算；以 Task 4 Step 6 编译期 `static_assert` 实测为准向上取整。TE 构建可能受既有 `TxIntrinsicGas.h` 问题阻塞，已在 Task 5/6 给出降级路径（核心行为由 Task 3 bcos-evm 层测试保证）。

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-07-02-opstack-jovian-da-footprint-receipt.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - 每个 Task 派发新 subagent，Task 间两阶段 review，快速迭代。

**2. Inline Execution** - 本会话内按 executing-plans 分批执行，检查点 review。

**Which approach?**

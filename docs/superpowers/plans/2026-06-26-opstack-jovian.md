# OpStack Jovian (Scope B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `bcos-evm/opstack` 补齐 Jovian 执行层语义：operator fee fix、fork 门控、GPO `isJovian`/`getOperatorFee`、Jovian L1 attributes deposit（178B setter）。

**Architecture:** 扩展 `OpStackForkSchedule` 增加 `jovianTime`；`makeCachedOperatorCostFunc` 按 `isOpStackJovian(schedule, blockTime)` 在 Isthmus/Jovian 公式间切换；L1Block 双 setter 按 selector 路由；GPO 通过 `OpStackVmHostPolicy` 接收 fork 上下文。TE 默认 schedule **不变**（`makeIsthmusPlusForkSchedule()`）。

**Tech Stack:** C++17, Boost.Test, bcos-evm-op, evmc, CMake/CTest

**Spec:** `docs/superpowers/specs/2026-06-26-opstack-jovian-design.md`

## Global Constraints

- Scope B only：不含 min base fee、DA footprint 区块限流、receipt/RPC 字段、预编译上限
- `OpStackExecutionRequest::forkSchedule` 默认值保持 `makeIsthmusPlusForkSchedule()`；**不**改 `OpStackTransactionExecutorImpl`
- `isOpStackJovian(schedule, t) := schedule.jovianTime.has_value() && *jovianTime <= t`
- Jovian operator fee：`fee = gas * operatorFeeScalar * 100 + operatorFeeConstant`（对齐 op-geth `newOperatorCostFuncOperatorFeeFix`）
- Isthmus operator fee 公式与行为**零回归**
- `packOperatorFeeParams` 第三参数 `daFootprintGasScalar` 默认 `0`，避免破坏测试内联副本
- L1 setter：Isthmus `0x098999be`/176B 与 Jovian `0x3db6be2b`/178B **并存**，按 selector 路由
- 命令前缀：`rtk`

## Recommended Task Order

```
Task 1 → Task 2 → Task 3 → Task 4 → Task 5 → Task 6 → Task 7
```

## File Map

| File | Action |
|------|--------|
| `bcos-evm/opstack/OpStackForkSchedule.h` | Modify |
| `bcos-evm/test/opstack/OpStackForkScheduleTest.cpp` | Modify |
| `bcos-evm/opstack/OpStackFee.h` | Modify |
| `bcos-evm/opstack/OpStackFee.cpp` | Modify |
| `bcos-evm/test/opstack/OpStackFeeTest.cpp` | Modify |
| `bcos-evm/opstack/OpStackConstants.h` | Modify |
| `bcos-evm/opstack/l1/L1BlockSelectors.h` | Modify |
| `bcos-evm/opstack/l1/L1BlockStorage.h` | Modify |
| `bcos-evm/opstack/l1/L1BlockStorage.cpp` | Modify |
| `bcos-evm/opstack/l1/L1BlockPredeploy.cpp` | Modify |
| `bcos-evm/test/opstack/L1BlockPredeployTest.cpp` | Modify |
| `bcos-evm/test/fixtures/opstack/jovian_l1_attributes.bin` | Create |
| `bcos-evm/test/opstack/L1AttributesDepositTest.cpp` | Modify |
| `bcos-evm/opstack/l1/GasPriceOraclePredeploy.h` | Modify |
| `bcos-evm/opstack/l1/GasPriceOraclePredeploy.cpp` | Modify |
| `bcos-evm/opstack/OpStackVmHostPolicy.h` | Modify |
| `bcos-evm/opstack/OpStackTxLifecycle.cpp` | Modify |
| `bcos-evm/test/opstack/GasPriceOraclePredeployTest.cpp` | Modify |
| `docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-report.md` | Modify |

---

### Task 1: Fork schedule — `jovianTime` + helpers

**Files:**
- Modify: `bcos-evm/opstack/OpStackForkSchedule.h`
- Modify: `bcos-evm/test/opstack/OpStackForkScheduleTest.cpp`

**Interfaces:**
- Produces: `isOpStackJovian(OpStackForkSchedule const&, uint64_t blockTime) -> bool`
- Produces: `makeJovianPlusForkSchedule() -> OpStackForkSchedule`（`{fjordTime=0, isthmusTime=0, jovianTime=0}`）

- [ ] **Step 1: 写失败测试**

在 `OpStackForkScheduleTest.cpp` 追加：

```cpp
BOOST_AUTO_TEST_CASE(jovian_plus_preset_all_active_at_genesis)
{
    auto const schedule = makeJovianPlusForkSchedule();
    BOOST_REQUIRE(schedule.jovianTime.has_value());
    BOOST_CHECK_EQUAL(*schedule.jovianTime, 0u);
    BOOST_CHECK(isOpStackJovian(schedule, 0));
    BOOST_CHECK(isOpStackJovian(schedule, 999));
}

BOOST_AUTO_TEST_CASE(isthmus_plus_is_not_jovian)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    BOOST_CHECK(!isOpStackJovian(schedule, 0));
    BOOST_CHECK(!isOpStackJovian(schedule, 999));
}

BOOST_AUTO_TEST_CASE(jovian_future_timestamp_inactive)
{
    OpStackForkSchedule schedule{.fjordTime = 0, .isthmusTime = 0, .jovianTime = 100};
    BOOST_CHECK(!isOpStackJovian(schedule, 99));
    BOOST_CHECK(isOpStackJovian(schedule, 100));
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cd build && cmake --build . --target OpStackForkScheduleTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk test ./bcos-evm/test/OpStackForkScheduleTest --run_test=jovian_plus_preset_all_active_at_genesis
```

Expected: FAIL — `makeJovianPlusForkSchedule` / `isOpStackJovian` 未定义

- [ ] **Step 3: 实现**

`OpStackForkSchedule.h`：

```cpp
struct OpStackForkSchedule
{
    std::optional<uint64_t> fjordTime;
    std::optional<uint64_t> isthmusTime;
    std::optional<uint64_t> jovianTime;
};

inline OpStackForkSchedule makeJovianPlusForkSchedule()
{
    return OpStackForkSchedule{.fjordTime = 0, .isthmusTime = 0, .jovianTime = 0};
}

inline bool isOpStackJovian(OpStackForkSchedule const& schedule, uint64_t blockTime)
{
    return isOpStackForkActive(schedule.jovianTime, blockTime);
}
```

- [ ] **Step 4: 运行测试确认通过**

```bash
rtk test ./bcos-evm/test/OpStackForkScheduleTest
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackForkSchedule.h bcos-evm/test/opstack/OpStackForkScheduleTest.cpp
rtk git commit -m "feat(opstack): add jovianTime to OpStackForkSchedule"
```

---

### Task 2: Operator fee — `operatorCostJovian` + cache 分支

**Files:**
- Modify: `bcos-evm/opstack/OpStackFee.h`
- Modify: `bcos-evm/opstack/OpStackFee.cpp`
- Modify: `bcos-evm/test/opstack/OpStackFeeTest.cpp`

**Interfaces:**
- Consumes: `isOpStackJovian`, `makeJovianPlusForkSchedule`（Task 1）
- Produces: `u256 operatorCostJovian(uint64_t gas, OpStackFeeParams const& params)`
- Produces: `makeCachedOperatorCostFunc` 在 `isOpStackJovian` 为 true 时调用 Jovian 公式

**Canonical fixture（op-geth `receipt_opstack_test.go`）：**
- `gas=21000`, `scalar=1439103868`, `constant=1256417826609331460`
- Jovian expected: `1259439941733311460`
- Isthmus (gas=1618) expected unchanged: `1256417826611659930`

- [ ] **Step 1: 写失败测试**

```cpp
BOOST_AUTO_TEST_CASE(JovianOperator_gas21000_matchesOpGethFixture)
{
    auto const params = makeTestParams();
    BOOST_CHECK_EQUAL(operatorCostJovian(21'000, params), u256("1259439941733311460"));
}

BOOST_AUTO_TEST_CASE(selectOperator_JovianPlus_gas1618_usesJovianFormula)
{
    auto const schedule = makeJovianPlusForkSchedule();
    auto const operatorCost = selectOperatorCostFunc(schedule, makeTestParams());
    // 1618 * 1439103868 * 100 + constant
    BOOST_CHECK_EQUAL(operatorCost(1618, 1), u256("1258650673815013860"));
}

BOOST_AUTO_TEST_CASE(selectOperator_IsthmusPlus_still_uses_isthmus_formula)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    auto const operatorCost = selectOperatorCostFunc(schedule, makeTestParams());
    BOOST_CHECK_EQUAL(operatorCost(1618, 1), u256("1256417826611659930"));
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
rtk test ./bcos-evm/test/OpStackFeeTest --run_test=JovianOperator_gas21000_matchesOpGethFixture
```

Expected: FAIL — `operatorCostJovian` 未定义

- [ ] **Step 3: 实现**

`OpStackFee.h` 增加声明：

```cpp
u256 operatorCostJovian(uint64_t gas, OpStackFeeParams const& params);
```

`OpStackFee.cpp`：

```cpp
u256 operatorCostJovian(uint64_t gas, OpStackFeeParams const& params)
{
    if (params.operatorFeeScalar == 0 && params.operatorFeeConstant == 0)
    {
        return 0;
    }
    auto fee = u256(gas) * params.operatorFeeScalar * u256(100);
    fee += params.operatorFeeConstant;
    return fee;
}
```

`makeCachedOperatorCostFunc` 内 cache refresh 分支：

```cpp
if (!cache->isthmusActive)
{
    return 0;
}
if (cache->params.operatorFeeScalar == 0 && cache->params.operatorFeeConstant == 0)
{
    return 0;
}
if (isOpStackJovian(schedule, blockTime))
{
    return operatorCostJovian(gas, cache->params);
}
return operatorCostIsthmus(gas, cache->params);
```

- [ ] **Step 4: 运行测试确认通过**

```bash
rtk test ./bcos-evm/test/OpStackFeeTest
```

Expected: PASS（含 Isthmus 回归）

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackFee.h bcos-evm/opstack/OpStackFee.cpp bcos-evm/test/opstack/OpStackFeeTest.cpp
rtk git commit -m "feat(opstack): add Jovian operator fee formula and fork branch"
```

---

### Task 3: L1Block storage — Jovian parser + pack daFootprint

**Files:**
- Modify: `bcos-evm/opstack/OpStackConstants.h`
- Modify: `bcos-evm/opstack/l1/L1BlockSelectors.h`
- Modify: `bcos-evm/opstack/l1/L1BlockStorage.h`
- Modify: `bcos-evm/opstack/l1/L1BlockStorage.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`
- Create: `bcos-evm/test/fixtures/opstack/jovian_l1_attributes.bin`

**Interfaces:**
- Produces: `parseJovianL1Attributes(bytesConstRef) -> std::optional<JovianL1Attributes>`
- Produces: `packOperatorFeeParams(scalar, constant, daFootprintGasScalar=0) -> evmc_bytes32`

**Fixture hex（op-geth `receipt_opstack_test.go`，178B）：**

```
3db6be2b000000020000000300000000000004d200000000000004d200000000000004d2000000000000000000000000000000000000000000000000000000003b9aca00000000000000000000000000000000000000000000000000000000000098968000000000000000000000000000000000000000000000000000000000000004d200000000000000000000000000000000000000000000000000000000000004d255c6fb7c116fb15b44847d040190
```

- [ ] **Step 1: 创建 fixture 文件**

```bash
python3 - <<'PY'
import pathlib
hex_str = "3db6be2b000000020000000300000000000004d200000000000004d200000000000004d2000000000000000000000000000000000000000000000000000000003b9aca00000000000000000000000000000000000000000000000000000000000098968000000000000000000000000000000000000000000000000000000000000004d200000000000000000000000000000000000000000000000000000000000004d255c6fb7c116fb15b44847d040190"
data = bytes.fromhex(hex_str)
path = pathlib.Path("bcos-evm/test/fixtures/opstack/jovian_l1_attributes.bin")
path.write_bytes(data)
assert len(data) == 178
PY
```

- [ ] **Step 2: 写失败测试**

```cpp
BOOST_AUTO_TEST_CASE(parse_jovian_fixture_matches_fields)
{
    auto const calldata = loadFixture("jovian_l1_attributes.bin");
    BOOST_REQUIRE_EQUAL(calldata.size(), JOVIAN_L1_ATTRIBUTES_LEN);
    auto const parsed = parseJovianL1Attributes(bcos::ref(calldata));
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(parsed->baseFeeScalar, 2u);
    BOOST_CHECK_EQUAL(parsed->blobBaseFeeScalar, 3u);
    BOOST_CHECK_EQUAL(parsed->daFootprintGasScalar, 400u);
}

BOOST_AUTO_TEST_CASE(pack_operator_fee_params_writes_da_footprint_scalar)
{
    auto const packed = packOperatorFeeParams(2, 5, 400);
    BOOST_CHECK_EQUAL(unpackDaFootprintGasScalar(packed), u256(400));
    BOOST_CHECK_EQUAL(unpackOperatorFeeScalar(packed), u256(2));
    BOOST_CHECK_EQUAL(unpackOperatorFeeConstant(packed), u256(5));
}
```

- [ ] **Step 3: 运行测试确认失败**

```bash
rtk test ./bcos-evm/test/L1BlockPredeployTest --run_test=parse_jovian_fixture_matches_fields
```

Expected: FAIL

- [ ] **Step 4: 实现**

`OpStackConstants.h`：

```cpp
inline constexpr size_t JOVIAN_L1_ATTRIBUTES_LEN = 178;
```

`L1BlockSelectors.h`：

```cpp
inline constexpr uint32_t kSetL1BlockValuesJovian = 0x3db6be2b;
```

`L1BlockStorage.h` — `JovianL1Attributes` 继承/复用 `IsthmusL1Attributes` 字段 + `uint16_t daFootprintGasScalar`；`parseJovianL1Attributes`；`packOperatorFeeParams` 签名加默认第三参数。

`L1BlockStorage.cpp` — parser 在 `calldata.size() >= JOVIAN_L1_ATTRIBUTES_LEN` 时解析 offset 176–177；pack 写入 `out.bytes[18]`/`out.bytes[19]`。

- [ ] **Step 5: 运行测试确认通过**

```bash
rtk test ./bcos-evm/test/L1BlockPredeployTest
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackConstants.h bcos-evm/opstack/l1/L1BlockSelectors.h \
  bcos-evm/opstack/l1/L1BlockStorage.h bcos-evm/opstack/l1/L1BlockStorage.cpp \
  bcos-evm/test/opstack/L1BlockPredeployTest.cpp bcos-evm/test/fixtures/opstack/jovian_l1_attributes.bin
rtk git commit -m "feat(opstack): add Jovian L1 attributes parser and daFootprint pack"
```

---

### Task 4: L1Block predeploy — `applySetterJovian`

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`

**Interfaces:**
- Consumes: `parseJovianL1Attributes`, `packOperatorFeeParams(scalar, constant, daFootprint)`（Task 3）
- Produces: `L1BlockPredeploy::dispatch` 处理 `kSetL1BlockValuesJovian`

- [ ] **Step 1: 写失败测试**

```cpp
BOOST_AUTO_TEST_CASE(setter_unpacks_jovian_fixture_into_slots_including_da_footprint)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("jovian_l1_attributes.bin");

    auto result = L1BlockPredeploy::dispatch(state, makeCall(calldata, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);

    checkGetterHex(state, l1block::kDaFootprintGasScalar,
        "0000000000000000000000000000000000000000000000000000000000000190"); // 400
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
rtk test ./bcos-evm/test/L1BlockPredeployTest --run_test=setter_unpacks_jovian_fixture_into_slots_including_da_footprint
```

Expected: FAIL — REVERT（未知 selector）

- [ ] **Step 3: 实现 `applySetterJovian`**

复制 `applySetterIsthmus` 结构；使用 `parseJovianL1Attributes`；`packOperatorFeeParams(parsed->operatorFeeScalar, parsed->operatorFeeConstant, parsed->daFootprintGasScalar)`。

`dispatch` switch 增加：

```cpp
case l1block::kSetL1BlockValuesJovian:
    return applySetterJovian(state, msg, input);
```

- [ ] **Step 4: 运行测试确认通过**

```bash
rtk test ./bcos-evm/test/L1BlockPredeployTest
```

Expected: PASS（含 Isthmus setter 回归）

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/l1/L1BlockPredeploy.cpp bcos-evm/test/opstack/L1BlockPredeployTest.cpp
rtk git commit -m "feat(opstack): add Jovian L1Block setter dispatch"
```

---

### Task 5: GPO + VmHostPolicy fork 感知

**Files:**
- Modify: `bcos-evm/opstack/l1/GasPriceOraclePredeploy.h`
- Modify: `bcos-evm/opstack/l1/GasPriceOraclePredeploy.cpp`
- Modify: `bcos-evm/opstack/OpStackVmHostPolicy.h`
- Modify: `bcos-evm/opstack/OpStackTxLifecycle.cpp`
- Modify: `bcos-evm/test/opstack/GasPriceOraclePredeployTest.cpp`

**Interfaces:**
- Consumes: `isOpStackJovian`, `operatorCostJovian`（Task 1–2）
- Produces: `GasPriceOraclePredeploy::dispatch(state, msg, l2BaseFee, forkSchedule, blockTime)`
- Produces: `OpStackVmHostPolicy(state*, l2BaseFee, forkSchedule, blockTimestamp)`

- [ ] **Step 1: 写失败测试**

更新 `GasPriceOraclePredeployTest.cpp` 中 `decimals_and_fork_flags_match_isthmus_profile`：拆为两个 case；新增：

```cpp
BOOST_AUTO_TEST_CASE(jovian_schedule_is_jovian_and_operator_fee_use_jovian_formula)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);
    // seed operator fee params slot (same as OpStackFeeTest makeTestParamsState)
    state::Account l1Block;
    l1Block.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'439'103'868, 1'256'417'826'609'331'460ULL);
    baseState.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    auto const schedule = makeJovianPlusForkSchedule();
    auto const blockTime = 1u;

    auto jovian = GasPriceOraclePredeploy::dispatch(
        state, makeCall(selectorInput(gpo::kIsJovian)), u256(0), schedule, blockTime);
    BOOST_REQUIRE(jovian.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*jovian), u256(1));
    releaseResult(*jovian);

    bytes getOpFeeInput = selectorInput(gpo::kGetOperatorFee);
    getOpFeeInput.resize(36);
    // ABI: gasUsed = 21000 at offset 4
    auto gasUsed = u256(21'000);
    auto encoded = state::toEvmC(gasUsed);
    std::memcpy(getOpFeeInput.data() + 4, encoded.bytes, 32);

    auto fee = GasPriceOraclePredeploy::dispatch(
        state, makeCall(getOpFeeInput), u256(0), schedule, blockTime);
    BOOST_REQUIRE(fee.has_value());
    BOOST_CHECK_EQUAL(readU256Output(*fee), u256("1259439941733311460"));
    releaseResult(*fee);
}
```

同步更新所有 `GasPriceOraclePredeploy::dispatch(...)` 调用点为 5 参数（测试文件内 + `OpStackVmHostPolicy`）。

- [ ] **Step 2: 运行测试确认失败**

```bash
rtk test ./bcos-evm/test/GasPriceOraclePredeployTest --run_test=jovian_schedule_is_jovian_and_operator_fee_use_jovian_formula
```

Expected: FAIL — 编译错误或 `kIsJovian` 返回 0

- [ ] **Step 3: 实现**

`GasPriceOraclePredeploy.h`：

```cpp
static std::optional<evmc_result> dispatch(
    state::State& state, evmc_message const& msg, bcos::u256 l2BaseFee,
    OpStackForkSchedule const& forkSchedule, uint64_t blockTime);
```

`GasPriceOraclePredeploy.cpp`：

```cpp
case gpo::kIsJovian:
    return successWithU256(msg.gas, isOpStackJovian(forkSchedule, blockTime) ? u256(1) : u256(0));
case gpo::kGetOperatorFee:
{
    // ... existing gasUsed decode ...
    auto const params = loadOpStackFeeParams(state);
    auto const fee = isOpStackJovian(forkSchedule, blockTime) ?
                         operatorCostJovian(static_cast<uint64_t>(gasUsed), params) :
                         operatorCostIsthmus(static_cast<uint64_t>(gasUsed), params);
    return successWithU256(msg.gas, fee);
}
```

`OpStackVmHostPolicy.h` — 增加成员 `OpStackForkSchedule m_forkSchedule`、`uint64_t m_blockTimestamp`；构造：

```cpp
explicit OpStackVmHostPolicy(state::State* state = nullptr, bcos::u256 l2BaseFee = 0,
    OpStackForkSchedule forkSchedule = makeIsthmusPlusForkSchedule(), uint64_t blockTimestamp = 0)
```

GPO 调用：

```cpp
return GasPriceOraclePredeploy::dispatch(
    *m_state, msg, m_l2BaseFee, m_forkSchedule, m_blockTimestamp);
```

`OpStackTxLifecycle.cpp`：

```cpp
OpStackVmHostPolicy opHostExtension(
    &ctx.state, input.blockInfo.baseFee, input.forkSchedule, input.blockInfo.timestamp);
```

现有 `OpStackVmHostPolicy extension(&state)` 测试调用保持默认参数（pre-Jovian）。

- [ ] **Step 4: 编译并运行 GPO + cross 测试**

```bash
cmake --build build --target GasPriceOraclePredeployTest PrecompileRouterEquivalenceTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk test ./build/bcos-evm/test/GasPriceOraclePredeployTest
rtk test ./build/bcos-evm/test/PrecompileRouterEquivalenceTest
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/l1/GasPriceOraclePredeploy.h bcos-evm/opstack/l1/GasPriceOraclePredeploy.cpp \
  bcos-evm/opstack/OpStackVmHostPolicy.h bcos-evm/opstack/OpStackTxLifecycle.cpp \
  bcos-evm/test/opstack/GasPriceOraclePredeployTest.cpp
rtk git commit -m "feat(opstack): wire Jovian fork context into GPO and VmHostPolicy"
```

---

### Task 6: L1 attributes deposit E2E — Jovian schedule + operator fee

**Files:**
- Modify: `bcos-evm/test/opstack/L1AttributesDepositTest.cpp`

**Interfaces:**
- Consumes: Task 1–5 全部

- [ ] **Step 1: 写失败测试**

```cpp
BOOST_AUTO_TEST_CASE(jovian_l1_attributes_deposit_then_user_tx_uses_jovian_operator_fee)
{
    // 复制 l1_attributes_deposit_updates_l1block_and_affects_following_user_tx 结构
    // 差异：
    //   calldata = loadFixture("jovian_l1_attributes.bin")
    //   depositInput.forkSchedule = makeJovianPlusForkSchedule()
    //   userInput.forkSchedule = makeJovianPlusForkSchedule()
    //   expectedOperator = operatorCostJovian(gasUsed, feeParams)
    // 断言 receiptMeta.operatorFee 与 OP_OPERATOR_FEE_RECIPIENT balance
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
rtk test ./bcos-evm/test/L1AttributesDepositTest --run_test=jovian_l1_attributes_deposit_then_user_tx_uses_jovian_operator_fee
```

Expected: FAIL — operator fee 仍用 Isthmus 值

- [ ] **Step 3: 确认生产代码已就绪**

Task 2–5 应已使 `wireOperatorCostFuncWithState` + lifecycle 在 Jovian schedule 下走 Jovian 公式。若仍失败，检查 `depositInput`/`userInput` 是否设置 `forkSchedule`。

- [ ] **Step 4: 运行测试确认通过**

```bash
rtk test ./bcos-evm/test/L1AttributesDepositTest
```

Expected: PASS（含原 Isthmus E2E）

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/opstack/L1AttributesDepositTest.cpp
rtk git commit -m "test(opstack): Jovian L1 attributes deposit E2E with operator fee fix"
```

---

### Task 7: 全量回归 + 文档更新

**Files:**
- Modify: `docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-report.md`

- [ ] **Step 1: 运行 opstack 测试套件**

```bash
cd build && ctest -R 'OpStack|L1Block|GasPriceOracle|L1Attributes' --output-on-failure
```

Expected: 全部 PASS

- [ ] **Step 2: 更新 diff 报告**

在 `2026-06-25-opstack-op-geth-diff-report.md`：
- 差异 9 移入「已对齐」
- 汇总表更新
- 增加修订记录指向本 plan

- [ ] **Step 3: 更新 spec 状态**

`docs/superpowers/specs/2026-06-26-opstack-jovian-design.md` 首行 `Status: Implemented`

- [ ] **Step 4: Commit**

```bash
rtk git add docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-report.md \
  docs/superpowers/specs/2026-06-26-opstack-jovian-design.md
rtk git commit -m "docs(opstack): mark Jovian scope B aligned in op-geth diff report"
```

---

## Spec Self-Review

| Spec 要求 | Task |
|-----------|------|
| Fork 门控 `jovianTime` | Task 1 |
| Operator fee fix | Task 2 |
| GPO `isJovian`/`getOperatorFee` | Task 5 |
| Jovian L1 attributes 178B | Task 3–4 |
| Isthmus setter 保留 | Task 4（不删除 Isthmus case） |
| TE 默认不变 | Global Constraints |
| 测试 T1–T9 | Task 2–6 |
| diff 报告更新 | Task 7 |

无 TBD/占位符。`packOperatorFeeParams` 默认第三参数避免测试大范围改动。

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-26-opstack-jovian.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派发独立 subagent，Task 间 review，迭代快

**2. Inline Execution** — 本会话按 Task 顺序直接实现，checkpoint 处暂停 review

你选哪种？

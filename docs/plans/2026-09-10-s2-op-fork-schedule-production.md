# S2 生产 OP fork-schedule codec 放开历史 El fork 名 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让生产加载链（`[op_fork_schedule].canonical` → codec → `OpForkSchedule::parse` → `configAt`）接受含 Regolith…Karst 9 档 EL fork 的 schedule，名字表单一真相源，校验严格连续。

**Architecture:** 在 `OpForkScheduleCodec.h` 建一张协议顺序的 `c_opForkNames[9]` 权威表，`forkOrder`/`isAllowedBaseline` 改查表；`validateScheduleRecords` 用「baseline 锚点 + 第 2 条起 `order==prev+1`」的严格连续规则，删掉 Karst/Jovian 特例；`c_maxOpForkActivations` 8→16。`OpForkSchedule.cpp` 的 `forkFromName`/`forkNameFromEnum` 改为按该表索引 ↔ `OpFork`。**不碰** Engine 解析/版本表（S3）。

**Tech Stack:** C++20, Boost.Test, CMake + Ninja + 主仓 vcpkg。

**Spec:** `docs/2026-09-10-s2-op-fork-schedule-production-design.md`

## Global Constraints

- 唯一改代码处：`/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550` @ `feat/karst-on-release-3.18`。**禁止**在主仓 `/Users/octopus/octo/code/FISCO-BCOS` 脏工作区改代码。
- 冲突顺序：**OP protocol spec → op-geth → 本设计决定 → 本仓库现码**。每个 Task 的 Step 3 动手前必须打开该 Task 列出的权威原文，禁止用 FISCO 现码反推官方语义。
- 名字集固定 9 档：`regolith, canyon, ecotone, fjord, granite, holocene, isthmus, jovian, karst`；与 `bcos-evm` 的 `OpFork` 枚举顺序一致。**不含** `delta` / `interop` / `bedrock`。
- baseline 必须 `timestamp == 0`，可为任一 EL 档；严格连续（第 2 条起 `order == previousOrder + 1`）。
- 不改 `OpForkId` / `OpSchedulerSeam` / `engine/bcos-engine/*` / `OpBaseFee.h`（S3）。
- 现有 isthmus/jovian/karst canonical 的行为必须逐字节不变（除已列出的翻转测试）。

**Worktree / 构建（首次或 build 缺失时）：**

```bash
WT=/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B "$WT/build" -S "$WT" \
  -DFULLNODE=ON -DTESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=/Users/octopus/octo/code/FISCO-BCOS/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_INSTALLED_DIR=/Users/octopus/octo/code/FISCO-BCOS/build/vcpkg_installed -G Ninja
```

**测试 target / 二进制：**

| target | 二进制 | 涉及 suite |
|--------|--------|-----------|
| `bcos-evm-opstack-tests` | `$WT/build/bcos-evm/test/bcos-evm-opstack-tests` | `OpForkScheduleCodecSuite`、`OpForkScheduleSuite` |
| `test-bcos-tool` | `$WT/build/bcos-tool/test/test-bcos-tool` | `NodeConfigOpForkScheduleTest` |
| `test-bcos-ledger` | `$WT/build/bcos-ledger/test/test-bcos-ledger` | `OpForkScheduleMetadataTest` |

**Boost suite 名（写错会空跑 exit 0 假绿）：** `OpForkScheduleCodecSuite`、`OpForkScheduleSuite`、`NodeConfigOpForkScheduleTest`、`OpForkScheduleMetadataTest`。

---

### Task 0: 探针 — 把今日必炸点写成基线（只读）

**Files:** 只读。

- [ ] **Step 1: 确认收窄点仍在**

```bash
WT=/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
rg -n "forkOrder|isAllowedBaseline|hasKarst|hasJovian|c_maxOpForkActivations" \
  "$WT/bcos-framework/bcos-framework/ledger/OpForkScheduleCodec.h"
rg -n "forkFromName|forkNameFromEnum" "$WT/bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp"
```

期望：`forkOrder` 只认 isthmus/jovian/karst；`isAllowedBaseline` 只允许 isthmus|jovian；`c_maxOpForkActivations = 8`；存在 `hasKarst && baseline=="isthmus" && !hasJovian` 特例；`forkNameFromEnum` 对 pre-Isthmus 抛错。

- [ ] **Step 2: 打开权威原文（本 Task 只读，记下顺序）**

```bash
sed -n '10,80p' /Users/octopus/octo/code/op-geth/params/config_op.go
sed -n '505,522p' /Users/octopus/octo/code/op-geth/params/config.go
rg -n 'Regolith|Canyon|Ecotone|Fjord|Granite|Holocene|Isthmus|Jovian|Karst' \
  /Users/octopus/octo/code/ethereum-optimism-specs/specs/protocol/superchain-upgrades.md | head
```

确认：op-geth EL fork 顺序 = Regolith→Canyon→Ecotone→Fjord→Granite→Holocene→Isthmus→Jovian→Karst（**无 Delta**，`Interop` 在其后但不在范围）。

- [ ] **Step 3: 确认 build 可用**

```bash
ninja -C "$WT/build" bcos-evm-opstack-tests
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleCodecSuite 2>&1 | tail -5
```

不改行为，无代码则不提交。

---

### Task 1: codec 权威名字表 + 严格连续 + 上限 16

**Files:**
- Modify: `bcos-framework/bcos-framework/ledger/OpForkScheduleCodec.h`
- Test: `bcos-evm/test/opstack/OpForkScheduleCodecTest.cpp`

**Interfaces:**
- Produces: `bcos::ledger::detail::c_opForkNames`（`std::array<std::string_view,9>`）、`detail::forkOrder(std::string_view) -> int`、`detail::isAllowedBaseline(std::string_view) -> bool`、`c_maxOpForkActivations == 16`。

- [ ] **Step 1: 写失败测试 / 改翻转测试**

在 `bcos-evm/test/opstack/OpForkScheduleCodecTest.cpp` 顶部补 `#include <string>`，并做三件事：

① 新增（会红）：

```cpp
BOOST_AUTO_TEST_CASE(AcceptsAllNineElForkBaselines)
{
    for (auto const* name : {"regolith", "canyon", "ecotone", "fjord", "granite", "holocene",
             "isthmus", "jovian", "karst"})
    {
        auto acts = parseOpForkSchedule(std::string("0:") + name);
        BOOST_REQUIRE_EQUAL(acts.size(), 1u);
        BOOST_CHECK_EQUAL(acts[0].forkName, name);
    }
}

BOOST_AUTO_TEST_CASE(AcceptsFullOfficialChain)
{
    auto acts = parseOpForkSchedule(
        "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
        "5000:holocene,6000:isthmus,7000:jovian,8000:karst");
    BOOST_REQUIRE_EQUAL(acts.size(), 9u);
    BOOST_CHECK_EQUAL(acts[8].forkName, "karst");
    BOOST_CHECK_EQUAL(acts[8].timestamp, 8000u);
}

BOOST_AUTO_TEST_CASE(NormalizesCaseAndWhitespace)
{
    auto acts = parseOpForkSchedule("0: Regolith ,1000:Canyon");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[0].forkName, "regolith");
    BOOST_CHECK_EQUAL(acts[1].forkName, "canyon");
}
```

② 用下面**替换**现有 `RejectsKarstWithoutJovian`（`:27-39`）：`0:karst` 由拒变合法、`0:isthmus,<t>:karst` 改判跳档：

```cpp
BOOST_AUTO_TEST_CASE(RejectsSkippedFork)
{
    const auto isGap = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("forks out of protocol order") !=
               std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1783526401:karst"), InvalidOpForkSchedule, isGap);
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:regolith,100:ecotone"), InvalidOpForkSchedule, isGap);
}
```

③ 用下面**替换** `RejectsTooManyActivations`（`:52-62`）与 `RejectsEmptyMissingBaselineAndOrder`（`:64-85`）：

```cpp
BOOST_AUTO_TEST_CASE(RejectsTooManyActivations)
{
    const auto isTooMany = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("too many activations") != std::string_view::npos;
    };
    // cap 16：第 17 条在 push 前抛。
    std::string canonical = "0:regolith";
    for (uint64_t i = 1; i <= 16; ++i)
    {
        canonical += "," + std::to_string(i) + ":regolith";
    }
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(canonical), InvalidOpForkSchedule, isTooMany);
}

BOOST_AUTO_TEST_CASE(RejectsEmptyMissingBaselineAndOrder)
{
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("missing timestamp-0 baseline") !=
                   std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:notafork"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("invalid baseline") != std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(""), InvalidOpForkSchedule, [](auto const& e) {
        return std::string_view{e.what()}.find("empty schedule") != std::string_view::npos;
    });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:jovian,1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("forks out of protocol order") !=
                   std::string_view::npos;
        });
}

BOOST_AUTO_TEST_CASE(RejectsDuplicateTimestamp)
{
    const auto isDup = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("duplicate timestamp") != std::string_view::npos;
    };
    // 第 3 条 ts 与前一条相同：先于连续检查报错。
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,100:jovian,100:karst"),
        InvalidOpForkSchedule, isDup);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
ninja -C "$WT/build" bcos-evm-opstack-tests
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleCodecSuite
```

Expected: FAIL — `0:regolith`/`0:canyon`/… 目前被 `invalid baseline`/`unknown` 拒；`AcceptsFullOfficialChain` 失败。

- [ ] **Step 3: 写最小实现**

先打开权威原文（本 Task 强制）：

```bash
sed -n '10,80p' /Users/octopus/octo/code/op-geth/params/config_op.go
```

`OpForkScheduleCodec.h`：补 `#include <array>`；把 `namespace detail` 里的 `forkOrder`（`:65-74`）与 `isAllowedBaseline`（`:76-79`）整体替换为：

```cpp
inline constexpr std::array<std::string_view, 9> c_opForkNames = {
    "regolith", "canyon", "ecotone", "fjord", "granite",
    "holocene", "isthmus", "jovian", "karst",
};

[[nodiscard]] inline constexpr int forkOrder(std::string_view forkName)
{
    for (std::size_t i = 0; i < c_opForkNames.size(); ++i)
    {
        if (c_opForkNames[i] == forkName)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] inline constexpr bool isAllowedBaseline(std::string_view forkName)
{
    return forkOrder(forkName) >= 0;
}
```

把 `validateScheduleRecords`（`:119-168`）整体替换为：

```cpp
inline void validateScheduleRecords(std::span<const OpForkActivationRecord> activations)
{
    if (activations.empty())
        throwInvalidOpForkSchedule("empty schedule");

    if (activations.front().timestamp != 0)
        throwInvalidOpForkSchedule("missing timestamp-0 baseline");

    if (!isAllowedBaseline(activations.front().forkName))
        throwInvalidOpForkSchedule("invalid baseline fork");

    int previousOrder = -1;
    uint64_t previousTimestamp = 0;

    for (std::size_t index = 0; index < activations.size(); ++index)
    {
        const auto& activation = activations[index];
        const int order = forkOrder(activation.forkName);
        if (order < 0)
            throwInvalidOpForkSchedule("unknown or pre-Isthmus fork");

        if (activation.timestamp < previousTimestamp)
            throwInvalidOpForkSchedule("timestamps out of order");
        if (index != 0 && activation.timestamp == previousTimestamp)
            throwInvalidOpForkSchedule("duplicate timestamp");

        // baseline 是锚点；从第 2 条起严格连续（order 严格递增，重复 fork 不可能出现）。
        if (index != 0 && order != previousOrder + 1)
            throwInvalidOpForkSchedule("forks out of protocol order");

        previousOrder = order;
        previousTimestamp = activation.timestamp;
    }
}
```

把 `c_maxOpForkActivations`（`:185`）改为 `16`。把文件头注释（`:37-38`）「baseline remains isthmus|jovian」改为「baseline may be any EL fork (regolith…karst); schedule must be contiguous」。

- [ ] **Step 4: 跑测试确认通过**

```bash
ninja -C "$WT/build" bcos-evm-opstack-tests
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleCodecSuite
```

Expected: PASS 全部用例。

- [ ] **Step 5: Commit**

```bash
cd "$WT"
rtk git add bcos-framework/bcos-framework/ledger/OpForkScheduleCodec.h \
  bcos-evm/test/opstack/OpForkScheduleCodecTest.cpp
rtk git commit -m "feat(ledger): accept historical EL fork names in schedule codec"
```

---

### Task 2: `OpForkSchedule` 复用名字表（+ 陈旧注释）

**Files:**
- Modify: `bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp`
- Modify: `bcos-evm/bcos-evm/opstack/OpForkSchedule.h`（仅注释）
- Test: `bcos-evm/test/opstack/OpForkScheduleTest.cpp`

**Interfaces:**
- Consumes: `bcos::ledger::detail::c_opForkNames`、`detail::forkOrder`（Task 1）。
- Produces: `forkFromName`/`forkNameFromEnum` 覆盖 9 档；`OpForkSchedule::parse` 接受任一 EL 档 baseline。

- [ ] **Step 1: 写失败测试 / 改翻转测试**

在 `OpForkScheduleTest.cpp`：把 `ParseStillRejectsRegolithName`（`:128-131`）替换为：

```cpp
BOOST_AUTO_TEST_CASE(ParseAcceptsRegolithBaseline)
{
    auto s = OpForkSchedule::parse("0:regolith");
    BOOST_CHECK_EQUAL(s.forkAt(0), OpFork::Regolith);
    BOOST_CHECK_EQUAL(&s.configAt(0), &regolithConfig());
}
```

新增（会红）：

```cpp
BOOST_AUTO_TEST_CASE(ForkNameEnumRoundTripsAllNine)
{
    using bcos::ledger::detail::c_opForkNames;
    for (std::size_t i = 0; i < c_opForkNames.size(); ++i)
    {
        auto s = OpForkSchedule::parse("0:" + std::string(c_opForkNames[i]));
        BOOST_CHECK_EQUAL(s.forkAt(0), static_cast<OpFork>(i));
    }
    BOOST_CHECK_EQUAL(static_cast<std::size_t>(OpFork::Karst), c_opForkNames.size() - 1);
}
```

（`OpForkScheduleTest.cpp` 若缺 `<string>` / `<cstddef>` 则补 `#include <string>`。）

- [ ] **Step 2: 跑测试确认失败**

```bash
ninja -C "$WT/build" bcos-evm-opstack-tests
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleSuite/ParseAcceptsRegolithBaseline
```

Expected: FAIL — `parse("0:regolith")` 仍抛 `InvalidOpForkSchedule`。

- [ ] **Step 3: 写最小实现**

先打开权威原文（本 Task 强制）：

```bash
sed -n '505,522p' /Users/octopus/octo/code/op-geth/params/config.go
```

`OpForkSchedule.cpp`：把 `forkFromName`（`:13-22`）替换为：

```cpp
OpFork forkFromName(std::string_view forkName)
{
    const int index = ledger::detail::forkOrder(forkName);
    if (index < 0)
    {
        ledger::throwInvalidOpForkSchedule("unknown fork");
    }
    return static_cast<OpFork>(index);
}
```

把 `forkNameFromEnum`（`:50-63`）替换为：

```cpp
std::string forkNameFromEnum(OpFork fork)
{
    const auto index = static_cast<std::size_t>(fork);
    if (index >= ledger::detail::c_opForkNames.size())
    {
        ledger::throwInvalidOpForkSchedule("unknown fork");
    }
    return std::string(ledger::detail::c_opForkNames[index]);
}
```

`OpForkSchedule.h` 更新陈旧注释：`:15-18`「production parse / codec is still Isthmus+-only (decision A5)」与 `:101-103`「Karst requires Jovian first」。改为反映：生产 parse 接受 Regolith…Karst 的连续 schedule（Engine 版本解析仍属 S3）。

- [ ] **Step 4: 跑测试确认通过**

```bash
ninja -C "$WT/build" bcos-evm-opstack-tests
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleSuite
```

Expected: PASS（含原 `ConfigAtTimestampSelectsIsthmusThenJovian`、`LegacyFlagsStillSelectIsthmusOrJovian`、`KarstImpliesOsakaConfig`）。

- [ ] **Step 5: Commit**

```bash
cd "$WT"
rtk git add bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp \
  bcos-evm/bcos-evm/opstack/OpForkSchedule.h \
  bcos-evm/test/opstack/OpForkScheduleTest.cpp
rtk git commit -m "feat(op): map schedule fork names through the codec table"
```

---

### Task 3: 加载链测试（ledger metadata + NodeConfig）

**Files:**
- Test: `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp`
- Test: `bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp`

**Interfaces:**
- Consumes: Task 1/2 的行为（codec 接受 9 档、严格连续、错误文案 `forks out of protocol order`）。

- [ ] **Step 1: 写测试 / 改翻转测试**

`bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp`：把 `rejectsKarstWithoutJovian`（`:42-48`）替换为：

```cpp
BOOST_AUTO_TEST_CASE(rejectsSkippedFork)
{
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                                           "canonical=0:isthmus,1:karst\n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "protocol order"); });
}
```

同文件新增：

```cpp
BOOST_AUTO_TEST_CASE(acceptsFullOfficialChain)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
                "5000:holocene,6000:isthmus,7000:jovian,8000:karst\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule,
        "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
        "5000:holocene,6000:isthmus,7000:jovian,8000:karst");
}

BOOST_AUTO_TEST_CASE(acceptsRegolithBaseline)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                     "canonical=0:regolith\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, "0:regolith");
}
```

`bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp`：在匿名 namespace 的 `c_isthmusJovianSchedule`（`:44`）旁新增常量，并在 suite 内新增用例：

```cpp
constexpr char const* c_officialHistorySchedule =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
    "5000:holocene,6000:isthmus,7000:jovian,8000:karst";
```

```cpp
BOOST_AUTO_TEST_CASE(officialHistoryScheduleRoundTrips)
{
    const auto resolved = resolveOpForkScheduleCanonical(
        std::nullopt, std::string{c_officialHistorySchedule}, false, HashType{});
    BOOST_CHECK_EQUAL(resolved, c_officialHistorySchedule);
    BOOST_CHECK_EQUAL(keccakOpForkScheduleHash(resolved).hex(),
        keccakOpForkScheduleHash(c_officialHistorySchedule).hex());
}
```

- [ ] **Step 2: 跑测试确认**

```bash
ninja -C "$WT/build" test-bcos-tool test-bcos-ledger
"$WT/build/bcos-tool/test/test-bcos-tool" --run_test=NodeConfigOpForkScheduleTest
"$WT/build/bcos-ledger/test/test-bcos-ledger" --run_test=OpForkScheduleMetadataTest
```

Expected: Task 3 新用例在 Task 1/2 完成后 PASS。若此时仍 FAIL（在 Task 1/2 之前单独执行），属预期——先完成 Task 1/2。

- [ ] **Step 3: 写最小实现**

无生产代码改动（本 Task 只验证加载链已由 Task 1/2 打通）。若 `acceptsRegolithBaseline`/`acceptsFullOfficialChain` 仍失败，回到 Task 1/2 检查 `forkOrder`/`isAllowedBaseline`；禁止在本 Task 改 codec。

- [ ] **Step 4: 跑测试确认通过**

```bash
"$WT/build/bcos-tool/test/test-bcos-tool" --run_test=NodeConfigOpForkScheduleTest
"$WT/build/bcos-ledger/test/test-bcos-ledger" --run_test=OpForkScheduleMetadataTest
```

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
cd "$WT"
rtk git add bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp \
  bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp
rtk git commit -m "test(op): cover production loading of historical fork schedules"
```

---

## 关门检查（全部 Task 后）

- [ ] **回归全量相关 suite**

```bash
WT=/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
ninja -C "$WT/build" bcos-evm-opstack-tests test-bcos-tool test-bcos-ledger
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleCodecSuite
"$WT/build/bcos-evm/test/bcos-evm-opstack-tests" --run_test=OpForkScheduleSuite
"$WT/build/bcos-tool/test/test-bcos-tool" --run_test=NodeConfigOpForkScheduleTest
"$WT/build/bcos-ledger/test/test-bcos-ledger" --run_test=OpForkScheduleMetadataTest
```

Expected: 全 PASS。

- [ ] **确认未越界**：`git -C "$WT" diff --name-only` 只含 codec、`OpForkSchedule.{h,cpp}` 及其 4 个测试文件；无 `OpForkId.h` / `OpSchedulerSeam.h` / `engine/bcos-engine/*` / `OpBaseFee.h`。

## 明确不做（S2）

- `OpForkId` 历史档 / Engine 版本表 / extraData / baseFee / caps（S3）。
- L1 fee / 收据 / EVM `configForFork`（S4）。
- ImportedStore / SetCanonical（S5+S6）。
- `delta` / `interop` / `pectra_blob_schedule` 名字。
- 创世 extraData 放行、官方 genesis / superchain registry 接入（S1）。

# specs-tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 `bcos-evm/specs-tests/` 模块，先完成 Ethereum/geth GST 向量迁移（P1–P3），再完成 BCOS baseline 与 OPStack/op-geth golden（P4–P6），使三路径证据可追踪且与 capability-matrix 联动。

**Architecture:** 独立顶层目录 `specs-tests/` 承载 loader、ForkProfileRegistry、manifest、PathAdapter 与 runner；`bcos-evm-specs-tests-core` 不含路径依赖，eth/bcos/opstack adapter 分库；证据语义分 `ReferenceParity` / `BaselineReachability` / `DeviationAssertion` / `UpstreamLiteralParity`；外部资产通过 submodule/CI 下载 + pin，CMake configure 不联网。

**Tech Stack:** C++20、Boost.Test、Boost.PropertyTree（JSON）、CMake/CTest、evmone、现有 `executeViaEth` / `ExecuteViaHost` / `opStackExecuteViaHost`；Python manifest lint；后续 Go golden exporter

## Global Constraints

- **设计 spec：** `docs/superpowers/specs/2026-06-21-bcos-evm-test-system-design.md`（v2）
- **目录名：** `bcos-evm/specs-tests/`（不是 `conformance/`）
- **CMake 开关：** `BCOS_EVM_SPECS_TESTS` 默认 `OFF`；仅 `TESTS=ON && BCOS_EVM_SPECS_TESTS=ON` 时 `add_subdirectory(specs-tests)`
- **实施顺序：** 先 Ethereum（P1–P3），再 BCOS baseline（P4），再 OPStack golden/baseline（P5–P6）
- **证据语义：** `executeViaEth` GST pass ≠ BCOS/OPStack baseline 证明（ADR-001）
- **GST 最终断言：** `expectException + stateRoot + logsHash`；P1 允许 `transitional`（status/post account），manifest 必须标注
- **资产路径 env：** `ETHEREUM_TESTS_ROOT`、`EEST_ROOT`、`OP_GETH_GOLDEN_ROOT` 可覆盖默认路径
- **禁止：** CMake configure 阶段下载 GST/EEST；silent skip；在 `test/fixtures/` 复制官方 GST JSON
- **命令前缀：** 使用 `rtk`（仓库 CLAUDE.md 规则）
- **geth 参考：** `/Users/octopus/octo/code/blockchain-impl/go-ethereum`
- **op-geth 参考：** `/Users/octopus/octo/code/blockchain-impl/op-geth`

---

## File Map（P1 交付范围）

| 路径 | 职责 |
|------|------|
| `bcos-evm/specs-tests/README.md` | 模块说明、fork 扩展清单、资产准备命令 |
| `bcos-evm/specs-tests/CMakeLists.txt` | core/eth 库 + runner targets + CTest labels |
| `bcos-evm/specs-tests/assets/upstream-pins.json` | geth/op-geth/ethereum-tests pin |
| `bcos-evm/specs-tests/assets/ethereum-tests/` | git submodule（GeneralStateTests） |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/EvidenceKind.h` | 证据枚举 |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/ExecutionPath.h` | Reference/BcosBaseline/OpStackBaseline |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/StateTestTypes.h` | StateTestCase、StateSubtest、ExecutionResult |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/ManifestLoader.h` | 读 smoke manifest JSON |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/GeneralStateTestLoader.h` | 官方 GST JSON 解析 |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/ForkProfileRegistry.h` | eth-cancun/eth-prague profile |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/StateTestMatcher.h` | skip/known_diff 查询 |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/StateTestAssert.h` | transitional + final 断言 |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/PathAdapter.h` | adapter 接口 |
| `bcos-evm/specs-tests/include/bcos-evm/specs-tests/ExecuteViaEthAdapter.h` | reference path 执行 |
| `bcos-evm/specs-tests/src/*.cpp` | 上述实现 |
| `bcos-evm/specs-tests/runners/EthGSTSmoke.cpp` | P1 smoke runner |
| `bcos-evm/specs-tests/manifests/schema.json` | manifest JSON schema |
| `bcos-evm/specs-tests/manifests/eth-gst-prague-smoke.json` | 首批 curated case |
| `bcos-evm/specs-tests/manifests/expectations.json` | skip/known_diff 元数据 |
| `bcos-evm/specs-tests/tools/manifest_lint.py` | evidenceId / capabilityRow 校验 |
| `bcos-evm/CMakeLists.txt` | 增加 `BCOS_EVM_SPECS_TESTS` option |
| `.github/workflows/capability-gate.yml` | 增加 specs-tests-smoke-job job（P1 末） |

**复用（只读/薄封装，不搬代码）：**

| 现有文件 | 复用方式 |
|----------|----------|
| `bcos-evm/test/fixtures/EthFixtureAdapter.h` | ExecuteViaEthAdapter 构建 input 时参考 |
| `bcos-evm/test/fixtures/FixtureAssert.h` | transitional post account 断言参考 |
| `bcos-evm/test/state/InMemoryStateView.h` | runner 内 seed pre-state |
| `bcos-evm/eth/ExecuteViaEth.h` | reference 执行入口 |

---

## 并行策略

```
Part A — Ethereum (本计划 P1–P3，必须先完成)
  Task 1 ── Task 2 ── Task 3 ── Task 4 ── Task 5 ── Task 6 ── Task 7 ── Task 8 ── Task 9 ── Task 10 ── Task 11
  (骨架)   (pins)  (types)  (GST)   (registry)(matcher)(adapter)(assert)(runner)(lint)  (CI)

Part B — Ethereum full (P2–P3，Part A 完成后)
  Task 12 (stateRoot/logsHash) → Task 13 (Osaka profile) → Task 14 (EthGSTFull) → Task 15 (EEST) → Task 16 (nightly workflow)

Part C — BCOS baseline (P4)
  Task 17 (HostAdapter) → Task 18 (deviation manifest) → Task 19 (BcosGSTSmoke)

Part D — OPStack (P5–P6，Ethereum 主线稳定后)
  Task 20 (golden schema) → Task 21 (OpStackFee golden) → Task 22 (exporter) → Task 23 (OpStackAdapter) → Task 24 (TE E2E manifest)
```

---

# Part A — P1: Ethereum Skeleton（详细任务）

### Task 1: specs-tests 目录骨架与 CMake 开关

**Files:**
- Create: `bcos-evm/specs-tests/README.md`
- Create: `bcos-evm/specs-tests/CMakeLists.txt`
- Modify: `bcos-evm/CMakeLists.txt`

**Interfaces — Produces:**
- `option(BCOS_EVM_SPECS_TESTS ...)` 在 `bcos-evm/CMakeLists.txt`
- 空 target `bcos-evm-specs-tests-core` 可链接
- README 含资产准备说明

- [ ] **Step 1: 创建目录骨架**

```bash
mkdir -p bcos-evm/specs-tests/{assets,include/bcos-evm/specs-tests,src,runners,manifests,tools}
```

- [ ] **Step 2: 修改 `bcos-evm/CMakeLists.txt`**

在 `if(TESTS)` 块内改为：

```cmake
option(BCOS_EVM_SPECS_TESTS "Build EVM reference test runners" OFF)

if(TESTS)
    add_subdirectory(test)
    if(BCOS_EVM_SPECS_TESTS)
        add_subdirectory(specs-tests)
    endif()
endif()
```

- [ ] **Step 3: 创建最小 `bcos-evm/specs-tests/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.28)

add_library(bcos-evm-specs-tests-core STATIC
    src/ReferenceTestsPlaceholder.cpp
)

target_include_directories(bcos-evm-specs-tests-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(bcos-evm-specs-tests-core PUBLIC
    bcos-utilities
)
```

创建占位源文件 `src/ReferenceTestsPlaceholder.cpp`：

```cpp
namespace bcos::evm::reference_tests {}
```

- [ ] **Step 4: 验证 configure**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake -S . -B build-ref -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
```

Expected: configure 成功，生成 `bcos-evm-specs-tests-core` target

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/CMakeLists.txt bcos-evm/specs-tests/
rtk git commit -m "$(cat <<'EOF'
feat(specs-tests): add module skeleton and CMake gate

Introduce bcos-evm/specs-tests behind BCOS_EVM_SPECS_TESTS
so reference vector runners stay opt-in and do not affect default builds.
EOF
)"
```

---

### Task 2: upstream-pins 与 ethereum/tests submodule

**Files:**
- Create: `bcos-evm/specs-tests/assets/upstream-pins.json`
- Modify: `.gitmodules`（若仓库根已有则追加）
- Create: `bcos-evm/specs-tests/README.md`（资产章节）

**Interfaces — Produces:**
- `upstream-pins.json` 含 geth/op-geth/ethereum-tests commit 字段
- `resolveEthereumTestsRoot()` 读取 env `ETHEREUM_TESTS_ROOT` 或默认 `assets/ethereum-tests`

- [ ] **Step 1: 写入 `assets/upstream-pins.json`**

```json
{
  "geth": {
    "path": "/Users/octopus/octo/code/blockchain-impl/go-ethereum",
    "tag": "v1.17.3"
  },
  "op_geth": {
    "path": "/Users/octopus/octo/code/blockchain-impl/op-geth",
    "tag": "v1.101702.2"
  },
  "ethereum_tests": {
    "submodule_path": "bcos-evm/specs-tests/assets/ethereum-tests",
    "repo": "https://github.com/ethereum/tests",
    "commit": "<fill-in-step-2>"
  },
  "eest": {
    "release": "v5.1.0",
    "url": "https://github.com/ethereum/execution-spec-tests/releases/download/v5.1.0/fixtures_develop.tar.gz",
    "sha256": "<fill-in-part-b-task-15>"
  }
}
```

Step 2 必须将 `ethereum_tests.commit` 写成 submodule 的实际 SHA；EEST sha256 在 Part B Task 15 再填，P1 不依赖 EEST。

- [ ] **Step 2: 添加 submodule**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git submodule add --depth 1 https://github.com/ethereum/tests \
  bcos-evm/specs-tests/assets/ethereum-tests
cd bcos-evm/specs-tests/assets/ethereum-tests && rtk git rev-parse HEAD
```

将输出的 commit 写回 `upstream-pins.json`。

- [ ] **Step 3: 在 README 记录本地运行前置**

```markdown
## Assets

```bash
git submodule update --init --depth 1 bcos-evm/specs-tests/assets/ethereum-tests
export ETHEREUM_TESTS_ROOT=/path/to/GeneralStateTests/parent  # optional override
```
```

- [ ] **Step 4: Commit**

```bash
rtk git add .gitmodules bcos-evm/specs-tests/assets/upstream-pins.json \
  bcos-evm/specs-tests/README.md bcos-evm/specs-tests/assets/ethereum-tests
rtk git commit -m "$(cat <<'EOF'
chore(specs-tests): pin ethereum/tests submodule and upstream metadata
EOF
)"
```

---

### Task 3: 核心类型与 Manifest schema

**Files:**
- Create: `include/bcos-evm/specs-tests/EvidenceKind.h`
- Create: `include/bcos-evm/specs-tests/ExecutionPath.h`
- Create: `include/bcos-evm/specs-tests/StateTestTypes.h`
- Create: `include/bcos-evm/specs-tests/ManifestLoader.h`
- Create: `src/ManifestLoader.cpp`
- Create: `manifests/schema.json`
- Create: `manifests/eth-gst-prague-smoke.json`
- Create: `manifests/expectations.json`

**Interfaces — Produces:**

```cpp
// EvidenceKind.h
enum class EvidenceKind {
    ReferenceParity,
    BaselineReachability,
    DeviationAssertion,
    UpstreamLiteralParity
};

// ExecutionPath.h
enum class ExecutionPath { Reference, BcosBaseline, OpStackBaseline };

// StateTestTypes.h
struct StateSubtest {
    std::string fork;
    int dataIndex{};
    int gasIndex{};
    int valueIndex{};
};

struct ExpectedPostState {
    std::optional<std::string> expectException;
    std::optional<evmc_bytes32> stateRoot;
    std::optional<evmc_bytes32> logsHash;
    // transitional fields:
    std::optional<evmc_status_code> status;
    std::optional<int64_t> gasUsed;
};

struct ManifestEntry {
    std::string evidenceId;
    std::string sourceSuite;       // "ethereum-tests"
    std::string casePath;          // relative GST path
    std::string forkProfileId;     // "eth-prague"
    ExecutionPath path;
    EvidenceKind evidenceKind;
    std::vector<std::string> capabilityRowIds;
    std::vector<std::string> assertLevels;  // ["transitional"] or ["expectException","stateRoot","logsHash"]
};

std::vector<ManifestEntry> loadManifest(std::filesystem::path const& jsonPath);
```

- [ ] **Step 1: 实现三个 enum/struct 头文件**（内容见 Interfaces）

- [ ] **Step 2: 实现 `ManifestLoader.cpp`**

使用 Boost.PropertyTree 解析 JSON array；字段缺失时 `BOOST_THROW_EXCEPTION`。

- [ ] **Step 3: 创建首批 smoke manifest**

`manifests/eth-gst-prague-smoke.json` 选 3–5 个已知可在 `executeViaEth` 跑通的 imported 等价 GST case（优先 `stCall_emptyAccount`、`stRevert_revertBasic` 等在 geth GeneralStateTests 中的对应路径；若路径未知，先用 manifest 指向具体 GST 文件相对路径）。

每条 entry 示例：

```json
{
  "evidenceId": "eth.gst.prague.smoke.call_empty_account",
  "sourceSuite": "ethereum-tests",
  "casePath": "GeneralStateTests/stCallCodes/callcodeEmptyContract.json",
  "forkProfileId": "eth-prague",
  "path": "Reference",
  "evidenceKind": "ReferenceParity",
  "capabilityRowIds": ["eip2929-runtime-warm"],
  "assertLevels": ["transitional"]
}
```

- [ ] **Step 4: 创建空 expectations 骨架**

```json
{ "expectations": [] }
```

- [ ] **Step 5: 单元测试 ManifestLoader**

Create: `bcos-evm/specs-tests/runners/ManifestLoaderTest.cpp`

```cpp
#define BOOST_TEST_MODULE ManifestLoaderTest
#include "bcos-evm/specs-tests/ManifestLoader.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(loads_prague_smoke_manifest)
{
    auto entries = bcos::evm::reference_tests::loadManifest(
        "bcos-evm/specs-tests/manifests/eth-gst-prague-smoke.json");
    BOOST_REQUIRE_GE(entries.size(), 1u);
    BOOST_CHECK_EQUAL(entries.front().evidenceKind, bcos::evm::reference_tests::EvidenceKind::ReferenceParity);
}
```

- [ ] **Step 6: CMake 增加 ManifestLoaderTest**

```cmake
add_executable(ManifestLoaderTest runners/ManifestLoaderTest.cpp src/ManifestLoader.cpp)
target_link_libraries(ManifestLoaderTest PRIVATE bcos-evm-specs-tests-core Boost::unit_test_framework)
add_test(NAME ManifestLoaderTest COMMAND ManifestLoaderTest)
set_tests_properties(ManifestLoaderTest PROPERTIES LABELS "specs-tests;specs-tests-smoke")
```

- [ ] **Step 7: 运行测试**

```bash
cmake --build build-ref --target ManifestLoaderTest
cd build-ref && ctest -R ManifestLoaderTest -V
```

Expected: PASS

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/specs-tests/
rtk git commit -m "$(cat <<'EOF'
feat(specs-tests): add evidence types and manifest loader
EOF
)"
```

---

### Task 4: GeneralStateTestLoader（官方 GST JSON）

**Files:**
- Create: `include/bcos-evm/specs-tests/GeneralStateTestLoader.h`
- Create: `src/GeneralStateTestLoader.cpp`

**Interfaces — Consumes:** `StateTestTypes.h`
**Interfaces — Produces:**

```cpp
struct StateTestCase {
    std::string name;
    std::filesystem::path sourcePath;
    state::BlockInfo env;
    state::Transaction tx;
    std::vector<std::pair<evmc_address, state::Account>> preState;
    std::map<std::string, std::vector<ExpectedPostState>> postByFork;
};

StateTestCase loadGeneralStateTest(std::filesystem::path const& jsonPath);
std::vector<StateSubtest> listSubtests(StateTestCase const& test, std::string_view fork);
ExpectedPostState selectExpected(StateTestCase const& test, StateSubtest const& subtest);
```

P1 范围：支持 legacy GST 单对象 JSON（`env/pre/transaction/post`）；暂不解析 authorization list 全字段；7702 GST case 留 P2。

- [ ] **Step 1: 写 loader 失败测试**

Create: `runners/GeneralStateTestLoaderTest.cpp`

从 `assets/ethereum-tests/GeneralStateTests/` 选一个最小 JSON（如 `stExample/add11.json` 若存在），断言 `postByFork` 非空。

- [ ] **Step 2: 实现 `GeneralStateTestLoader.cpp`**

解析字段对照 geth `tests/state_test_util.go`：
- `env.currentNumber/currentTimestamp/currentGasLimit/currentCoinbase/currentBaseFee/currentRandom/currentExcessBlobGas`
- `pre` accounts: balance/nonce/code/storage
- `transaction` gas/data/value/to + indexes
- `post.<fork>[i].hash/logs/expectException/indexes`

- [ ] **Step 3: 运行 loader 测试**

```bash
cmake --build build-ref --target GeneralStateTestLoaderTest
cd build-ref && ctest -R GeneralStateTestLoaderTest -V
```

Expected: PASS（submodule 已 init）

- [ ] **Step 4: Commit**

```bash
rtk git commit -am "feat(specs-tests): add GeneralStateTestLoader for official GST JSON"
```

---

### Task 5: ForkProfileRegistry（Cancun/Prague）

**Files:**
- Create: `include/bcos-evm/specs-tests/ForkProfileRegistry.h`
- Create: `src/ForkProfileRegistry.cpp`

**Interfaces — Produces:**

```cpp
struct PathProfile {
    ExecutionPath path;
    EvidenceKind evidenceKind;
    std::vector<std::string> enabledCapabilityRows;
    std::optional<std::string> unsupportedReason;
};

struct ForkProfile {
    std::string profileId;
    std::string canonicalName;
    std::vector<std::string> aliases;
    std::string upstreamForkName;
    bcos::evm_standard::RevisionConfig revision;
    std::vector<std::string> activatedEips;
    std::vector<PathProfile> pathProfiles;
};

class ForkProfileRegistry {
public:
    static ForkProfileRegistry const& instance();
    std::optional<ForkProfile> findByProfileId(std::string_view id) const;
    std::optional<ForkProfile> findByUpstreamFork(std::string_view fork) const;
};
```

P1 注册：
- `eth-cancun` → upstream `"Cancun"`, ReferenceParity only
- `eth-prague` → upstream `"Prague"`, ReferenceParity only

- [ ] **Step 1: 写 registry 测试**

```cpp
BOOST_AUTO_TEST_CASE(prague_profile_maps_upstream_fork)
{
    auto profile = ForkProfileRegistry::instance().findByUpstreamFork("Prague");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->profileId, "eth-prague");
}
```

- [ ] **Step 2: 实现 registry**（revision 复用 `EthPolicy` / 现有 `makePragueRevisionConfig` 逻辑）

- [ ] **Step 3: 运行测试并 commit**

---

### Task 6: StateTestMatcher + expectations.json

**Files:**
- Create: `include/bcos-evm/specs-tests/StateTestMatcher.h`
- Create: `src/StateTestMatcher.cpp`
- Modify: `manifests/expectations.json`

**Interfaces — Produces:**

```cpp
struct MatchDecision {
    enum class Kind { Run, Skip, KnownDiff, Deviation } kind;
    std::optional<std::string> reason;
    std::vector<std::string> affectedFields;
};

class StateTestMatcher {
public:
    explicit StateTestMatcher(std::filesystem::path expectationsJson);
    MatchDecision decide(std::string const& caseId, ExecutionPath path) const;
};
```

P1 内置 geth 同源 hard skip（EOF、timeConsuming 等）+ 从 `expectations.json` 加载。

- [ ] **Step 1: 实现 matcher + 测试 skip 规则**
- [ ] **Step 2: Commit**

---

### Task 7: ExecuteViaEthAdapter

**Files:**
- Create: `include/bcos-evm/specs-tests/PathAdapter.h`
- Create: `include/bcos-evm/specs-tests/ExecuteViaEthAdapter.h`
- Create: `src/ExecuteViaEthAdapter.cpp`
- Create: `bcos-evm/specs-tests/CMakeLists.txt` 中 `bcos-evm-specs-tests-eth` 库

**Interfaces — Consumes:** `StateTestCase`, `ForkProfile`, 现有 `executeViaEth`
**Interfaces — Produces:**

```cpp
class ExecuteViaEthAdapter : public PathAdapter {
public:
    ExecutionPath path() const override { return ExecutionPath::Reference; }
    bool supports(ForkProfile const& profile, std::string_view capabilityRowId) const override;
    task::Task<ExecutionResult> execute(StateTestCase const&, StateSubtest const&) override;
};
```

实现要点：
- seed `InMemoryStateView` from `preState`
- 用 `ForkProfile.revision` 构建 `ExecuteViaEthInput`
- 调用 `task::syncWait(executeViaEth(...))`
- 返回 `ExecutionResult{status, gasUsed, output, stateDiff, rejectionReason}`

- [ ] **Step 1: 写 adapter 集成测试**（对一个 manifest case 执行，断言 status 非 internal error）
- [ ] **Step 2: 实现 adapter**
- [ ] **Step 3: Commit**

---

### Task 8: StateTestAssert（transitional P1）

**Files:**
- Create: `include/bcos-evm/specs-tests/StateTestAssert.h`
- Create: `src/StateTestAssert.cpp`

**Interfaces — Produces:**

```cpp
struct AssertReport {
    bool passed{};
    std::string message;
};

AssertReport assertResult(
    ManifestEntry const& entry,
    ExpectedPostState const& expected,
    ExecutionResult const& actual,
    int64_t gasBefore);
```

P1 逻辑：
- 若 `assertLevels` 含 `transitional`：检查 `expectException` 或 `status`；可选 post account diff
- 若含 `stateRoot`/`logsHash`：P2 才强制
- `KnownDiff` 仅允许 manifest 声明字段

- [ ] **Step 1: 实现 + 单元测试**
- [ ] **Step 2: Commit**

---

### Task 9: EthGSTSmoke runner

**Files:**
- Create: `bcos-evm/specs-tests/runners/EthGSTSmoke.cpp`
- Modify: `bcos-evm/specs-tests/CMakeLists.txt`

**Interfaces — Consumes:** 全部 P1 组件

- [ ] **Step 1: 实现 runner CLI**

```cpp
// argv: --manifest <path> [--ethereum-tests-root <path>]
int main(int argc, char** argv)
{
    auto manifest = loadManifest(manifestPath);
    for (auto const& entry : manifest) {
        auto gstPath = ethereumTestsRoot / entry.casePath;
        auto testCase = loadGeneralStateTest(gstPath);
        auto profile = *ForkProfileRegistry::instance().findByProfileId(entry.forkProfileId);
        auto subtests = listSubtests(testCase, profile.upstreamForkName);
        for (auto const& subtest : subtests) {
            // matcher → adapter → assert
        }
    }
}
```

- [ ] **Step 2: CMake add_test**

```cmake
add_executable(EthGSTSmoke runners/EthGSTSmoke.cpp ...)
add_test(NAME EthGSTSmoke COMMAND EthGSTSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth-gst-prague-smoke.json
    --ethereum-tests-root $<IF:$<BOOL:${ETHEREUM_TESTS_ROOT}>,${ETHEREUM_TESTS_ROOT},${CMAKE_CURRENT_SOURCE_DIR}/assets/ethereum-tests>)
set_tests_properties(EthGSTSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel")
```

- [ ] **Step 3: 运行**

```bash
cmake --build build-ref --target EthGSTSmoke
cd build-ref/bcos-evm/specs-tests && ctest -R EthGSTSmoke -V
```

Expected: smoke manifest 全部 PASS（或已知 case 写入 expectations.json 后 PASS）

- [ ] **Step 4: Commit**

```bash
rtk git commit -am "feat(specs-tests): add EthGSTSmoke runner for Prague curated GST"
```

---

### Task 10: manifest_lint.py（最小版）

**Files:**
- Create: `bcos-evm/specs-tests/tools/manifest_lint.py`
- Create: `bcos-evm/specs-tests/manifests/capability-rows.json`（从 matrix 提取的 row id 列表）

- [ ] **Step 1: 实现 lint**

校验：
- `evidenceId` 唯一
- `capabilityRowIds` 均存在于 `capability-rows.json`
- `evidenceKind=ReferenceParity` 时 `path` 必须是 `Reference`
- expectations 每条有 `reasonClass`、`issueOrAdr`、`reviewBy`

```bash
python3 bcos-evm/specs-tests/tools/manifest_lint.py \
  --manifests bcos-evm/specs-tests/manifests \
  --capability-rows bcos-evm/specs-tests/manifests/capability-rows.json
```

Expected: exit 0

- [ ] **Step 2: Commit**

---

### Task 11: capability-gate smoke CI job

**Files:**
- Modify: `.github/workflows/capability-gate.yml`

- [ ] **Step 1: 增加 paths 触发**

```yaml
paths:
  - 'bcos-evm/specs-tests/**'
  - 'bcos-evm/eth/**'
```

- [ ] **Step 2: 增加 job `specs-tests-smoke-job`**

步骤：checkout + submodule init + configure `-DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON` + build EthGSTSmoke + `ctest -R EthGSTSmoke -V`

- [ ] **Step 3: Commit**

---

## P1 验收清单

- [ ] `cmake -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON` 可构建
- [ ] `ctest -L specs-tests-smoke` 绿
- [ ] 现有 `bcos-evm/test/` 默认行为不变（`BCOS_EVM_SPECS_TESTS=OFF`）
- [ ] README 说明资产准备与 env 覆盖
- [ ] manifest 中 smoke case 均标 `assertLevels: ["transitional"]`

---

# Part B — P2–P3: Ethereum Full（任务概要）

### Task 12: Final P0 断言 — stateRoot + logsHash

**Files:**
- Modify: `src/StateTestAssert.cpp`
- Modify: `eth/state/*`（若缺 root/logs 计算设施则在此补）
- Modify: smoke manifest `assertLevels` 升级选取的 case

**验收:** 选定 Prague case 同时断言 `expectException + stateRoot + logsHash`

---

### Task 13: 注册 eth-osaka profile + Osaka smoke manifest

**Files:**
- Modify: `src/ForkProfileRegistry.cpp`
- Create: `manifests/eth-gst-osaka-smoke.json`
- Modify: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`（验证 osaka profile）

**验收:** `ExecuteViaEthAdapter` 可跑 Osaka GST smoke；仅 `ReferenceParity`

---

### Task 14: EthGSTFull nightly runner

**Files:**
- Create: `runners/EthGSTFull.cpp`
- Create: `.github/workflows/specs-tests-nightly.yml`

**验收:** `ctest -L specs-tests-full -R EthGSTFull` 跑 Cancun/Prague/Osaka 全 GST（geth 同源 skip）

---

### Task 15: EEST loader + EthExecutionSpecStateTests

**Files:**
- Create: `include/bcos-evm/specs-tests/EestStateTestLoader.h`
- Create: `runners/EthExecutionSpecStateTests.cpp`
- Modify: `assets/upstream-pins.json`（填 EEST sha256）
- CI: nightly 下载 EEST tarball 到 `assets/eest`

**验收:** Prague/7702、7623 EEST 子集 nightly 绿

---

### Task 16: EthExecutionSpecTransactionTests（后续）

**验收:** 7702 tx 编码/validation 子集

---

# Part C — P4: BCOS Baseline（任务概要）

### Task 17: ExecuteViaHostAdapter + bcos-specs-tests-bcos 库

**验收:** Cancun/Prague baseline GST 子集；证据 kind = `BaselineReachability`

### Task 18: Deviation manifest + Bcos21000GasDeviation 接入 expectations

**验收:** deviation 行有正向断言，不 skip GST

### Task 19: BcosGSTSmoke runner

**验收:** `ctest -R BcosGSTSmoke`；不得把结果记为 ReferenceParity

### Task 20: Osaka on BCOS baseline（Phase 2 of P4）

**验收:** `feature_evm_osaka` profile 下 7212/7823 capability rows

---

# Part D — P5–P6: OPStack（任务概要）

### Task 21: op-geth-golden schema + 手工 rollup_cost golden

**Files:**
- Create: `assets/op-geth-golden/rollup_cost.json`
- Create: `include/bcos-evm/specs-tests/OpGethGoldenLoader.h`

**验收:** `OpStackFeeTest` 读 golden；metadata 含 op-geth commit

### Task 22: export_opgeth_golden.go + regenerate_golden.sh --check

**验收:** nightly golden check；无现成 upstream 工具，本项目自建 exporter

### Task 23: receipt/deposit golden + OpStackReceiptTest

### Task 24: OpStackAdapter + OpStackGSTSmoke + TE manifest 扩展

**验收:** OPStack `explicit` matrix 行有 baseline/literal evidence

---

# Part E — P7: BlockchainTests（可选）

### Task 25: BlockchainTestLoader + EthBlockchainTests runner

**验收:** 多块场景 smoke；EEST blockchain 子集

---

## Spec Coverage Self-Review

| Spec 章节 | 对应 Task |
|-----------|-----------|
| §3.1 证据语义 | Task 3, 7, 17, 21 |
| §2.3 Upstream Pins | Task 2, 15 |
| §4 独立目录 | Task 1 |
| §5.1 GST Loader | Task 4 |
| §5.2 ForkProfileRegistry | Task 5, 13 |
| §5.3 Matcher/expectations | Task 6 |
| §5.3.1 Manifest Evidence | Task 3, 10 |
| §5.4 PathAdapter | Task 7, 17, 24 |
| §5.5 StateTestAssert | Task 8, 12 |
| §5.6 OPStack golden | Task 21–23 |
| §6 CI/labels | Task 11, 14 |
| §7 P1–P7 阶段 | Part A–E |
| §8 风险 | Task 6, 8, 10（no silent skip） |

无 TBD 步骤留在 P1 任务内；`upstream-pins.json` 中 submodule commit / EEST sha256 在 Task 2/15 填实。

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-21-specs-tests-implementation.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — 每个 Task 派一个全新 subagent，Task 间做 review，迭代快
2. **Inline Execution** — 在本 session 用 executing-plans 按 Task 批量执行，checkpoint 处 review

**建议从 Part A Task 1 开始。** 你希望用哪种方式？

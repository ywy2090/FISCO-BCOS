# ETH Kernel Capability Inheritance — Remaining Work Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Revision:** 2026-06-20 post-grill — fixes Isthmus snapshot, 7623 success case, fixture naming, auth test scope, §9 honesty, ADR-007.

**Goal:** Close these **specific** design §8 Open items: CMake drift recovery, `RevisionConfig` fork snapshots (Eth/Fisco full tables + Isthmus sparse documented), BCOS 7623 entry precheck test, TE baseline imported-fixture smoke, kernel EIP-2537 test, matrix/tracker sync, Web3 decoder decision (ADR-007). **Does not** claim full §9 Met.

**Architecture:** Inheritance boundary stays at `executeMessage()`. BCOS/OP baseline proof uses `executeViaHost` / `opStackExecuteViaHost` orchestrator tests (ADR-002 §7), not `executeViaEth`. Matrix at `bcos-evm/capability-matrix.md` is normative; **Test ref** must match test scope (kernel vs baseline vs hook-only).

**Tech Stack:** C++17/20, Boost.Test, CMake/CTest, evmone, `bcos-evm` / `bcos-evm-op` / `transaction-executor`, GitHub Actions `capability-gate`.

## Global Constraints

- TE baseline paths only: BCOS `TransactionExecutorImpl` → `executeViaHost` → `executeMessage`; OPStack `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` → `executeMessage`. **`executeViaEth` is reference-only** (ADR-001).
- Normative matrix: `bcos-evm/capability-matrix.md` — update in same PR as capability-surface or test-ref changes.
- `RevisionConfig` bool fields: `REVISION_CONFIG_BOOL_FIELDS(X)` + `revisionConfigBoolFieldCount() == 13`.
- BCOS EIP-7702 precheck/intrinsic: **unsupported**, ADR-006 deferred — out of scope.
- BCOS `executeViaHost` **always** debits `BALANCE_TRANSFER_GAS` (21000) after orchestration prechecks — account for this in baseline tests (see `Bcos21000GasDeviationTest`).
- CI: `capability-gate.yml` + `check-capability-matrix.sh`; `RevisionConfig.h` → `RevisionConfigProfileTest.cpp` diff required.
- Branch: `feat-evm-refactor`.

---

## As-built inventory (2026-06-20)

| Spec §8 item | Status | Evidence |
| --- | --- | --- |
| Phase 1 matrix + ADR-001–006 + CI gate | **Done** | `capability-matrix.md`, ADRs, workflow on branch |
| BCOS/OP 7702 orchestrator E2E | **Done** | `Bcos7702*`, `OpStack7702*` (BCOS CMake drift — Task 0) |
| OP L1Block HostExtension E2E | **Done** | `L1BlockGetterTest` |
| Builder + profile tests (source) | **Done** | `*TxInputBuilderTest`, `RevisionConfigProfileTest` (CMake drift — Task 0) |
| Full Eth/Fisco fork snapshot tables | **Open** | Task 1 |
| Isthmus helper sparse profile documented | **Open** | Task 1 (separate assert) |
| BCOS 7623 entry precheck | **Open** | Task 2 |
| Imported fixture → `executeViaHost` pipeline | **Open** | Task 3 (not 7702 delegation E2E) |
| EIP-2537 kernel test ref | **Open** | Task 4 (kernel row only) |
| BCOS auth orchestrator hook test | **Open** | Task 5 (hook-only; not full `AuthCheck`) |
| Web3 decoder decision | **Open** | Task 6 (ADR-007) |
| capability-gate on mainline | **Open** | Appendix A (human) |

---

## File map

| File | Action |
| --- | --- |
| `bcos-evm/test/CMakeLists.txt` | Restore targets + optional drift guard comment |
| `bcos-evm/test/eth/RevisionConfigProfileTest.cpp` | Eth/Fisco full snapshots + Isthmus sparse snapshot |
| `bcos-evm/test/bcos/Bcos7623PrecheckTest.cpp` | Create |
| `bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp` | Create (renamed from generic "OrchestratorTest") |
| `bcos-evm/test/bcos/ExecuteViaHostImportedFixtureTest.cpp` | Create |
| `bcos-evm/test/fixtures/FiscoFixtureAdapter.h` | Create |
| `bcos-evm/test/fixtures/HostFixtureAssert.h` | Create |
| `bcos-evm/test/eth/Eip2537KernelTest.cpp` | Create |
| `bcos-evm/docs/adr/007-te-web3-decoder-dependency.md` | Create |
| `bcos-evm/capability-matrix.md` | Test ref + footnotes |
| `bcos-evm/docs/inheritance-work-tracker.md` | Sync |
| `bcos-evm/docs/architecture-known-gaps.md` | Link ADR-007 |
| `docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md` | §8/§9/§11 |

---

### Task 0: Restore inheritance-contract CTest registrations (CMake drift)

**Root cause (document in commit message):** inheritance-contract targets were appended in an earlier commit then **truncated** from `bcos-evm/test/CMakeLists.txt` tail (file ends at line ~899 with only `OpStack7702*` + `ExecuteViaEthFixtureTest`; sources remain on disk).

**Done when:** All listed targets build **and** full inheritance subset passes ctest (Step 3).

**Files:** `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1: Append missing CMake block** (after `OpStack7702ExecuteViaHostPropagation` block)

```cmake
function(add_te_input_builder_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/transaction-executor)
    target_link_libraries(${NAME} PRIVATE
        bcos-evm bcos-evm-op executor protocol-tars bcos-crypto bcos-codec)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_te_input_builder_test(EthTxInputBuilderTest eth/EthTxInputBuilderTest.cpp)
add_te_input_builder_test(FiscoTxInputBuilderTest eth/FiscoTxInputBuilderTest.cpp)
add_te_input_builder_test(OpStackTxInputBuilderTest opstack/OpStackTxInputBuilderTest.cpp)

add_executable(RevisionConfigProfileTest eth/RevisionConfigProfileTest.cpp)
target_include_directories(RevisionConfigProfileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(RevisionConfigProfileTest PRIVATE
    bcos-evm bcos-evm-eth protocol-tars bcos-framework)
add_test(NAME RevisionConfigProfile COMMAND RevisionConfigProfileTest)

add_executable(TxFeaturePrepareTest eth/TxFeaturePrepareTest.cpp)
target_include_directories(TxFeaturePrepareTest PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(TxFeaturePrepareTest PRIVATE bcos-evm-eth)
add_test(NAME TxFeaturePrepare COMMAND TxFeaturePrepareTest)

add_executable(OpStackTxPropsTest opstack/OpStackTxPropsTest.cpp)
target_include_directories(OpStackTxPropsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/transaction-executor)
target_link_libraries(OpStackTxPropsTest PRIVATE bcos-evm-op bcos-evm-eth)
add_test(NAME OpStackTxProps COMMAND OpStackTxPropsTest)

add_executable(Bcos21000GasDeviationTest bcos/Bcos21000GasDeviationTest.cpp)
target_include_directories(Bcos21000GasDeviationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos21000GasDeviationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos21000GasDeviation COMMAND Bcos21000GasDeviationTest)

add_executable(Bcos7702ExecuteViaHostPropagationTest bcos/Bcos7702ExecuteViaHostPropagationTest.cpp)
target_include_directories(Bcos7702ExecuteViaHostPropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7702ExecuteViaHostPropagationTest PRIVATE
    bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos7702ExecuteViaHostPropagation COMMAND Bcos7702ExecuteViaHostPropagationTest)
```

- [ ] **Step 2: Build all restored targets**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --target RevisionConfigProfileTest Bcos7702ExecuteViaHostPropagationTest \
  Bcos21000GasDeviationTest EthTxInputBuilderTest FiscoTxInputBuilderTest \
  OpStackTxInputBuilderTest TxFeaturePrepareTest OpStackTxPropsTest \
  OpStack7702ExecuteViaHostPropagationTest -j
```

- [ ] **Step 3: Run full inheritance-contract ctest subset**

```bash
ctest --test-dir build -R "RevisionConfigProfile|Bcos7702ExecuteViaHostPropagation|Bcos21000GasDeviation|EthTxInputBuilder|FiscoTxInputBuilder|OpStackTxInputBuilder|TxFeaturePrepare|OpStackTxProps|OpStack7702ExecuteViaHostPropagation|L1BlockGetter|ExecuteViaEthFixture" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
fix(test): restore inheritance-contract CTest targets dropped from CMakeLists tail

EOF
)"
```

---

### Task 1: RevisionConfig fork snapshots (Eth/Fisco full + Isthmus sparse)

**Files:** `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`

**Two assert helpers (do not conflate):**
- `assertRevisionConfigMatches` — **all 13 bools** for `EthPolicy` / `FiscoPolicy`
- `assertIsthmusHelperProfile` — **all 13 bools** with explicit expected values matching sparse `makeIsthmusRevisionConfig()` (unset fields = `false`)

- [ ] **Step 1: Add helpers**

```cpp
struct ExpectedRevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;
#define REVISION_CONFIG_FIELD(name) bool name = false;
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_FIELD)
#undef REVISION_CONFIG_FIELD
    uint8_t calldata_floor_per_token = 0;
};

inline void assertRevisionConfigMatches(
    bcos::evm_standard::RevisionConfig const& actual, ExpectedRevisionConfig const& expected)
{
    BOOST_CHECK_EQUAL(actual.revision, expected.revision);
#define REVISION_CONFIG_ASSERT(name) BOOST_CHECK_EQUAL(actual.name, expected.name);
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_ASSERT)
#undef REVISION_CONFIG_ASSERT
    BOOST_CHECK_EQUAL(actual.calldata_floor_per_token, expected.calldata_floor_per_token);
}

inline void assertIsthmusHelperProfile(bcos::evm_standard::RevisionConfig const& actual)
{
    // Sparse helper — only documents fields makeIsthmusRevisionConfig() sets today (ADR-004 profile-only rest stay false).
    ExpectedRevisionConfig expected{};
    expected.revision = EVMC_PRAGUE;
    expected.eip7623 = true;
    expected.eip7702 = true;
    expected.eip4844 = true;
    expected.prague_post_execution = false;
    expected.calldata_floor_per_token = 10;
    assertRevisionConfigMatches(actual, expected);
}
```

- [ ] **Step 2: Add Eth + Fisco table tests**

```cpp
BOOST_AUTO_TEST_CASE(eth_policy_full_fork_snapshots)
{
    EthPolicy policy;
    struct Row { int64_t block; ExpectedRevisionConfig expected; };
    std::vector<Row> const rows = {
        {15'537'394, {.revision = EVMC_PARIS, .warm_access = true, .eip1559 = true}},
        {17'034'870, {.revision = EVMC_SHANGHAI, .warm_access = true, .eip1559 = true}},
        {19'426'587, {.revision = EVMC_CANCUN, .warm_access = true, .eip1153 = true,
             .eip4844 = true, .eip5656 = true, .eip6780 = true, .eip1559 = true}},
        {22'000'000, {.revision = EVMC_PRAGUE, .warm_access = true, .eip1153 = true,
             .eip4844 = true, .eip5656 = true, .eip6780 = true, .eip2537 = true,
             .eip7623 = true, .eip7702 = true, .eip1559 = true, .calldata_floor_per_token = 10}},
        {25'000'000, {.revision = EVMC_OSAKA, .warm_access = true, .eip1153 = true,
             .eip4844 = true, .eip5656 = true, .eip6780 = true, .eip2537 = true,
             .eip7623 = true, .eip7702 = true, .eip7212 = true, .eip7823 = true,
             .eip1559 = true, .calldata_floor_per_token = 10}},
    };
    for (auto const& row : rows)
    {
        BOOST_TEST_CONTEXT("block=" << row.block)
        {
            auto header = makeHeader(row.block,
                static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
            assertRevisionConfigMatches(policy.computeRevisionConfig(header), row.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(fisco_policy_feature_gate_snapshots)
{
    using Flag = ledger::Features::Flag;
    struct Row { std::function<void(ledger::Features&)> setup; ExpectedRevisionConfig expected; };
    std::vector<Row> const rows = {
        {[&](ledger::Features& f) { f.set(Flag::feature_evm_cancun); },
            {.revision = EVMC_CANCUN, .warm_access = true, .eip1153 = true, .eip4844 = true,
                .eip5656 = true, .eip6780 = true, .eip1559 = true}},
        {[&](ledger::Features& f) { f.set(Flag::feature_evm_cancun); f.set(Flag::feature_evm_prague); },
            {.revision = EVMC_PRAGUE, .warm_access = true, .eip1153 = true, .eip4844 = true,
                .eip5656 = true, .eip6780 = true, .eip2537 = true, .eip7623 = true,
                .eip7702 = true, .eip1559 = true, .calldata_floor_per_token = 10}},
        {[&](ledger::Features& f) {
             f.set(Flag::feature_evm_cancun); f.set(Flag::feature_evm_prague);
             f.set(Flag::feature_evm_osaka);
         },
            {.revision = EVMC_OSAKA, .warm_access = true, .eip1153 = true, .eip4844 = true,
                .eip5656 = true, .eip6780 = true, .eip2537 = true, .eip7623 = true,
                .eip7702 = true, .eip7212 = true, .eip7823 = true, .eip1559 = true,
                .calldata_floor_per_token = 10}},
        {[&](ledger::Features& /*unused*/) {},
            {.revision = EVMC_CANCUN, .warm_access = true, .eip1153 = true, .eip4844 = true,
                .eip5656 = true, .eip6780 = true, .eip1559 = true}},
    };
    for (auto const& row : rows)
    {
        ledger::Features features;
        row.setup(features);
        bcos::chain_policy::FiscoPolicy policy(features, false, false);
        auto header = makeHeader(1, static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
        assertRevisionConfigMatches(policy.computeRevisionConfig(header).eth(), row.expected);
    }
}

BOOST_AUTO_TEST_CASE(isthmus_helper_sparse_profile_all_fields)
{
    assertIsthmusHelperProfile(makeIsthmusRevisionConfig());
}
```

Keep existing spot tests (`isthmus_profile_enables_prague_kernel_flags`, etc.) — redundant but harmless.

- [ ] **Step 3: Run + commit**

```bash
cmake --build build --target RevisionConfigProfileTest -j
ctest --test-dir build -R RevisionConfigProfile -V
rtk git add bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(eth): add RevisionConfig fork snapshots and documented Isthmus sparse profile

EOF
)"
```

---

### Task 2: BCOS EIP-7623 entry precheck test

**Scope:** Prove `web3Tx && eip7623` OOG **before** EVM when `gas < normalCost`. Do **not** test post-precheck EVM with nonzero calldata to empty account (hits `NotFoundCodeError` + 21000 debit — see `ExecuteViaHost.cpp`).

**Files:** `bcos-evm/test/bcos/Bcos7623PrecheckTest.cpp`, `CMakeLists.txt`, `capability-matrix.md`

- [ ] **Step 1: Create test**

```cpp
#define BOOST_TEST_MODULE Bcos7623PrecheckTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(executeViaHost_web3Tx_eip7623_oog_when_gas_below_normal_cost)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes calldata{0x01};
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(calldata));
    BOOST_REQUIRE_GT(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = components.normalCost - 1;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.web3Tx = true;
    input.revisionConfig.eth().revision = EVMC_PRAGUE;
    input.revisionConfig.eth().eip7623 = true;

    auto output = task::syncWait(executeViaHost(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
}

BOOST_AUTO_TEST_CASE(executeViaHost_web3Tx_eip7623_skips_precheck_when_normal_cost_zero)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x03);
    auto const target = addressFromLastByte(0x04);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes emptyCalldata;
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(emptyCalldata));
    BOOST_CHECK_EQUAL(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = emptyCalldata.data();
    message.input_size = 0;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.web3Tx = true;
    input.revisionConfig.eth().revision = EVMC_PRAGUE;
    input.revisionConfig.eth().eip7623 = true;

    auto output = task::syncWait(executeViaHost(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}
```

- [ ] **Step 2: Register CTest**

```cmake
add_executable(Bcos7623PrecheckTest bcos/Bcos7623PrecheckTest.cpp)
target_include_directories(Bcos7623PrecheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7623PrecheckTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME Bcos7623Precheck COMMAND Bcos7623PrecheckTest)
```

- [ ] **Step 3: Matrix** — EIP-7623 entry precheck Test ref: `` `Bcos7623PrecheckTest` ``

- [ ] **Step 4: Run + commit**

```bash
cmake --build build --target Bcos7623PrecheckTest -j
ctest --test-dir build -R Bcos7623Precheck -V
rtk git add bcos-evm/test/bcos/Bcos7623PrecheckTest.cpp bcos-evm/test/CMakeLists.txt bcos-evm/capability-matrix.md
rtk git commit -m "$(cat <<'EOF'
test(bcos): cover EIP-7623 web3Tx entry precheck in executeViaHost

EOF
)"
```

---

### Task 3: TE baseline imported-fixture smoke (`executeViaHost` adapter)

**Scope clarification:** Closes tracker **#34** as **fixture pipeline infra**, not EIP-7702 delegation E2E (that is `Bcos7702ExecuteViaHostPropagationTest`). File `stEIP7702_delegation.json` is a **misnomer** — it runs a plain CALL to `0xbb` returning 42; test name documents this.

**Files:** `FiscoFixtureAdapter.h`, `HostFixtureAssert.h`, `ExecuteViaHostImportedFixtureTest.cpp`, `CMakeLists.txt`, `architecture-known-gaps.md`, tracker #34 text

- [ ] **Step 1: Create `FiscoFixtureAdapter.h`**

```cpp
#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test::fixtures
{

inline bcos::evm_standard::RevisionConfig revisionConfigFromFixtureRevision(std::string const& revision)
{
    bcos::evm_standard::RevisionConfig cfg;
    if (revision == "prague")
    {
        cfg.revision = EVMC_PRAGUE;
        cfg.eip7702 = true;
        cfg.eip7623 = true;
        cfg.eip2537 = true;
        cfg.calldata_floor_per_token = 10;
    }
    else if (revision == "cancun")
    {
        cfg.revision = EVMC_CANCUN;
    }
    else
    {
        BOOST_FAIL("unsupported fixture revision: " << revision);
    }
    cfg.warm_access = true;
    cfg.eip1153 = cfg.revision >= EVMC_CANCUN;
    cfg.eip4844 = cfg.revision >= EVMC_CANCUN;
    cfg.eip5656 = cfg.revision >= EVMC_CANCUN;
    cfg.eip6780 = cfg.revision >= EVMC_CANCUN;
    return cfg;
}

inline ExecuteViaHostInput buildExecuteViaHostInput(FixtureCase const& fixture,
    state::StateView const& stateView, evmc::VM& vm, bcos::crypto::Hash const& hashImpl)
{
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;

    evmc_message msg{};
    msg.kind = fixture.tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.flags = fixture.txProps.isStatic ? EVMC_STATIC : 0;
    msg.gas = fixture.tx.gasLimit;
    msg.sender = fixture.tx.from;
    msg.recipient = fixture.tx.to.value_or(evmc_address{});
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();
    msg.value = state::toEvmC(fixture.tx.value);
    input.message = msg;
    input.blockInfo = fixture.block;
    input.gasPrice = fixture.tx.gasPrice;
    input.web3Tx = true;
    input.revisionConfig.eth() = revisionConfigFromFixtureRevision(fixture.revision);
    return input;
}

}  // namespace bcos::evm::test::fixtures
```

- [ ] **Step 2: Create `HostFixtureAssert.h`**

```cpp
#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test::fixtures
{

inline void assertHostFixtureResult(
    FixtureCase const& fixture, ExecuteViaHostOutput const& output, int64_t gasBefore)
{
    BOOST_CHECK_EQUAL(
        static_cast<int>(output.evmcResult.status_code), static_cast<int>(fixture.expected.status));
    bcos::bytes actual(output.evmcResult.output_data,
        output.evmcResult.output_data + output.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    if (fixture.expected.gasUsed != 0)
    {
        int64_t const actualGas = gasBefore - output.evmcResult.gas_left;
        BOOST_CHECK_LE(std::abs(actualGas - fixture.expected.gasUsed),
            fixture.expected.gasUsedTolerance);
    }
}

}  // namespace bcos::evm::test::fixtures
```

- [ ] **Step 3: Create `ExecuteViaHostImportedFixtureTest.cpp`**

```cpp
#define BOOST_TEST_MODULE ExecuteViaHostImportedFixtureTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/FiscoFixtureAdapter.h"
#include "fixtures/HostFixtureAssert.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

BOOST_AUTO_TEST_CASE(imported_fixture_plain_call_via_execute_via_host)
{
    // stEIP7702_delegation.json: historical filename; plain CALL to 0xbb returning 42 — not auth-list 7702.
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto const path =
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stEIP7702_delegation.json"
#else
        std::filesystem::path("fixtures/state/imported/stEIP7702_delegation.json")
#endif
        ;
    auto fixture = loadFixture(path);
    state::test::InMemoryStateView view;
    for (auto const& [addr, acct] : fixture.preState)
        view.insert_account(addr, acct);
    auto input = buildExecuteViaHostInput(fixture, view, vm, hashImpl);
    int64_t const gasBefore = input.message.gas;
    auto output = task::syncWait(executeViaHost(std::move(input)));
    assertHostFixtureResult(fixture, output, gasBefore);
}
}  // namespace bcos::evm::test
```

- [ ] **Step 4: Register CTest**

```cmake
add_executable(ExecuteViaHostImportedFixtureTest bcos/ExecuteViaHostImportedFixtureTest.cpp)
target_include_directories(ExecuteViaHostImportedFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(ExecuteViaHostImportedFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(ExecuteViaHostImportedFixtureTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-crypto)
add_test(NAME ExecuteViaHostImportedFixture COMMAND ExecuteViaHostImportedFixtureTest)
```

- [ ] **Step 5: Update known-gaps** — change `stEIP7702_delegation.json` row to **Closed** via `ExecuteViaHostImportedFixtureTest` (pipeline only).

- [ ] **Step 6: Run + commit**

```bash
cmake --build build --target ExecuteViaHostImportedFixtureTest -j
ctest --test-dir build -R ExecuteViaHostImportedFixture -V
rtk git add bcos-evm/test/fixtures/FiscoFixtureAdapter.h bcos-evm/test/fixtures/HostFixtureAssert.h \
  bcos-evm/test/bcos/ExecuteViaHostImportedFixtureTest.cpp bcos-evm/test/CMakeLists.txt \
  bcos-evm/docs/architecture-known-gaps.md
rtk git commit -m "$(cat <<'EOF'
test(bcos): add executeViaHost imported-fixture adapter smoke test

EOF
)"
```

---

### Task 4: EIP-2537 kernel contract test (kernel row only)

**Matrix rule:** Add `` `Eip2537KernelTest` `` to **EIP-2537 precompiles (0x0b–0x11) kernel** row only. **Do not** imply BCOS TE baseline-reachable (BCOS row stays `feature-gated`).

**Files:** `bcos-evm/test/eth/Eip2537KernelTest.cpp`, `CMakeLists.txt`, `capability-matrix.md`

- [ ] **Step 1: Create test** (direct `executeMessage`, `stBLS_add.json`, `EVMC_PRAGUE`)

```cpp
#define BOOST_TEST_MODULE Eip2537KernelTest

#include "bcos-evm/eth/executeMessage.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

BOOST_AUTO_TEST_CASE(stBLS_add_precompile_0x0b_via_executeMessage)
{
#ifdef ETH_STATE_FIXTURES_DIR
    auto const path = std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stBLS_add.json";
#else
    auto const path = std::filesystem::path("fixtures/state/imported/stBLS_add.json");
#endif
    auto fixture = loadFixture(path);
    state::test::InMemoryStateView view;
    for (auto const& [addr, acct] : fixture.preState)
        view.insert_account(addr, acct);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = fixture.tx.gasLimit;
    msg.sender = fixture.tx.from;
    msg.recipient = *fixture.tx.to;
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = &view;
    input.vm = &vm;
    input.message = msg;
    input.blockInfo = fixture.block;
    input.revisionConfig.revision = EVMC_PRAGUE;

    auto output = executeMessage(input);
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    bcos::bytes actual(output.result.output_data,
        output.result.output_data + output.result.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output), "BLS output mismatch");
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: Register CTest**

```cmake
add_executable(Eip2537KernelTest eth/Eip2537KernelTest.cpp)
target_include_directories(Eip2537KernelTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(Eip2537KernelTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(Eip2537KernelTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip2537Kernel COMMAND Eip2537KernelTest)
```

- [ ] **Step 3: Matrix** — add `` `Eip2537KernelTest` `` to **kernel row only** (not BCOS feature-gated cell).

- [ ] **Step 4: Run + commit**

```bash
cmake --build build --target Eip2537KernelTest -j
ctest --test-dir build -R Eip2537Kernel -V
rtk git add bcos-evm/test/eth/Eip2537KernelTest.cpp bcos-evm/test/CMakeLists.txt bcos-evm/capability-matrix.md
rtk git commit -m "$(cat <<'EOF'
test(eth): add EIP-2537 kernel contract test for BLS precompile 0x0b

EOF
)"
```

---

### Task 5: BCOS auth orchestrator **hook** test

**Scope:** Proves `enable_auth_check && authChecker` short-circuits **before** kernel — **not** full FISCO `AuthCheck` / TE integration.

**Matrix:** BCOS auth check Test ref: `` `BcosAuthOrchestratorHookTest` (hook-only; not AuthCheck integration) ``

**Files:** `bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp`, `CMakeLists.txt`, `capability-matrix.md`

- [ ] **Step 1: Create test**

```cpp
#define BOOST_TEST_MODULE BcosAuthOrchestratorHookTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(auth_checker_hook_short_circuits_before_executeMessage)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.revisionConfig.enable_auth_check = true;
    input.authChecker = [](evmc_message const&) -> std::optional<EVMCResult> {
        evmc_result fail{};
        fail.status_code = EVMC_REJECTED;
        fail.gas_left = 0;
        return EVMCResult(fail, protocol::TransactionStatus::PermissionDenied);
    };

    auto output = task::syncWait(executeViaHost(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REJECTED);
    BOOST_CHECK(output.stateDiff.accounts.find(target) == output.stateDiff.accounts.end());
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: Register CTest**

```cmake
add_executable(BcosAuthOrchestratorHookTest bcos/BcosAuthOrchestratorHookTest.cpp)
target_include_directories(BcosAuthOrchestratorHookTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(BcosAuthOrchestratorHookTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME BcosAuthOrchestratorHook COMMAND BcosAuthOrchestratorHookTest)
```

- [ ] **Step 3: Run + commit**

```bash
cmake --build build --target BcosAuthOrchestratorHookTest -j
ctest --test-dir build -R BcosAuthOrchestratorHook -V
rtk git add bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp bcos-evm/test/CMakeLists.txt bcos-evm/capability-matrix.md
rtk git commit -m "$(cat <<'EOF'
test(bcos): prove authChecker hook short-circuits executeViaHost before kernel

EOF
)"
```

---

### Task 6: ADR-007 — TE Web3 decoder dependency

**Files:**
- Create: `bcos-evm/docs/adr/007-te-web3-decoder-dependency.md`
- Modify: `bcos-evm/docs/architecture-known-gaps.md`
- Modify: design spec §6.2 + §11 Open Decision (resolve decoder item)

- [ ] **Step 1: Create ADR-007**

```markdown
# ADR-007: TE Web3 Decoder Dependency on bcos-executor

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** Design §2.1, §6.2; Open Decision (Web3 decoder)

## Decision

TE baseline input builders (`EthTxInputBuilder`, `FiscoTxInputBuilder`, `OpStackTxInputBuilder`) **continue** to depend on `bcos-executor` Web3 decode (`Web3AccessListResolver`, typed-tx kind) for the inheritance-contract scope.

Migration to a TE-local decoder is **deferred**; not required for kernel inheritance proofs on `executeViaHost` / `opStackExecuteViaHost`.

## Consequences

- Inheritance matrix tracks tx-input **fields after decode**, not decoder location.
- Future migration requires ADR amendment + matrix note; not a silent refactor.
```

- [ ] **Step 2: Link from known-gaps + resolve design §11 item**

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/docs/adr/007-te-web3-decoder-dependency.md \
  bcos-evm/docs/architecture-known-gaps.md \
  docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md
rtk git commit -m "$(cat <<'EOF'
docs(adr): accept TE Web3 decoder dependency on bcos-executor (ADR-007)

EOF
)"
```

---

### Task 7: Matrix, tracker, design §8 sync (honest §9)

**Do not** mark §9 fully Met. Only update rows with evidence.

| §9 criterion | After this plan |
| --- | --- |
| Kernel vs orchestrator tests separated | **Met** (unchanged) |
| Matrix updated with EIP changes | **Met** (process) |
| BCOS/OP TE baseline kernel inheritance | **Partial** — 7702/7623/fixture smoke added; value transfer / CREATE nonce / full auth still open |
| Chain EIPs extension tests | **Partial** — L1Block done; auth hook-only |
| Tx-input visible path | **Partial** — builders restored; decoder deferred per ADR-007 |

**Tracker closes:** 16, 17, 29, 30, 32, 34 (as fixture pipeline), 35 (partial — value transfer rows stay `—`). **Not** item 6 (Appendix A).

**Matrix Test ref updates:**

| Row | Test ref |
| --- | --- |
| EIP-7623 entry precheck (BCOS) | `Bcos7623PrecheckTest` |
| EIP-2537 kernel | `Eip2537KernelTest` |
| chain precompile routing (OP) | `L1BlockGetterTest` |
| BCOS auth check | `BcosAuthOrchestratorHookTest` (hook-only footnote) |
| builtin precompiles | existing `stPrecompile_*`, `ExecuteViaHostImportedFixtureTest` |

- [ ] **Step 1: Apply doc/matrix/tracker edits**

- [ ] **Step 2: Lint**

```bash
bash bcos-evm/tools/ci/check-capability-matrix.sh
```

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/capability-matrix.md bcos-evm/docs/inheritance-work-tracker.md \
  docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md
rtk git commit -m "$(cat <<'EOF'
docs: sync inheritance tracker and spec §8 with post-grill test scope

EOF
)"
```

---

## Verification gate (Tasks 0–7)

```bash
cmake --build build --target RevisionConfigProfileTest Bcos7623PrecheckTest \
  ExecuteViaHostImportedFixtureTest BcosAuthOrchestratorHookTest Eip2537KernelTest \
  Bcos7702ExecuteViaHostPropagationTest OpStack7702ExecuteViaHostPropagationTest \
  EthTxInputBuilderTest FiscoTxInputBuilderTest OpStackTxInputBuilderTest \
  TxFeaturePrepareTest OpStackTxPropsTest Bcos21000GasDeviationTest -j

ctest --test-dir build -R "RevisionConfigProfile|Bcos7623Precheck|ExecuteViaHostImportedFixture|BcosAuthOrchestratorHook|Eip2537Kernel|Bcos7702|OpStack7702|EthTxInputBuilder|FiscoTxInputBuilder|OpStackTxInputBuilder|TxFeaturePrepare|OpStackTxProps|Bcos21000|L1BlockGetter" --output-on-failure

bash bcos-evm/tools/ci/check-capability-matrix.sh
```

---

## Appendix A — Human-only: capability-gate merge (tracker #6)

Not an agent task. Owner merges PR containing `.github/workflows/capability-gate.yml`, confirms `matrix-lint` green, then sets tracker item **6** to `[x]`.

---

## Out of scope

- BCOS 7702 precheck/intrinsic (ADR-006)
- Web3 decoder code migration (ADR-007 defers)
- Full FISCO `AuthCheck` integration test
- BCOS value transfer / CREATE nonce orchestrator tests
- Renaming `stEIP7702_delegation.json` on disk (optional follow-up)
- Legacy `bcos-executor` path

---

## Self-review (post-grill)

| Grill finding | Fixed |
| --- | --- |
| Isthmus assert vs sparse helper | `assertIsthmusHelperProfile` with all 13 fields |
| 7623 success case vs NotFoundCodeError | Empty calldata case; nonzero calldata only in OOG test |
| Task 3 misnamed 7702 closure | Renamed test; tracker = pipeline infra |
| Auth stub scope | Hook test + matrix footnote |
| §9 overclaim | Partial table in Task 7 |
| Web3 without ADR | ADR-007 |
| Task 8 agent blocker | Appendix A |
| Duplicate plan file | `2026-06-20-inheritance-contract-remaining.md` superseded |
| Task 4 missing CMake | Full blocks in Tasks 2/5 |
| Verification gate incomplete | Includes all restored builder targets |

**Plan complete and saved to `docs/superpowers/plans/2026-06-20-eth-kernel-capability-inheritance-remaining.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks

**2. Inline Execution** — execute in this session with checkpoints

**Which approach?**

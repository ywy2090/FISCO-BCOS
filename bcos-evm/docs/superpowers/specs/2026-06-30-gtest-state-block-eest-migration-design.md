# GTest-Driven State / Block / EEST Test Migration Design

**Date**: 2026-06-30
**Source**: Port evmone's `evmone-statetest` and `evmone-blockchaintest` GTest-driven test modules into bcos-evm, unifying the existing EEST/GST custom-runner infrastructure under a common fine-grained GTest registration pattern.

---

## Motivation

bcos-evm already supports Ethereum State Tests (legacy GST and EEST formats) via custom-runner executables (`EthGSTFull`, `EthExecutionSpecStateTests`, `OpStackEestBlockchainRunner`, etc.). These runners use a flat-iteration pattern: load all fixtures, execute in a loop, and tally pass/fail counts.

evmone's equivalent test binaries (`evmone-statetest`, `evmone-blockchaintest`) use **dynamic GTest registration** (`testing::RegisterTest`). This gives developers:

- `--gtest_filter` with wildcard matching on individual test cases (e.g. `--gtest_filter=stExample/add11*`)
- `--gtest_list_tests` to enumerate all discoverable tests without executing them
- `--gtest_shuffle`, `--gtest_repeat`, `--gtest_break_on_failure` — free from the framework
- Isolated failure reports: one subtest failure does not abort the rest of the file

This design adds the same GTest-registered runners **alongside** (not replacing) the existing manifest-driven runners, and fills the Eth-path EEST blockchain test gap.

---

## Architecture Overview

```
Existing                          New (this design)
────────                           ──────────────────
GeneralStateTestLoader ──┐        EthGSTGranular.cpp      (Chapter 1)
EthMessageAdapter       ──┤  ──→  EthEestStateGranular.cpp (Chapter 3)
EestStateTestLoader     ──┘        EthEestTxGranular.cpp   (Chapter 3)

EthMessageAdapter       ──┐
(per-tx execution)       │  ──→  applyEthBlock()           (Chapter 2)
Block post-processing    │        EthBlockTransitionTest.cpp(Chapter 2)
                         ┘        EthEestBlockGranular.cpp  (Chapter 3)
                                  EthEestBlockchainRunner.cpp(Chapter 3)
```

**Key principle**: zero changes to the execution path. `EthMessageAdapter::execute()` is treated as a sealed primitive. The new runners are thin GTest shells that:
1. Use existing loaders to parse JSON fixtures
2. Call existing adapters for execution
3. Use `EXPECT_*` / `ASSERT_*` for test assertions

The existing manifest-driven runners (`EthGSTFull`, `EthGSTSmoke`, `EthExecutionSpecStateTests`, etc.) continue to operate unchanged.

---

## Chapter 1 — GTest-Granular State Test Runner

### 1.1 Two Registration Granularities

Same pattern as evmone:

| Input Type | Registration | Suite Name | Notes |
|-----------|-------------|-----------|-------|
| Directory path | File-level: one GTest case per JSON file | `<relative-path>` | Each file's `TestBody()` iterates all subtests; failure aborts the file |
| Single file path | Subtest-level: one GTest case per `(fork, dataIndex, gasIndex, valueIndex)` combo | `<file-path>` | Each subtest is an independent GTest case with its own pass/fail |

### 1.2 Core Classes

```cpp
// File-level (directory input)
class EthGstFileTest : public testing::Test {
    std::filesystem::path m_path;          // JSON file path
    std::vector<ForkProfile> m_profiles;   // active forks
    bcos::crypto::Hash* m_hashImpl;
    evmc::VM* m_vm;

    void TestBody() final {
        auto cases = loadGeneralStateTestFile(m_path);
        for (auto& testCase : cases)
            for (auto& profile : m_profiles)
                for (auto& subtest : listSubtests(testCase, profile.upstreamForkName))
                    runAndAssert(testCase, subtest, profile);
    }

    static void register_one(suite, file, profiles, hashImpl, vm);
    // → testing::RegisterTest(suite, file.stem(), ...)
};

// Subtest-level (single-file input)
class EthGstSubtest : public testing::Test {
    StateTestCase m_testCase;
    StateSubtest m_subtest;
    ForkProfile m_profile;
    bcos::crypto::Hash* m_hashImpl;
    evmc::VM* m_vm;

    void TestBody() final {
        runAndAssert(m_testCase, m_subtest, m_profile);
    }

    static void register_one(testCase, subtest, profile, hashImpl, vm, suite, testName);
    // → testing::RegisterTest(suite, testName, ...)
};
```

### 1.3 `runAndAssert` helper

```cpp
void runAndAssert(const StateTestCase& testCase, const StateSubtest& subtest,
                  const ForkProfile& profile, ...) {
    SCOPED_TRACE(...);
    auto expected = selectExpected(testCase, subtest);
    // co_await wrapped via task::syncWait
    auto result = task::syncWait(adapter.execute(testCase, subtest));
    auto report = assertResult(entry, expected, result, gasBefore);
    EXPECT_TRUE(report.passed) << report.message;
}
```

Reuses `StateTestAssert::assertResult()` from the existing `bcos-evm-specs-tests-core` library.

### 1.4 CLI and Discovery

```bash
# Scan a directory — file-level registration
evmone-ethtest /path/to/GeneralStateTests --fork-profiles eth-cancun,eth-prague

# Single file — subtest-level registration
evmone-ethtest /path/to/GeneralStateTests/stExample/add11.json --fork-profiles eth-cancun

# GTest-native filtering
evmone-ethtest /path/to/GeneralStateTests --gtest_filter="*add11*:*eip1559*"
evmone-ethtest /path/to/GeneralStateTests/stExample/add11.json --gtest_filter="*Cancun*1*2*0*"
```

- `--fork-profiles`: comma-separated fork profile IDs (default: `eth-cancun,eth-prague,eth-osaka`)
- `--gtest_filter`: provided natively by GTest — no custom implementation needed

### 1.5 Relationship to Existing Runners

```
Purpose              | Existing Runner              | New GTest Runner
─────────────────────┼──────────────────────────────┼─────────────────────
CI nightly (full)    | EthGSTFull (manifest loop)   | EthGSTGranular (GTest)
CI every commit      | EthGSTSmoke (manifest loop)  | —
Dev / debug single   | — (manual --case-prefix)     | EthGSTGranular + --gtest_filter
```

The new runner is NOT a wholesale replacement — it coexists. The manifest-driven runners remain the primary CI harness (optimized for bulk throughput with expectation manifests); the GTest runner is for interactive developer use with fine-grained selection.

---

## Chapter 2 — Block-Level Transition Test

### 2.1 Scope

Port the block-level validation logic from evmone-blockchaintest, but **only the parts relevant to post-Merge Ethereum**:

**Included:**
- Single-block, multi-transaction cumulative `stateRoot` validation
- Block header invariant checks (gas limit elasticity, base fee calculation, excess blob gas)
- Transaction MPT root / Receipt MPT root validation
- `gasUsed` / `logsBloom` matching

**Excluded (PoW-era, irrelevant):**
- Difficulty / ethash verification
- Ommer (uncle) block validation
- Multi-block canonical chain selection (deferred; the initial version validates a single block at a time)

### 2.2 New Block-Apply Function

The Eth path currently has no block-level `apply_block()` aggregator. Add a lightweight one:

```cpp
// test/helpers/BlockTransition.h

struct BlockApplyResult {
    TestStateView postState;
    std::vector<TransactionReceipt> receipts;
    int64_t gasUsed = 0;
    BloomFilter bloom;
};

BlockApplyResult applyEthBlock(
    TestStateView& preState,
    std::span<const state::Transaction> transactions,
    const state::BlockInfo& blockInfo,
    const ForkProfile& profile,
    evmc::VM& vm,
    bcos::crypto::Hash& hashImpl);
```

Internally:
1. Calls `EthMessageAdapter::execute()` for each transaction in order
2. Accumulates `StateDiff` across transactions
3. Applies block-level post-processing: coinbase reward (set to 0 for tests), withdrawals
4. Computes `StateDiff` → `TestStateView` → MPT root
5. Returns aggregated `receipts`, `gasUsed`, `bloom`

### 2.3 Fixture Format

Use JSON fixtures compatible with a stripped-down evmone BlockchainTest schema. Example:

```json
{
  "name": "single_block_two_txs",
  "network": "Cancun",
  "pre": { /* accounts */ },
  "blocks": [
    {
      "blockHeader": { "gasLimit": "0x...", "baseFee": "0x...", ... },
      "transactions": [ /* tx array */ ],
      "expectedHeader": { "stateRoot": "0x...", "txRoot": "0x...", "receiptsRoot": "0x..." }
    }
  ],
  "expectedPostState": { /* account map */ }
}
```

### 2.4 GTest Registration

Same dual-granularity pattern:

- **Directory input**: file-level GTest cases
- **Single file input**: per-test-case GTest cases

---

## Chapter 3 — EEST Full Coverage

### 3.1 Gap Analysis

| Test Type              | Eth Path                                  | OPStack Path                       |
|------------------------|-------------------------------------------|------------------------------------|
| EEST state_tests       | ✅ `EthExecutionSpecStateTests` (manifest) | ✅ `OpStackEestStateRunner` (custom) |
| EEST blockchain_tests  | ❌ Missing                                | ✅ `OpStackEestBlockchainRunner`   |
| EEST transaction_tests | ✅ `EthExecutionSpecTransactionTests`     | ✅ `OpStackEestTxRunner`           |
| GTest granularity      | ❌ All use custom loops                   | ❌ All use custom loops            |

### 3.2 New Components

| New File | Purpose | Reuses |
|----------|--------|--------|
| `runners/eth/EthEestStateGranular.cpp` | GTest-wrapped EEST state tests | `EestStateTestLoader` (existing), `EthMessageAdapter` (existing), Chapter 1 GTest pattern |
| `runners/eth/EthEestTxGranular.cpp` | GTest-wrapped EEST tx tests | `EestTransactionTestLoader` (existing), Chapter 1 GTest pattern |
| `runners/eth/EthEestBlockchainRunner.cpp` | Eth path EEST blockchain execution | Chapter 2 `applyEthBlock()` |
| `runners/eth/EthEestBlockGranular.cpp` | GTest-wrapped EEST blockchain tests | `EestStateTestLoader` (existing), Chapter 2 `applyEthBlock()`, Chapter 1 GTest pattern |

### 3.3 Dependency Graph

```
Chapter 1 (GTest registration pattern)
    │
    ├── EthGSTGranular       (legacy GST, GTest)
    ├── EthEestStateGranular (EEST state, GTest)    ← Chapter 3
    └── EthEestTxGranular    (EEST tx, GTest)       ← Chapter 3

Chapter 2 (Block transition logic)
    │
    ├── EthBlockTransitionTest   (legacy block fixture, GTest)
    ├── EthEestBlockchainRunner  (EEST block, custom loop)  ← Chapter 3
    └── EthEestBlockGranular     (EEST block, GTest)        ← Chapter 3
```

### 3.4 EEST Blockchain Runner Details

The runner parses EEST `blockchain_tests/*.json` fixtures:

```
For each fixture:
  1. Validate genesis block header
  2. For each block:
     a. Validate block header vs parent
     b. For each rejectable transaction: verify rejection reason
     c. For accepted transactions: apply via applyEthBlock()
     d. Verify expectedBlockHeader (stateRoot, txRoot, receiptsRoot, gasUsed, bloom)
  3. Verify expectedLastBlockHash and expectedPostState
```

Block validation is a subset of `validate_block()` from evmone — adapted to post-Merge reality (no PoW, no ommers).

### 3.5 CI Labels

All new test targets receive appropriate CTest labels for CI integration:

- `specs-tests` — all reference tests (common prefix)
- `specs-tests-smoke` / `specs-tests-full` — granularity tier
- `eth-kernel` / `opstack` — execution path
- `eest` / `gst` — fixture format origin

---

## File Layout

```
bcos-evm/test/eth-eest-test/
├── runners/
│   └── eth/
│       ├── EthGSTFull.cpp                    ← Exists, unchanged
│       ├── EthGSTSmoke.cpp                   ← Exists, unchanged
│       ├── EthGSTGranular.cpp                ← NEW: Chapter 1 — GTest GST
│       ├── EthExecutionSpecStateTests.cpp     ← Exists, unchanged
│       ├── EthEestStateGranular.cpp          ← NEW: Chapter 3 — GTest EEST state
│       ├── EthEestTxGranular.cpp             ← NEW: Chapter 3 — GTest EEST tx
│       ├── EthBlockTransitionTest.cpp         ← NEW: Chapter 2 — block GTest
│       ├── EthEestBlockchainRunner.cpp        ← NEW: Chapter 3 — EEST block runner
│       └── EthEestBlockGranular.cpp           ← NEW: Chapter 3 — GTest EEST block
├── manifests/                                ← Exists; add block manifests as needed
├── src/                                      ← Exists, unchanged
└── CMakeLists.txt                            ← Extend: 6 new targets

bcos-evm/test/helpers/
└── BlockTransition.h                         ← NEW: Chapter 2 — applyEthBlock()
```

---

## Implementation Order

1. **Chapter 1** — `EthGSTGranular.cpp` (GTest-granular GST runner, least new code, highest developer-visible payoff)
2. **Chapter 2** — `BlockTransition.h` + `EthBlockTransitionTest.cpp` (block-level aggregation capability)
3. **Chapter 3** — `EthEestStateGranular.cpp`, `EthEestTxGranular.cpp`, `EthEestBlockchainRunner.cpp`, `EthEestBlockGranular.cpp` (EEST gap fill + GTest wrapping)

Each chapter builds on the previous and can be merged independently.

---

## What Is NOT Being Ported

- **evmone's `test/state/` journal-based State class** — bcos-evm has `bcos-evm/eth/state/` with its own account / storage model, already integrated with the execution pipeline
- **evmone's `validate_block()` in full** — PoW-era checks (difficulty, ethash, ommers) are irrelevant to bcos-evm; only post-Merge header invariants are checked
- **evmone's `bytecode` DSL** (`add(7, 13) + ret_top()`) — bcos-evm tests use JSON fixtures, not in-code bytecode assembly
- **evmone's `evmc::MockedHost` unit test pattern** — bcos-evm has its own mock adapters (`test/bcos/adapters/`)
- **Existing manifest-driven runners** — unchanged; the new GTest runners coexist alongside them

---

## Design Review Checklist

- [x] No changes to execution path (`EthMessageAdapter`, `applyEthMessage`, `applyOpStackMessage`)
- [x] Existing manifest runners continue to work
- [x] GTest registration pattern matches evmone (file-level for dir, subtest-level for single file)
- [x] Block test excludes PoW-era logic
- [x] EEST gap only filled for Eth path (OPStack already has blockchain runner)
- [x] CI label taxonomy consistent with existing conventions

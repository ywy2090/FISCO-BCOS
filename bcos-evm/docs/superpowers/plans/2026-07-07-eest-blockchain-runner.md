# EEST Blockchain Test Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `eth-eest-test` a real Ethereum blockchain-test runner (block header validation, invalid-block/`expectException` matching, MPT root checks, blob-gas + withdrawal semantics) reaching Cancun ≥90% (milestone M1) with crash-free nightly full sweep.

**Architecture:** Layered — a test-only `BlockchainTestTypes` data model, a `BlockchainTestLoader` (boost::property_tree JSON → structs), a pure `BlockValidation` engine, and two thin runners (`EthEestBlockchainRunner` CLI + `EthEestBlockGranular` GTest) that share the loader/validation core and drive the existing `applyEthBlock`. evmone is a reference spec only — never linked.

**Tech Stack:** C++20, boost::property_tree (JSON), Boost.Test (unit tests), GTest (granular runner), evmone VM (`evmone::evmone` only), CMake + CTest.

## Global Constraints

- **evmone reuse:** reference spec only. NEVER `target_link_libraries(... evmone::testutils)` or `evmone::state`. Only the existing `evmone::evmone` (VM) is allowed.
- **JSON library:** use `boost::property_tree` exclusively in the loader. Do NOT introduce `nlohmann_json`.
- **Production code frozen:** do NOT modify `eth/state/BlockInfo.hpp` (9 fields), `eth/state/Transaction.hpp`, `eth/state/StateDiff.hpp`, or any OPStack path.
- **Reuse, don't recreate:** extend `src/GstStateHash.cpp` (already has RLP + MPT + `computeStateRoot`) for tx/receipts/withdrawal/requests roots. Do NOT create a new `MptHash.h`.
- **Hash type:** all new `compute*Root` functions return `evmc_bytes32` (matching existing `computeStateRoot`/`computeLogsHash`). `BlockchainTestTypes` header hash fields are also `evmc_bytes32` to avoid `h256↔evmc_bytes32` conversion churn (deviation from spec §4.1 which used `h256`; recorded here as an approved simplification).
- **Namespace:** all new symbols live in `namespace bcos::evm::reference_tests`.
- **Unit-test framework:** new loader/validation/MPT unit tests use Boost.Test via the existing `add_reference_test(<Target> test/<File>.cpp)` helper (matches `GstStateHashTest`).
- **Build dir:** `build-bcos-evm-check`. Build a target: `cmake --build build-bcos-evm-check --target <T> -j`. Run a test: `ctest --test-dir build-bcos-evm-check -R <T> --output-on-failure`.
- **Fixtures root:** `build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src/blockchain_tests`.
- **Commits:** use `rtk git ...`. One commit per task step group as shown.

## JSON Schema Reference (EEST blockchain fixtures — verified 2026-07-07)

Top-level test object: `network`, `genesisBlockHeader`, `pre`, `postState`, `lastblockhash`, `config`, `genesisRLP`, `blocks[]`.

Block header keys (in `genesisBlockHeader`, `blocks[].blockHeader`, and `blocks[].rlp_decoded.blockHeader`):
`parentHash, uncleHash, coinbase, stateRoot, transactionsTrie, receiptTrie, bloom, difficulty, number, gasLimit, gasUsed, timestamp, extraData, mixHash, nonce, baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas, parentBeaconBlockRoot, hash`.

Block keys: valid blocks have `blockHeader` + `transactions` + `withdrawals` + `rlp` + `blocknumber`; invalid blocks have `rlp` + `expectException` + `rlp_decoded` (which mirrors `blockHeader`/`transactions`/`withdrawals`). `expectException` values look like `"BlockException.INCORRECT_BLOB_GAS_USED"` or `"TransactionException.<X>"`.

Withdrawal object: `{ index, validatorIndex, address, amount }` (all hex; `amount` in Gwei).

Cancun corpus size (baseline sizing): 105 files, 8389 tests (6100 valid-only, 2289 contain an invalid block).

---
## File Structure

**New files:**
- `include/bcos-evm/eth-eest-test/BlockchainTestTypes.h` — `BlobParams`, `BlobSchedule`, `Withdrawal`, `TestBlockHeader`, `TestBlock`, `BlockchainTest`, constants (`GAS_PER_BLOB`, `EMPTY_MPT_HASH` fwd).
- `include/bcos-evm/eth-eest-test/BlockValidationErrors.h` — `BlockError::*` string constants.
- `include/bcos-evm/eth-eest-test/BlockchainTestLoader.h` — loader function declarations.
- `src/BlockchainTestLoader.cpp` — JSON → structs (added to `bcos-evm-specs-tests-core`).
- `helpers/BlockValidation.h` — `validateBlock()` + blob/base-fee helpers (header-only, in `test/helpers/`).
- `test/BlockchainTestLoaderTest.cpp` — Boost.Test unit tests.
- `test/BlockValidationTest.cpp` — Boost.Test unit tests.
- `manifests/eth/eth-eest-blockchain-smoke.json` — curated PR-gate manifest (Phase 4).

**Modified files:**
- `src/GstStateHash.cpp` + `include/bcos-evm/eth-eest-test/GstStateHash.h` — add `computeTxRoot`/`computeReceiptsRoot`/`computeWithdrawalRoot`/`computeRequestsHash` + `EMPTY_MPT_HASH`.
- `test/helpers/BlockTransition.h` — extend `TransactionReceipt` + `BlockApplyResult`; add `withdrawals` param to `applyEthBlock`.
- `src/ForkProfileRegistry.cpp` + header — add `resolveRevision(network, timestamp)`.
- `runners/eth/EthEestBlockchainRunner.cpp` — refactor to loader+validation.
- `runners/eth/EthEestBlockGranular.cpp` — refactor to per-file execution GTest.
- `test/eth-eest-test/CMakeLists.txt` — wire new source/tests/CTest.
- `.github/workflows/specs-tests-nightly.yml` — add blockchain full jobs.
- `test/eth-eest-test/eest-integration-matrix.md` — update §6 baseline.

---

# Phase 0 — Scaffolding (types, loader, revision resolution)

### Task 0.1: Test types + error-code constants

**Files:**
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h`
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockValidationErrors.h`

**Interfaces:**
- Consumes: `state::BlockInfo` (`eth/state/BlockInfo.hpp`), `state::Transaction`, `TestStateView`, `GstTransactionTemplate` (`GeneralStateTestLoader.h`).
- Produces: `BlobParams`, `BlobSchedule`, `Withdrawal`, `TestBlockHeader`, `TestBlock`, `BlockchainTest`, `GAS_PER_BLOB`; `BlockError::*` constants.

- [ ] **Step 1: Write `BlockValidationErrors.h`**

```cpp
#pragma once

namespace bcos::evm::reference_tests::BlockError
{
inline constexpr auto INVALID_BLOCK_PARENT = "INVALID_BLOCK_PARENT";
inline constexpr auto INVALID_BLOCK_NUMBER = "INVALID_BLOCK_NUMBER";
inline constexpr auto INCORRECT_BLOCK_FORMAT = "INCORRECT_BLOCK_FORMAT";
inline constexpr auto INVALID_GASLIMIT = "INVALID_GASLIMIT";
inline constexpr auto INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT =
    "INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT";
inline constexpr auto INVALID_BASEFEE_PER_GAS = "INVALID_BASEFEE_PER_GAS";
inline constexpr auto INCORRECT_EXCESS_BLOB_GAS = "INCORRECT_EXCESS_BLOB_GAS";
inline constexpr auto INCORRECT_BLOB_GAS_USED = "INCORRECT_BLOB_GAS_USED";
inline constexpr auto RLP_BLOCK_LIMIT_EXCEEDED = "RLP_BLOCK_LIMIT_EXCEEDED";
}  // namespace bcos::evm::reference_tests::BlockError
```

- [ ] **Step 2: Write `BlockchainTestTypes.h`**

```cpp
#pragma once

#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bcos::evm::reference_tests
{

/// GAS_PER_BLOB (EIP-4844): constant across forks.
inline constexpr uint64_t GAS_PER_BLOB = 1u << 17;  // 131072

/// Fork-dependent blob parameters (evmone: state::BlobParams).
struct BlobParams
{
    uint16_t target = 3;
    uint16_t max = 6;
    uint32_t baseFeeUpdateFraction = 3338477;
};

/// Keyed by EEST network name ("Cancun", "Prague", ...); evmone: BlobSchedule map.
using BlobSchedule = std::unordered_map<std::string, BlobParams>;

/// EIP-4895 withdrawal. `amount` is in Gwei.
struct Withdrawal
{
    uint64_t index = 0;
    uint64_t validatorIndex = 0;
    evmc_address address{};
    uint64_t amount = 0;
};

/// Block header as it appears in EEST fixtures (evmone: BlockHeader).
struct TestBlockHeader
{
    evmc_bytes32 parentHash{};
    evmc_address coinbase{};
    evmc_bytes32 stateRoot{};
    evmc_bytes32 receiptsRoot{};    // JSON: receiptTrie
    bcos::bytes logsBloom;          // 256 bytes; JSON: bloom
    int64_t difficulty = 0;
    evmc_bytes32 prevRandao{};      // JSON: mixHash
    int64_t blockNumber = 0;        // JSON: number
    int64_t gasLimit = 0;
    int64_t gasUsed = 0;
    int64_t timestamp = 0;
    bcos::bytes extraData;
    uint64_t baseFeePerGas = 0;
    evmc_bytes32 hash{};
    evmc_bytes32 transactionsRoot{};  // JSON: transactionsTrie

    // Shanghai+
    evmc_bytes32 withdrawalsRoot{};

    // Cancun+
    evmc_bytes32 parentBeaconBlockRoot{};
    std::optional<uint64_t> blobGasUsed;
    std::optional<uint64_t> excessBlobGas;

    // Prague+
    evmc_bytes32 requestsHash{};
};

/// One block within a blockchain test (evmone: TestBlock).
struct TestBlock
{
    state::BlockInfo blockInfo;                  // execution context (parentHash filled in)
    std::vector<GstTransactionTemplate> transactions;
    std::vector<Withdrawal> withdrawals;         // Shanghai+
    std::optional<uint64_t> inputBlobGasUsed;    // Cancun+
    std::optional<uint64_t> inputExcessBlobGas;  // Cancun+
    size_t rlpSize = 0;                          // Osaka+ (EIP-7934)
    bool withdrawalsParseSuccess = true;
    std::string expectException;                 // empty => valid block
    TestBlockHeader expectedBlockHeader;
};

/// A full blockchain test (evmone: BlockchainTest).
struct BlockchainTest
{
    std::string name;
    std::string network;                 // "Cancun", ...
    TestStateView preState;
    TestBlockHeader genesisBlockHeader;
    std::vector<TestBlock> testBlocks;
    BlobSchedule blobSchedule;
    evmc_bytes32 lastBlockHash{};
    /// postState account map (empty => compare via postStateHash instead).
    std::vector<std::pair<evmc_address, state::Account>> postState;
    std::optional<evmc_bytes32> postStateHash;
};

}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 3: Verify it compiles via a throwaway translation unit**

Run: `cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor && echo '#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include "bcos-evm/eth-eest-test/BlockValidationErrors.h"
int main(){ return (int)bcos::evm::reference_tests::GAS_PER_BLOB - 131072; }' > /tmp/bctypes_probe.cpp && rtk cargo --version >/dev/null 2>&1; echo "compile check happens in Task 0.4 build"`

Expected: file written; real compile is exercised when the loader (Task 0.3) is added to the core lib and built in Task 0.4. (No standalone compile here to avoid bespoke include-path juggling.)

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockValidationErrors.h && rtk git commit -m "feat(eest): blockchain test types and validation error codes"
```

---

### Task 0.2: `resolveRevision(network, timestamp)`

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/ForkProfileRegistry.h`
- Modify: `bcos-evm/test/eth-eest-test/src/ForkProfileRegistry.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/ForkProfileRegistryTest.cpp` (append case)

**Interfaces:**
- Consumes: `ForkProfileRegistry::findByUpstreamFork` (existing), `ForkProfile::revision.revision` (`evmc_revision`).
- Produces: `std::optional<evmc_revision> resolveRevision(std::string_view network, int64_t timestamp)`.

- [ ] **Step 1: Write the failing test** (append to `test/ForkProfileRegistryTest.cpp`)

```cpp
BOOST_AUTO_TEST_CASE(resolve_revision_maps_network_to_evmc_revision)
{
    auto const& reg = ForkProfileRegistry::instance();
    auto const cancun = reg.resolveRevision("Cancun", /*timestamp*/ 0);
    BOOST_REQUIRE(cancun.has_value());
    BOOST_CHECK_EQUAL(static_cast<int>(*cancun), static_cast<int>(EVMC_CANCUN));

    auto const unknown = reg.resolveRevision("NoSuchFork", 0);
    BOOST_CHECK(!unknown.has_value());
}
```

- [ ] **Step 2: Run it to verify failure**

Run: `cmake --build build-bcos-evm-check --target ForkProfileRegistryTest -j && ctest --test-dir build-bcos-evm-check -R ForkProfileRegistryTest --output-on-failure`
Expected: FAIL — `resolveRevision` is not a member.

- [ ] **Step 3: Declare in `ForkProfileRegistry.h`** (inside `class ForkProfileRegistry`, after `findByUpstreamFork`)

```cpp
    /// Map an EEST network name (+timestamp, reserved for BPO transition forks)
    /// to an evmc_revision. Returns nullopt if the network is unknown.
    std::optional<evmc_revision> resolveRevision(
        std::string_view network, int64_t timestamp) const;
```

Add `#include <evmc/evmc.h>` at the top if not already present.

- [ ] **Step 4: Implement in `ForkProfileRegistry.cpp`**

```cpp
std::optional<evmc_revision> ForkProfileRegistry::resolveRevision(
    std::string_view network, int64_t /*timestamp*/) const
{
    // timestamp is reserved for future BPO/transition networks (out of M1-M4 scope);
    // single-network fixtures do not switch revision mid-test.
    if (auto profile = findByUpstreamFork(network))
        return profile->revision.revision;
    return std::nullopt;
}
```

- [ ] **Step 5: Run test to verify pass**

Run: `cmake --build build-bcos-evm-check --target ForkProfileRegistryTest -j && ctest --test-dir build-bcos-evm-check -R ForkProfileRegistryTest --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/ForkProfileRegistry.h bcos-evm/test/eth-eest-test/src/ForkProfileRegistry.cpp bcos-evm/test/eth-eest-test/test/ForkProfileRegistryTest.cpp && rtk git commit -m "feat(eest): resolveRevision(network,timestamp) on ForkProfileRegistry"
```

---

### Task 0.3: `BlockchainTestLoader` (JSON → structs)

**Files:**
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestLoader.h`
- Create: `bcos-evm/test/eth-eest-test/src/BlockchainTestLoader.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/BlockchainTestLoaderTest.cpp`

**Interfaces:**
- Consumes: types from Task 0.1; `parseTransactionTemplate` (`GeneralStateTestLoader.h`); `parseHex`/hex helpers (reimplement locally, matching `EthEestBlockchainRunner.cpp`).
- Produces:
  - `std::vector<BlockchainTest> loadBlockchainTests(boost::property_tree::ptree const& root);`
  - `TestBlockHeader parseBlockHeader(boost::property_tree::ptree const& j);`
  - `TestBlock parseTestBlock(boost::property_tree::ptree const& j, std::string_view network);`

- [ ] **Step 1: Write the failing test** using a real Cancun fixture

```cpp
#define BOOST_TEST_MODULE BlockchainTestLoaderTest
#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/included/unit_test.hpp>
#include <filesystem>

namespace bcos::evm::reference_tests
{
namespace
{
std::filesystem::path fixture()
{
    // SPECS_TESTS_EEST_ROOT points at .../evm_ref_eest_root; fixtures under fixtures/.
    return std::filesystem::path(SPECS_TESTS_EEST_ROOT) /
           "fixtures/blockchain_tests/cancun/eip5656_mcopy/test_mcopy_on_empty_memory.json";
}
}  // namespace

BOOST_AUTO_TEST_CASE(loads_cancun_valid_block_fixture)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_json(fixture().string(), root);

    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& t = tests.front();
    BOOST_CHECK_EQUAL(t.network, "Cancun");
    BOOST_CHECK_EQUAL(t.genesisBlockHeader.blockNumber, 0);
    BOOST_REQUIRE(!t.testBlocks.empty());
    BOOST_CHECK(t.testBlocks.front().expectException.empty());
    // lastblockhash present
    BOOST_CHECK(std::any_of(std::begin(t.lastBlockHash.bytes), std::end(t.lastBlockHash.bytes),
        [](uint8_t b) { return b != 0; }));
}

BOOST_AUTO_TEST_CASE(loads_invalid_block_via_rlp_decoded)
{
    boost::property_tree::ptree root;
    auto p = std::filesystem::path(SPECS_TESTS_EEST_ROOT) /
             "fixtures/blockchain_tests/cancun/eip4844_blobs/"
             "test_invalid_blob_gas_used_in_header.json";
    boost::property_tree::read_json(p.string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    bool sawInvalid = false;
    for (auto const& t : tests)
        for (auto const& b : t.testBlocks)
            if (!b.expectException.empty())
                sawInvalid = true;
    BOOST_CHECK(sawInvalid);
}
}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 2: Write `BlockchainTestLoader.h`**

```cpp
#pragma once

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include <boost/property_tree/ptree.hpp>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
/// Parse every test object in an EEST blockchain fixture ptree.
/// Skips engine-only objects (no `pre` + `genesisBlockHeader`).
std::vector<BlockchainTest> loadBlockchainTests(boost::property_tree::ptree const& root);

TestBlockHeader parseBlockHeader(boost::property_tree::ptree const& j);
TestBlock parseTestBlock(boost::property_tree::ptree const& j, std::string_view network);
}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 3: Write `BlockchainTestLoader.cpp`**

```cpp
#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <cstring>
#include <string>

namespace pt = boost::property_tree;

namespace bcos::evm::reference_tests
{
namespace
{
bcos::bytes hexToBytes(std::string_view s)
{
    if (s.starts_with("0x") || s.starts_with("0X"))
        s.remove_prefix(2);
    bcos::bytes out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2)
    {
        char buf[3] = {s[i], s[i + 1], '\0'};
        out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

evmc_address toAddr(std::string_view hex)
{
    evmc_address a{};
    auto b = hexToBytes(hex);
    if (b.size() >= sizeof(a.bytes))
        std::memcpy(a.bytes, b.data(), sizeof(a.bytes));
    return a;
}

evmc_bytes32 toBytes32(std::string_view hex)
{
    evmc_bytes32 o{};
    auto b = hexToBytes(hex);
    if (b.size() > sizeof(o.bytes))
        b.erase(b.begin(), b.end() - sizeof(o.bytes));
    if (!b.empty())
        std::memcpy(o.bytes + sizeof(o.bytes) - b.size(), b.data(), b.size());
    return o;
}

uint64_t toU64(std::string_view s)
{
    if (s.starts_with("0x") || s.starts_with("0X"))
        return std::strtoull(s.data() + 2, nullptr, 16);
    return std::strtoull(s.data(), nullptr, 10);
}

std::optional<std::string> opt(pt::ptree const& j, char const* k)
{
    return j.get_optional<std::string>(k);
}
}  // namespace

TestBlockHeader parseBlockHeader(pt::ptree const& j)
{
    TestBlockHeader h;
    if (auto s = opt(j, "parentHash")) h.parentHash = toBytes32(*s);
    if (auto s = opt(j, "coinbase")) h.coinbase = toAddr(*s);
    if (auto s = opt(j, "stateRoot")) h.stateRoot = toBytes32(*s);
    if (auto s = opt(j, "receiptTrie")) h.receiptsRoot = toBytes32(*s);
    if (auto s = opt(j, "bloom")) h.logsBloom = hexToBytes(*s);
    if (auto s = opt(j, "difficulty")) h.difficulty = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "mixHash")) h.prevRandao = toBytes32(*s);
    if (auto s = opt(j, "number")) h.blockNumber = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "gasLimit")) h.gasLimit = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "gasUsed")) h.gasUsed = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "timestamp")) h.timestamp = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "extraData")) h.extraData = hexToBytes(*s);
    if (auto s = opt(j, "baseFeePerGas")) h.baseFeePerGas = toU64(*s);
    if (auto s = opt(j, "hash")) h.hash = toBytes32(*s);
    if (auto s = opt(j, "transactionsTrie")) h.transactionsRoot = toBytes32(*s);
    if (auto s = opt(j, "withdrawalsRoot")) h.withdrawalsRoot = toBytes32(*s);
    if (auto s = opt(j, "parentBeaconBlockRoot")) h.parentBeaconBlockRoot = toBytes32(*s);
    if (auto s = opt(j, "blobGasUsed")) h.blobGasUsed = toU64(*s);
    if (auto s = opt(j, "excessBlobGas")) h.excessBlobGas = toU64(*s);
    if (auto s = opt(j, "requestsHash")) h.requestsHash = toBytes32(*s);
    return h;
}

TestBlock parseTestBlock(pt::ptree const& j, std::string_view /*network*/)
{
    TestBlock tb;
    tb.expectException = j.get<std::string>("expectException", "");

    // Structured data: valid blocks use blockHeader/transactions; invalid use rlp_decoded.
    pt::ptree const* src = &j;
    if (auto rd = j.get_child_optional("rlp_decoded"))
        src = &rd.get();

    if (auto hdr = src->get_child_optional("blockHeader"))
        tb.expectedBlockHeader = parseBlockHeader(*hdr);

    // BlockInfo execution context.
    auto& bi = tb.blockInfo;
    bi.number = tb.expectedBlockHeader.blockNumber;
    bi.timestamp = tb.expectedBlockHeader.timestamp;
    bi.gasLimit = tb.expectedBlockHeader.gasLimit;
    bi.coinbase = tb.expectedBlockHeader.coinbase;
    bi.prevRandao = tb.expectedBlockHeader.prevRandao;
    bi.parentHash = tb.expectedBlockHeader.parentHash;
    bi.baseFee = tb.expectedBlockHeader.baseFeePerGas;

    tb.inputBlobGasUsed = tb.expectedBlockHeader.blobGasUsed;
    tb.inputExcessBlobGas = tb.expectedBlockHeader.excessBlobGas;

    if (auto txs = src->get_child_optional("transactions"))
        for (auto const& [_, txTree] : *txs)
            tb.transactions.push_back(parseTransactionTemplate(txTree));

    if (auto ws = src->get_child_optional("withdrawals"))
    {
        for (auto const& [_, w] : *ws)
        {
            Withdrawal wd;
            if (auto s = opt(w, "index")) wd.index = toU64(*s);
            if (auto s = opt(w, "validatorIndex")) wd.validatorIndex = toU64(*s);
            if (auto s = opt(w, "address")) wd.address = toAddr(*s);
            if (auto s = opt(w, "amount")) wd.amount = toU64(*s);
            tb.withdrawals.push_back(wd);
        }
    }

    if (auto rlp = opt(j, "rlp"))
        tb.rlpSize = hexToBytes(*rlp).size();

    return tb;
}

std::vector<BlockchainTest> loadBlockchainTests(pt::ptree const& root)
{
    std::vector<BlockchainTest> out;
    for (auto const& [name, t] : root)
    {
        if (!t.count("pre") || !t.count("genesisBlockHeader"))
            continue;  // engine-only format: skipped (Phase 5)

        BlockchainTest bt;
        bt.name = name;
        bt.network = t.get<std::string>("network", "");
        bt.genesisBlockHeader = parseBlockHeader(t.get_child("genesisBlockHeader"));

        for (auto const& [addrStr, accTree] : t.get_child("pre"))
        {
            state::Account acc{};
            if (auto s = opt(accTree, "nonce")) acc.nonce = toU64(*s);
            if (auto s = opt(accTree, "balance")) acc.balance = bcos::fromBigQuantity(*s);
            if (auto s = opt(accTree, "code")) acc.code = hexToBytes(*s);
            if (auto st = accTree.get_child_optional("storage"))
                for (auto const& [k, v] : *st)
                    acc.storage[toBytes32(k)] = toBytes32(v.get_value<std::string>());
            bt.preState.insertAccount(toAddr(addrStr), std::move(acc));
        }

        if (auto blocks = t.get_child_optional("blocks"))
            for (auto const& [_, b] : *blocks)
                bt.testBlocks.push_back(parseTestBlock(b, bt.network));

        if (auto s = t.get_optional<std::string>("lastblockhash"))
            bt.lastBlockHash = toBytes32(*s);

        if (auto ps = t.get_child_optional("postState"))
        {
            for (auto const& [addrStr, accTree] : *ps)
            {
                state::Account acc{};
                if (auto s = opt(accTree, "nonce")) acc.nonce = toU64(*s);
                if (auto s = opt(accTree, "balance")) acc.balance = bcos::fromBigQuantity(*s);
                if (auto s = opt(accTree, "code")) acc.code = hexToBytes(*s);
                if (auto st = accTree.get_child_optional("storage"))
                    for (auto const& [k, v] : *st)
                        acc.storage[toBytes32(k)] = toBytes32(v.get_value<std::string>());
                bt.postState.emplace_back(toAddr(addrStr), std::move(acc));
            }
        }
        else if (auto s = t.get_optional<std::string>("postStateHash"))
            bt.postStateHash = toBytes32(*s);

        out.push_back(std::move(bt));
    }
    return out;
}
}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 4: Wire into CMake (core lib + test)** — see Task 0.4; do it now so the test builds.

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockchainTestLoaderTest -j && ctest --test-dir build-bcos-evm-check -R BlockchainTestLoaderTest --output-on-failure`
Expected: PASS (both cases).

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestLoader.h bcos-evm/test/eth-eest-test/src/BlockchainTestLoader.cpp bcos-evm/test/eth-eest-test/test/BlockchainTestLoaderTest.cpp && rtk git commit -m "feat(eest): blockchain test loader (json -> structs)"
```

---

### Task 0.4: CMake wiring for loader + loader test

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/CMakeLists.txt`

**Interfaces:**
- Consumes: `bcos-evm-specs-tests-core` STATIC lib source list; `add_reference_test` helper.
- Produces: `BlockchainTestLoader.cpp` compiled into core; `BlockchainTestLoaderTest` CTest target.

- [ ] **Step 1: Add loader source to the core library** (in the `add_library(bcos-evm-specs-tests-core STATIC ...)` list, after `src/EstStateFullManifest.cpp`)

```cmake
    src/EestStateFullManifest.cpp
    src/Eip7702StrictTxValidator.cpp
    src/BlockchainTestLoader.cpp
)
```

- [ ] **Step 2: Register the loader unit test** (after the existing `add_reference_test(...)` block, ~line 94). The `add_reference_test` helper already defines `SPECS_TESTS_MANIFEST_DIR`; the loader test needs `SPECS_TESTS_EEST_ROOT`, which core already exposes via `target_compile_definitions(... PUBLIC SPECS_TESTS_EEST_ROOT=...)`, so no extra define is required.

```cmake
add_reference_test(BlockchainTestLoaderTest test/BlockchainTestLoaderTest.cpp)
```

- [ ] **Step 3: Configure + build core to verify types/loader compile**

Run: `cmake --build build-bcos-evm-check --target bcos-evm-specs-tests-core -j`
Expected: builds clean (this exercises `BlockchainTestTypes.h` include correctness from Task 0.1).

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/CMakeLists.txt && rtk git commit -m "build(eest): compile blockchain loader into core + register loader test"
```

---

# Phase 1 — Validation engine (P0 header rules + blob math + result extension)

### Task 1.1: Base-fee helper + header rules 1–10

**Files:**
- Create: `bcos-evm/test/helpers/BlockValidation.h`  (same dir as existing `BlockTransition.h`, reached via the `..` include dir)
- Test: `bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp`

**Interfaces:**
- Consumes: `TestBlock`, `TestBlockHeader`, `BlobSchedule`, `BlockError::*`, `evmc_revision`.
- Produces:
  - `uint64_t calcBaseFee(uint64_t parentGasLimit, uint64_t parentGasUsed, uint64_t parentBaseFee);`
  - `std::optional<std::string> validateBlock(evmc_revision rev, BlobSchedule const& schedule, TestBlock const& tb, TestBlockHeader const* parent);`
  - (blob helpers added in Task 1.2, same header)

- [ ] **Step 1: Write the failing test** (base fee + a couple of header rules)

```cpp
#define BOOST_TEST_MODULE BlockValidationTest
#include "helpers/BlockValidation.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{
BOOST_AUTO_TEST_CASE(base_fee_constant_when_gas_used_equals_target)
{
    // parentGasLimit=20000000 -> target=10000000; used==target -> unchanged
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 10000000, 1000000000), 1000000000ull);
}

BOOST_AUTO_TEST_CASE(base_fee_rises_when_over_target)
{
    // used=15,000,000 target=10,000,000 base=1e9 -> +1e9*5e6/1e7/8 = +62,500,000
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 15000000, 1000000000), 1062500000ull);
}

BOOST_AUTO_TEST_CASE(rejects_missing_parent)
{
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 1;
    auto err = validateBlock(EVMC_LONDON, {}, tb, /*parent*/ nullptr);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_PARENT));
}

BOOST_AUTO_TEST_CASE(rejects_non_sequential_number)
{
    TestBlockHeader parent;
    parent.blockNumber = 5;
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 7;  // must be 6
    tb.expectedBlockHeader.gasLimit = 20000000;
    tb.expectedBlockHeader.timestamp = 100;
    parent.gasLimit = 20000000;
    parent.timestamp = 50;
    auto err = validateBlock(EVMC_PARIS, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_NUMBER));
}
}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j 2>&1 | tail -5`
Expected: FAIL — `helpers/BlockValidation.h` missing / `validateBlock` undefined. (Target is registered in Step 5.)

- [ ] **Step 3: Write `bcos-evm/test/helpers/BlockValidation.h`** (rules 1–10; blob rules 11–13 land in Task 1.2 as marked)

```cpp
#pragma once

#include "bcos-evm/eth-eest-test/BlockValidationErrors.h"
#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <optional>
#include <string>

namespace bcos::evm::reference_tests
{

/// EIP-1559 base fee from parent header. Denominator 8, elasticity 2.
inline uint64_t calcBaseFee(uint64_t parentGasLimit, uint64_t parentGasUsed, uint64_t parentBaseFee)
{
    constexpr uint64_t ELASTICITY = 2;
    constexpr uint64_t DENOM = 8;
    uint64_t const target = parentGasLimit / ELASTICITY;
    if (parentGasUsed == target)
        return parentBaseFee;
    if (parentGasUsed > target)
    {
        uint64_t const delta = parentBaseFee * (parentGasUsed - target) / target / DENOM;
        return parentBaseFee + std::max<uint64_t>(delta, 1);
    }
    uint64_t const delta = parentBaseFee * (target - parentGasUsed) / target / DENOM;
    return parentBaseFee - delta;
}

// Forward decls for blob helpers (implemented in Task 1.2, same header).
uint64_t calcExcessBlobGas(evmc_revision rev, BlobSchedule const& schedule,
    uint64_t parentBlobGasUsed, uint64_t parentExcessBlobGas);
BlobParams blobParamsFor(BlobSchedule const& schedule, std::string_view network);

/// Validate block-level consensus rules unrelated to individual transactions.
/// Returns nullopt if valid, else a BlockError code string.
inline std::optional<std::string> validateBlock(
    evmc_revision rev, BlobSchedule const& schedule, TestBlock const& tb, TestBlockHeader const* parent)
{
    auto const& h = tb.expectedBlockHeader;

    if (parent == nullptr)                                            // #1
        return BlockError::INVALID_BLOCK_PARENT;
    if (h.blockNumber != parent->blockNumber + 1)                    // #2
        return BlockError::INVALID_BLOCK_NUMBER;
    if (h.gasUsed > h.gasLimit)                                      // #3
        return BlockError::INCORRECT_BLOCK_FORMAT;

    uint64_t const pgl = static_cast<uint64_t>(parent->gasLimit);
    uint64_t const gl = static_cast<uint64_t>(h.gasLimit);
    uint64_t const maxDelta = pgl / 1024;
    if (gl >= pgl + maxDelta)                                        // #4
        return BlockError::INVALID_GASLIMIT;
    if (gl + maxDelta <= pgl)                                        // #5
        return BlockError::INVALID_GASLIMIT;
    if (gl < 5000)                                                  // #6
        return BlockError::INVALID_GASLIMIT;

    if (h.timestamp <= parent->timestamp)                           // #7
        return BlockError::INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT;

    // #8 Paris+ no-ommers: EEST fixtures never carry ommers in decoded form; nothing to check.

    if (h.extraData.size() > 32)                                    // #9
        return BlockError::INCORRECT_BLOCK_FORMAT;

    if (rev >= EVMC_LONDON)                                         // #10
    {
        uint64_t const expected =
            calcBaseFee(pgl, static_cast<uint64_t>(parent->gasUsed), parent->baseFeePerGas);
        if (h.baseFeePerGas != expected)
            return BlockError::INVALID_BASEFEE_PER_GAS;
    }

    // #11-#13 blob checks: added in Task 1.2.
    // #14 withdrawals parse, #15 EIP-7934 rlp size: added in Task 1.3.
    return std::nullopt;
}

}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 4: Register the test target in CMake** (`add_reference_test` links core; `helpers/BlockValidation.h` and `helpers/BlockTransition.h` both live in `bcos-evm/test/helpers/`, reached via the `..` include dir — same convention as `EthEestBlockchainRunner`.)

```cmake
add_reference_test(BlockValidationTest test/BlockValidationTest.cpp)
target_include_directories(BlockValidationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j && ctest --test-dir build-bcos-evm-check -R BlockValidationTest --output-on-failure`
Expected: PASS (4 cases). Note: `validateBlock` references `calcExcessBlobGas`/`blobParamsFor` only inside the Task 1.2 block, so forward decls suffice to link since they are unused until 1.2. If the linker complains about undefined refs, temporarily mark them `inline` no-op in this task — but they are not called yet, so no definition is required.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockValidation.h bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp bcos-evm/test/eth-eest-test/CMakeLists.txt && rtk git commit -m "feat(eest): block header validation rules 1-10 + EIP-1559 base fee"
```

---

### Task 1.2: Blob-gas helpers + header rules 11–13

**Files:**
- Modify: `bcos-evm/test/helpers/BlockValidation.h`
- Test: `bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp` (append)

**Interfaces:**
- Produces (definitions for the Task 1.1 forward decls, plus):
  - `uint64_t fakeExponential(uint64_t factor, uint64_t numerator, uint64_t denominator);`
  - `uint64_t computeBlobGasPrice(BlobParams const& p, uint64_t excessBlobGas);`
  - `uint64_t maxBlobGasPerBlock(BlobParams const& p);`

- [ ] **Step 1: Write the failing test** (append)

```cpp
BOOST_AUTO_TEST_CASE(excess_blob_gas_zero_below_target)
{
    // Cancun target=3 -> TARGET_BLOB_GAS = 3*131072 = 393216.
    // parentExcess=0, parentUsed=131072 (1 blob) -> sum < target -> 0
    BlobSchedule sched;  // empty -> blobParamsFor returns Cancun defaults
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, 131072, 0), 0ull);
}

BOOST_AUTO_TEST_CASE(excess_blob_gas_accumulates_above_target)
{
    // parentUsed = 6 blobs = 786432, parentExcess=0, target gas=393216
    // -> 786432 + 0 - 393216 = 393216
    BlobSchedule sched;
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, 786432, 0), 393216ull);
}

BOOST_AUTO_TEST_CASE(blob_gas_price_min_at_zero_excess)
{
    BlobParams p;  // Cancun defaults
    BOOST_CHECK_EQUAL(computeBlobGasPrice(p, 0), 1ull);  // MIN_BLOB_BASE_FEE
}

BOOST_AUTO_TEST_CASE(rejects_wrong_excess_blob_gas)
{
    TestBlockHeader parent;
    parent.blockNumber = 0; parent.gasLimit = 20000000; parent.timestamp = 0;
    parent.baseFeePerGas = 7; parent.gasUsed = 0;
    parent.blobGasUsed = 786432; parent.excessBlobGas = 0;   // -> expected excess 393216
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1; h.gasLimit = 20000000; h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0; h.excessBlobGas = 999999;             // wrong
    tb.inputBlobGasUsed = 0; tb.inputExcessBlobGas = 999999;
    auto err = validateBlock(EVMC_CANCUN, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INCORRECT_EXCESS_BLOB_GAS));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j 2>&1 | tail -5`
Expected: FAIL — helpers undefined / excess check not wired.

- [ ] **Step 3: Add blob helpers + rules 11–13 to `BlockValidation.h`**

Replace the two forward declarations with full definitions, and insert the Cancun+ block where `// #11-#13 blob checks` is:

```cpp
// ── Blob helpers (EIP-4844) ─────────────────────────────────
inline BlobParams blobParamsFor(BlobSchedule const& schedule, std::string_view network)
{
    if (auto it = schedule.find(std::string(network)); it != schedule.end())
        return it->second;
    // Defaults keyed by fork (used when fixture omits an explicit schedule).
    if (network == "Prague")
        return BlobParams{6, 9, 5007716};
    if (network == "Osaka" || network == "Amsterdam")
        return BlobParams{6, 9, 5007716};
    return BlobParams{3, 6, 3338477};  // Cancun
}

inline uint64_t fakeExponential(uint64_t factor, uint64_t numerator, uint64_t denominator)
{
    // Sum_{i>=0} factor * (numerator/denominator)^i / i!  (integer approximation, EIP-4844).
    unsigned __int128 i = 1;
    unsigned __int128 output = 0;
    unsigned __int128 numeratorAccum = static_cast<unsigned __int128>(factor) * denominator;
    while (numeratorAccum > 0)
    {
        output += numeratorAccum;
        numeratorAccum = (numeratorAccum * numerator) / (static_cast<unsigned __int128>(denominator) * i);
        ++i;
    }
    return static_cast<uint64_t>(output / denominator);
}

inline uint64_t computeBlobGasPrice(BlobParams const& p, uint64_t excessBlobGas)
{
    constexpr uint64_t MIN_BLOB_BASE_FEE = 1;
    return fakeExponential(MIN_BLOB_BASE_FEE, excessBlobGas, p.baseFeeUpdateFraction);
}

inline uint64_t maxBlobGasPerBlock(BlobParams const& p) { return uint64_t(p.max) * GAS_PER_BLOB; }

/// evmone: calc_excess_blob_gas (Cancun/Prague form; EIP-7918 Osaka refinement deferred to M3).
inline uint64_t calcExcessBlobGas(evmc_revision /*rev*/, BlobSchedule const& schedule,
    uint64_t parentBlobGasUsed, uint64_t parentExcessBlobGas)
{
    BlobParams const p = blobParamsFor(schedule, "Cancun");
    uint64_t const targetGas = uint64_t(p.target) * GAS_PER_BLOB;
    uint64_t const sum = parentExcessBlobGas + parentBlobGasUsed;
    return sum < targetGas ? 0 : sum - targetGas;
}
```

Then insert this block in `validateBlock`, replacing the `// #11-#13 blob checks` comment:

```cpp
    if (rev >= EVMC_CANCUN)
    {
        if (!tb.inputBlobGasUsed.has_value() || !tb.inputExcessBlobGas.has_value())  // #11
            return BlockError::INCORRECT_BLOCK_FORMAT;
        uint64_t const expectedExcess = calcExcessBlobGas(rev, schedule,             // #12
            parent->blobGasUsed.value_or(0), parent->excessBlobGas.value_or(0));
        if (tb.inputExcessBlobGas.value() != expectedExcess)
            return BlockError::INCORRECT_EXCESS_BLOB_GAS;
    }
    else if (tb.inputBlobGasUsed.has_value() || tb.inputExcessBlobGas.has_value())  // #13
        return BlockError::INCORRECT_BLOCK_FORMAT;
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j && ctest --test-dir build-bcos-evm-check -R BlockValidationTest --output-on-failure`
Expected: PASS (8 cases total).

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockValidation.h bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp && rtk git commit -m "feat(eest): blob-gas helpers + validate_block rules 11-13"
```

---

### Task 1.3: Header rules 14–15 (withdrawals parse + EIP-7934 RLP size)

**Files:**
- Modify: `bcos-evm/test/helpers/BlockValidation.h`
- Test: `bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp` (append)

**Interfaces:**
- Consumes: `TestBlock::withdrawalsParseSuccess`, `TestBlock::rlpSize`.
- Produces: `MAX_RLP_BLOCK_SIZE` constant; rules 14–15 in `validateBlock`.

- [ ] **Step 1: Write the failing test** (append)

```cpp
BOOST_AUTO_TEST_CASE(rejects_oversized_rlp_block_on_osaka)
{
    TestBlockHeader parent;
    parent.blockNumber = 0; parent.gasLimit = 20000000; parent.timestamp = 0;
    parent.baseFeePerGas = 7; parent.gasUsed = 0;
    parent.blobGasUsed = 0; parent.excessBlobGas = 0;
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1; h.gasLimit = 20000000; h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0; h.excessBlobGas = 0;
    tb.inputBlobGasUsed = 0; tb.inputExcessBlobGas = 0;
    tb.rlpSize = 9 * 1024 * 1024;  // > 8MB
    auto err = validateBlock(EVMC_OSAKA, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::RLP_BLOCK_LIMIT_EXCEEDED));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j && ctest --test-dir build-bcos-evm-check -R BlockValidationTest --output-on-failure`
Expected: FAIL — oversized block currently returns nullopt.

- [ ] **Step 3: Add rules 14–15** — after the blob block, before `return std::nullopt;`

```cpp
    if (!tb.withdrawalsParseSuccess)                                 // #14
        return BlockError::INCORRECT_BLOCK_FORMAT;

    constexpr size_t MAX_RLP_BLOCK_SIZE = 10u * 1024 * 1024 - 2u * 1024 * 1024;  // 8 MiB (EIP-7934)
    if (rev >= EVMC_OSAKA && tb.rlpSize > MAX_RLP_BLOCK_SIZE)        // #15
        return BlockError::RLP_BLOCK_LIMIT_EXCEEDED;
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j && ctest --test-dir build-bcos-evm-check -R BlockValidationTest --output-on-failure`
Expected: PASS (9 cases).

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockValidation.h bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp && rtk git commit -m "feat(eest): validate_block rules 14-15 (withdrawals parse + EIP-7934)"
```

---

### Task 1.4: Extend `BlockApplyResult` + `TransactionReceipt`

**Files:**
- Modify: `bcos-evm/test/helpers/BlockTransition.h`
- Test: `bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp` (append a struct-shape compile check)

**Interfaces:**
- Produces (new fields, all default-initialized so existing state-test callers are unaffected):
  - `TransactionReceipt`: `bcos::bytes bloom;` `std::vector<state::LogEntry> logs;`
  - `BlockApplyResult`: `bcos::bytes bloom;` `std::vector<bcos::bytes> requests;` `std::optional<std::string> requestsError;` `std::vector<size_t> rejected;` `uint64_t blobGasLeft = 0;`

- [ ] **Step 1: Write a compile-time shape test** (append to `BlockValidationTest.cpp`)

```cpp
#include "helpers/BlockTransition.h"
BOOST_AUTO_TEST_CASE(block_apply_result_has_extended_fields)
{
    BlockApplyResult r;
    r.bloom.resize(256, 0);
    r.rejected.push_back(0);
    r.requests.emplace_back();
    r.requestsError = std::nullopt;
    r.blobGasLeft = 0;
    TransactionReceipt rc;
    rc.bloom.resize(256, 0);
    rc.logs.clear();
    BOOST_CHECK_EQUAL(r.bloom.size(), 256u);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j 2>&1 | tail -5`
Expected: FAIL — `bloom`/`rejected`/`requests`/`logs` are not members.

- [ ] **Step 3: Extend the structs in `BlockTransition.h`**

In `struct TransactionReceipt` add after `int64_t gasUsed = 0;`:

```cpp
    bcos::bytes bloom;                    // 256-byte logs bloom for this tx
    std::vector<state::LogEntry> logs;    // all logs (supersedes single `log`; `log` kept for compat)
```

In `struct BlockApplyResult` add after `int64_t gasUsed = 0;`:

```cpp
    bcos::bytes bloom;                       // aggregate 256-byte block logs bloom
    std::vector<bcos::bytes> requests;       // EIP-7685 requests (Prague+); see §8.3.1
    std::optional<std::string> requestsError;
    std::vector<size_t> rejected;            // indices of txs rejected during block apply
    uint64_t blobGasLeft = 0;                // maxBlobGasPerBlock - consumed
```

Add `#include <optional>` and `#include <string>` at the top if not present.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockValidationTest -j && ctest --test-dir build-bcos-evm-check -R BlockValidationTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Confirm no state-test regression** (extended fields are additive)

Run: `cmake --build build-bcos-evm-check --target EthExecutionSpecStateTests -j && ctest --test-dir build-bcos-evm-check -R 'EthExecutionSpecStateTests$' --output-on-failure`
Expected: PASS (unchanged behavior).

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockTransition.h bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp && rtk git commit -m "feat(eest): extend BlockApplyResult/TransactionReceipt for block validation"
```

---

# Phase 2 — MPT roots + runner refactor (drives Cancun toward ≥50%)

> **Design note (signature capture):** `transactionsTrie` is the MPT over the *signed* transaction RLPs, which include `v,r,s`. `GstTransactionTemplate` drops signatures, so the loader must additionally capture each tx's canonical signed RLP. We do this by encoding directly from the JSON fields (which carry `type, chainId, nonce, gasPrice|maxFee*, gasLimit, to, value, data, accessList, v, r, s, ...`). `computeTxRoot` then operates on already-encoded bytes.

### Task 2.1: Capture signed-tx RLP in the loader

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h` (add `std::vector<bcos::bytes> rawTxRlp;` to `TestBlock`)
- Modify: `bcos-evm/test/eth-eest-test/src/BlockchainTestLoader.cpp` (encode each tx from JSON)
- Test: `bcos-evm/test/eth-eest-test/test/BlockchainTestLoaderTest.cpp` (append)

**Interfaces:**
- Produces: `TestBlock::rawTxRlp` — one canonical signed-tx RLP per transaction, type-prefixed for typed txs.
- Internal: `bcos::bytes encodeSignedTxFromJson(boost::property_tree::ptree const& tx)`.

**Transaction type field order (canonical signed RLP; each is a list unless noted):**
- **Legacy (`type` absent or `0x0`):** `[nonce, gasPrice, gasLimit, to, value, data, v, r, s]` — no type prefix.
- **EIP-2930 (`0x1`):** prefix `0x01` ++ `[chainId, nonce, gasPrice, gasLimit, to, value, data, accessList, yParity, r, s]`.
- **EIP-1559 (`0x2`):** prefix `0x02` ++ `[chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, yParity, r, s]`.
- **EIP-4844 (`0x3`):** prefix `0x03` ++ `[chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, maxFeePerBlobGas, blobVersionedHashes, yParity, r, s]`.
- **EIP-7702 (`0x4`):** prefix `0x04` ++ `[chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, authorizationList, yParity, r, s]`.

`accessList` element = `[address, [storageKey...]]`. `authorizationList` element = `[chainId, address, nonce, yParity, r, s]`. `to` empty = zero-length byte string (contract creation). Use the existing `rlpEncodeRaw/Uint64/U256/List` helpers (make them shared — see Step 2).

- [ ] **Step 1: Write the failing test** (tx root of a known single-tx Cancun block)

```cpp
BOOST_AUTO_TEST_CASE(captures_raw_tx_rlp_for_cancun_block)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_json(
        (std::filesystem::path(SPECS_TESTS_EEST_ROOT) /
         "fixtures/blockchain_tests/cancun/eip5656_mcopy/test_mcopy_on_empty_memory.json").string(),
        root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& blk = tests.front().testBlocks.front();
    BOOST_CHECK_EQUAL(blk.rawTxRlp.size(), blk.transactions.size());
    if (!blk.rawTxRlp.empty())
        BOOST_CHECK(!blk.rawTxRlp.front().empty());
}
```

- [ ] **Step 2: Expose RLP primitives for reuse**

The RLP helpers (`rlpEncodeRaw`, `rlpEncodeUint64`, `rlpEncodeU256`, `rlpEncodeList`) currently live in the anonymous namespace of `src/GstStateHash.cpp`. Promote their declarations into `include/bcos-evm/eth-eest-test/GstStateHash.h` (keep definitions in the .cpp, remove `static`/anon-namespace for these four) so the loader can call them:

```cpp
// in GstStateHash.h, namespace bcos::evm::reference_tests
bcos::bytes rlpEncodeRaw(bcos::bytes const& input);
bcos::bytes rlpEncodeUint64(uint64_t value);
bcos::bytes rlpEncodeU256(bcos::u256 value);
bcos::bytes rlpEncodeList(std::vector<bcos::bytes> const& items);
```

- [ ] **Step 3: Implement `encodeSignedTxFromJson`** in `BlockchainTestLoader.cpp` following the field-order table above, and populate `tb.rawTxRlp` in `parseTestBlock` (iterate the same `transactions`/`rlp_decoded.transactions` array used for `parseTransactionTemplate`). Encode `v`/`r`/`s` (legacy) or `yParity`(`v`)/`r`/`s` (typed) from the JSON hex fields via `rlpEncodeRaw(hexToBytes(...))` (strip leading zero bytes for scalars).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target BlockchainTestLoaderTest -j && ctest --test-dir build-bcos-evm-check -R BlockchainTestLoaderTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GstStateHash.h bcos-evm/test/eth-eest-test/src/GstStateHash.cpp bcos-evm/test/eth-eest-test/src/BlockchainTestLoader.cpp bcos-evm/test/eth-eest-test/test/BlockchainTestLoaderTest.cpp && rtk git commit -m "feat(eest): capture signed-tx RLP + expose rlp primitives"
```

---

### Task 2.2: `computeTxRoot` + `computeReceiptsRoot` + `computeWithdrawalRoot`

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GstStateHash.h`
- Modify: `bcos-evm/test/eth-eest-test/src/GstStateHash.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/GstStateHashTest.cpp` (append)

**Interfaces:**
- Produces:
  - `evmc_bytes32 computeTxRoot(std::span<const bcos::bytes> signedTxRlps);`
  - `evmc_bytes32 computeReceiptsRoot(std::span<const TransactionReceipt> receipts);`
  - `evmc_bytes32 computeWithdrawalRoot(std::span<const Withdrawal> withdrawals);`
- Consumes: internal `TrieEntry`, `hashTrieEntries`, `rlpEncode*`, `encodeLog`, `keccak256`, `EMPTY_ROOT`.

**Trie construction:** for an ordered list, key = `rlpEncodeUint64(index)` (RLP of the index integer, `0x80` for 0), value = the item's encoding. Reuse `hashTrieEntries`.

**Receipt encoding (per tx):** `payload = rlpEncodeList([statusByte, rlpEncodeUint64(cumulativeGasUsed), rlpEncodeRaw(bloom256), rlpEncodeList(encodedLogs)])`; for typed txs prepend the tx type byte to `payload`. `statusByte` = `rlpEncodeUint64(1)` on success, `{0x80}` (empty) on failure. Cumulative gas is the running sum across the block.

**Withdrawal encoding (per item):** `rlpEncodeList([rlpEncodeUint64(index), rlpEncodeUint64(validatorIndex), rlpEncodeRaw(addressBytes), rlpEncodeUint64(amount)])`.

- [ ] **Step 1: Write failing tests** (append to `GstStateHashTest.cpp`)

```cpp
BOOST_AUTO_TEST_CASE(empty_tx_root_is_empty_mpt)
{
    auto const r = computeTxRoot({});
    // EMPTY_MPT_HASH = keccak256(RLP("")) = 0x56e81f...b421
    evmc_bytes32 const empty = {0x56,0xe8,0x1f,0x17,0x1b,0xcc,0x55,0xa6,0xff,0x83,0x45,0xe6,
        0x92,0xc0,0xf8,0x6e,0x5b,0x48,0xe0,0x1b,0x99,0x6c,0xad,0xc0,0x01,0x62,0x2f,0xb5,0xe3,
        0x63,0xb4,0x21};
    BOOST_CHECK_EQUAL(std::memcmp(r.bytes, empty.bytes, 32), 0);
}

BOOST_AUTO_TEST_CASE(single_withdrawal_root_is_deterministic)
{
    Withdrawal w; w.index = 0; w.validatorIndex = 0; w.amount = 0x5209;
    auto const r = computeWithdrawalRoot(std::span<const Withdrawal>(&w, 1));
    // Non-empty, and stable across runs.
    BOOST_CHECK(std::memcmp(r.bytes, decltype(r){}.bytes, 32) != 0);
    BOOST_CHECK_EQUAL(std::memcmp(r.bytes, computeWithdrawalRoot(std::span<const Withdrawal>(&w,1)).bytes, 32), 0);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-bcos-evm-check --target GstStateHashTest -j 2>&1 | tail -5`
Expected: FAIL — functions undefined.

- [ ] **Step 3: Implement the three functions** in `GstStateHash.cpp` (declare in header) using the encodings above. Add `#include <span>` and include `BlockchainTestTypes.h` + `helpers/BlockTransition.h` for `Withdrawal`/`TransactionReceipt` (or forward-declare and take spans of them; prefer including `BlockchainTestTypes.h` for `Withdrawal` and adding a small `ReceiptForRoot` POD if include cycles arise — if `BlockTransition.h` include is circular, define `computeReceiptsRoot` over a minimal `{status, cumulativeGasUsed, bloom, logs}` view passed by the runner).

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-bcos-evm-check --target GstStateHashTest -j && ctest --test-dir build-bcos-evm-check -R GstStateHashTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GstStateHash.h bcos-evm/test/eth-eest-test/src/GstStateHash.cpp bcos-evm/test/eth-eest-test/test/GstStateHashTest.cpp && rtk git commit -m "feat(eest): computeTxRoot/computeReceiptsRoot/computeWithdrawalRoot"
```

---

### Task 2.3: Aggregate block logs bloom in `applyEthBlock`

**Files:**
- Modify: `bcos-evm/test/helpers/BlockTransition.h`
- Test: `bcos-evm/test/eth-eest-test/runners/eth/EthBlockTransitionTest.cpp` (append) or a new bloom unit case in `GstStateHashTest.cpp`

**Interfaces:**
- Consumes: existing `helpers/BloomFilter.hpp` (per spec §4.1). If it does not yet expose a log→bloom function, add `bcos::bytes computeLogsBloom(std::vector<state::LogEntry> const&)` there.
- Produces: `BlockApplyResult::bloom` (256 bytes) and per-receipt `TransactionReceipt::bloom` populated; `TransactionReceipt::logs` filled with all logs (not just `.front()`).

- [ ] **Step 1: Write failing test** — a block with one LOG-producing tx yields a non-zero `result.bloom` of size 256.

```cpp
// pseudo-fixture: reuse an existing EthBlockTransitionTest helper that builds a simple tx.
// Assert result.bloom.size()==256 and it is not all-zero when logs exist.
```

- [ ] **Step 2: Run to verify failure** — `result.bloom` empty.

Run: `cmake --build build-bcos-evm-check --target EthBlockTransitionTest -j && ctest --test-dir build-bcos-evm-check -R EthBlockTransitionTest --output-on-failure`

- [ ] **Step 3: Implement** — in `applyEthBlock`, replace the single-log capture with full logs and bloom:

```cpp
        receipt.logs = execResult.logs;                    // was: receipt.log = logs.front()
        if (!execResult.logs.empty())
            receipt.log = execResult.logs.front();         // keep compat field
        receipt.bloom = computeLogsBloom(execResult.logs);
```

After the tx loop, aggregate: `result.bloom.assign(256, 0); for (auto& rc : result.receipts) bloomOr(result.bloom, rc.bloom);` where `bloomOr` byte-wise ORs.

- [ ] **Step 4: Run to verify pass.**

Run: `cmake --build build-bcos-evm-check --target EthBlockTransitionTest -j && ctest --test-dir build-bcos-evm-check -R EthBlockTransitionTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockTransition.h bcos-evm/test/helpers/BloomFilter.hpp bcos-evm/test/eth-eest-test/runners/eth/EthBlockTransitionTest.cpp && rtk git commit -m "feat(eest): aggregate logs bloom in applyEthBlock"
```

---

### Task 2.4: Refactor `EthEestBlockchainRunner` to loader + validation + header checks

**Files:**
- Modify (refactor): `bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp`

**Interfaces:**
- Consumes: `loadBlockchainTests`, `validateBlock`, `resolveRevision`, `applyEthBlock`, `computeStateRoot`, `computeTxRoot`, `computeReceiptsRoot`.
- Produces: CLI runner that, per test, walks blocks and reports PASS/FAIL; `--fixtures <dir> [--limit N]` and (new) `--manifest <json> --eest-root <dir>` (manifest wiring completed in Phase 4; add the flag stub now).

**Runner core (per test) — implements spec §8.1/§8.2/§8.3/§8.4:**
- genesis checks (number/gasUsed/roots/bloom per §8.1);
- maintain `block_data[hash] -> {header, postState, total_difficulty}`, seeded with genesis;
- per block: `rev = resolveRevision(network, ts)`; find parent by `blockInfo.parentHash`; `err = validateBlock(...)`;
  - invalid path (`expectException` non-empty): if `err`, assert `expectException.find(*err) != npos` → continue; else execute and run Level 2/2.5/3 (rejected txs → `TransactionException.` substring; `requestsError`; header-field mismatch), FAIL only if all match;
  - valid path: `EXPECT !err`; execute from parent postState; compare stateRoot/txRoot(from `rawTxRlp`)/receiptsRoot/gasUsed/bloom (+ withdrawalsRoot in Phase 3, requestsHash in Phase 3); record BlockData; update canonical tip by `total_difficulty >=` (PoS: last valid block);
- final: `canonical_tip == lastBlockHash`; post-state via `postState`/`postStateHash`.

- [ ] **Step 1: Rewrite the runner** to the structure above, deleting the old inline parse/`runOneFixture`. Keep the `main()` CLI arg handling; add `--manifest`/`--eest-root` parsing that (for now) errors with "manifest mode lands in Phase 4" if used. On any FAIL print `FAIL <testName> <reason>` to `std::cerr` and set exit code 1 at the end; print `PASS <file> (<n> tests)` otherwise.

- [ ] **Step 2: Build the runner.**

Run: `cmake --build build-bcos-evm-check --target EthEestBlockchainRunner -j`
Expected: builds clean.

- [ ] **Step 3: Run against Cancun and record baseline** (M1 mid-point ≥50%)

Run: `./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestBlockchainRunner --fixtures build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src/blockchain_tests/cancun 2>&1 | tail -20`
Expected: a Results line `N passed, M failed`. **Gate:** passed/(passed+failed) ≥ 0.50 for valid-block files. Record the number in the commit message.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp && rtk git commit -m "refactor(eest): blockchain runner on loader+validation (cancun >=50%)"
```

---

# Phase 3 — P1 features + Granular runner (completes M1: Cancun ≥90%)

### Task 3.1: Withdrawal balance credit + `withdrawalsRoot` check (spec §10.3)

**Files:**
- Modify: `bcos-evm/test/helpers/BlockTransition.h` (add `std::span<const Withdrawal> withdrawals = {}` param; credit balances)
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp` (pass `tb.withdrawals`; assert `computeWithdrawalRoot`)
- Test: `bcos-evm/test/eth-eest-test/runners/eth/EthBlockTransitionTest.cpp` (append credit case)

**Interfaces:**
- Produces: `applyEthBlock(..., std::span<const Withdrawal> withdrawals = {})`; withdrawal amounts credited as `amount * 1e9` Wei after all txs, before post-state build.

- [ ] **Step 1: Failing test** — pre-state balance B, one withdrawal of `amount` Gwei to that address → post balance `B + amount*1e9`.

- [ ] **Step 2: Run to verify failure.**

Run: `cmake --build build-bcos-evm-check --target EthBlockTransitionTest -j && ctest --test-dir build-bcos-evm-check -R EthBlockTransitionTest --output-on-failure`

- [ ] **Step 3: Implement credit** in `applyEthBlock`, after the tx loop and before `buildPostStateView`:

```cpp
    for (auto const& w : withdrawals)
    {
        bool found = false;
        for (auto& [addr, acc] : preStatePairs)
            if (state::AddressEqual{}(addr, w.address))
            { acc.balance += bcos::u256(w.amount) * bcos::u256(1000000000); found = true; break; }
        if (!found)
        {
            state::Account acc; acc.balance = bcos::u256(w.amount) * bcos::u256(1000000000);
            preStatePairs.emplace_back(w.address, std::move(acc));
        }
    }
```

Add `#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"` for `Withdrawal`. In the runner's valid-block path add, for `rev >= EVMC_SHANGHAI`, `EXPECT computeWithdrawalRoot(tb.withdrawals) == tb.expectedBlockHeader.withdrawalsRoot`.

- [ ] **Step 4: Run to verify pass** (unit + Cancun sweep).

Run: `cmake --build build-bcos-evm-check --target EthBlockTransitionTest EthEestBlockchainRunner -j && ctest --test-dir build-bcos-evm-check -R EthBlockTransitionTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockTransition.h bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp bcos-evm/test/eth-eest-test/runners/eth/EthBlockTransitionTest.cpp && rtk git commit -m "feat(eest): withdrawal balance credit + withdrawalsRoot check"
```

---

### Task 3.2: `computeRequestsHash` + Prague requests (XFAIL-guarded, spec §8.3.1)

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/src/GstStateHash.cpp` + header (`computeRequestsHash`)
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/GstStateHashTest.cpp` (append)

**Interfaces:**
- Produces: `evmc_bytes32 computeRequestsHash(std::span<const bcos::bytes> requests);` — `sha256(concat over i of sha256(typeByte_i ++ payload_i))` per EIP-7685. (Implement with the crypto lib's SHA-256; if unavailable, use the existing keccak path is WRONG — requests use SHA-256, so wire `bcos::crypto::sha256`.)

- [ ] **Step 1: Failing test** — empty requests → `sha256("")`-based empty hash equals the EIP-7685 empty value; one known `(type,payload)` → deterministic.

- [ ] **Step 2: Run to verify failure.**

Run: `cmake --build build-bcos-evm-check --target GstStateHashTest -j && ctest --test-dir build-bcos-evm-check -R GstStateHashTest --output-on-failure`

- [ ] **Step 3: Implement `computeRequestsHash`** per EIP-7685. In the runner, for `rev >= EVMC_PRAGUE`, if `res.requests` is populated assert `computeRequestsHash(res.requests) == tb.expectedBlockHeader.requestsHash`; **otherwise `GTEST_SKIP`/log XFAIL** (requests collection from system contracts is not wired for M1). Record this limitation in the matrix (§6) under M2.

- [ ] **Step 4: Run to verify pass.**

Run: `cmake --build build-bcos-evm-check --target GstStateHashTest -j && ctest --test-dir build-bcos-evm-check -R GstStateHashTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GstStateHash.h bcos-evm/test/eth-eest-test/src/GstStateHash.cpp bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp bcos-evm/test/eth-eest-test/test/GstStateHashTest.cpp && rtk git commit -m "feat(eest): computeRequestsHash + prague requests (xfail-guarded)"
```

---

### Task 3.3: Refactor `EthEestBlockGranular` to per-file execution GTest

**Files:**
- Modify (refactor): `bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockGranular.cpp`

**Interfaces:**
- Consumes: same loader/validation/runner core as Task 2.4. Extract the per-test logic from the CLI runner into a shared header `helpers/BlockchainRunCore.h` (returns a `std::vector<std::string>` of failure messages) so both runners call it.

- [ ] **Step 1: Extract shared core** — create `bcos-evm/test/helpers/BlockchainRunCore.h` with `std::vector<std::string> runBlockchainTest(BlockchainTest const&, evmc::VM&, bcos::crypto::Hash&);` (moved from the CLI runner body). CLI runner and granular both call it. No behavior change.

- [ ] **Step 2: Rewrite `EthEestBlockGranular`** to register one GTest per `.json` file (keep the existing dynamic `RegisterTest` machinery), and in `TestBody` call `loadBlockchainTests` + `runBlockchainTest`, `EXPECT_TRUE(failures.empty())` with the joined messages.

- [ ] **Step 3: Build + run Cancun filter.**

Run: `cmake --build build-bcos-evm-check --target EthEestBlockGranular -j && ./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestBlockGranular build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src/blockchain_tests --gtest_filter='*cancun*' 2>&1 | tail -15`
Expected: per-file pass/fail; **Gate (M1): Cancun ≥90%** of files pass. Record the ratio.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/helpers/BlockchainRunCore.h bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockGranular.cpp && rtk git commit -m "refactor(eest): shared blockchain run core + granular execution (cancun >=90%, M1)"
```

---

# Phase 4 — Full sweep + CI hard gates

### Task 4.1: Curated smoke manifest + switch PR gate

**Files:**
- Create: `bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-blockchain-smoke.json`
- Modify: `bcos-evm/test/eth-eest-test/CMakeLists.txt` (EthEestBlockchainSmoke → manifest; see spec §12.1)

**Interfaces:**
- The runner's `--manifest`/`--eest-root` mode (stubbed in Task 2.4) is implemented here to select a curated list of Cancun files that pass M1.

- [ ] **Step 1: Implement `--manifest` mode** in `EthEestBlockchainRunner`: read a JSON array of relative fixture paths under `<eest-root>/fixtures/blockchain_tests/`, run each, FAIL the process if any fails.

- [ ] **Step 2: Author the manifest** — 20–50 Cancun fixtures confirmed passing in Task 3.3 (list exact relative paths).

- [ ] **Step 3: Update CTest** to the manifest form (spec §12.1):

```cmake
add_test(NAME EthEestBlockchainSmoke COMMAND EthEestBlockchainRunner
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth/eth-eest-blockchain-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT})
set_tests_properties(EthEestBlockchainSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest")
```

- [ ] **Step 4: Run smoke.**

Run: `ctest --test-dir build-bcos-evm-check -R EthEestBlockchainSmoke --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-blockchain-smoke.json bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp bcos-evm/test/eth-eest-test/CMakeLists.txt && rtk git commit -m "feat(eest): blockchain smoke manifest as PR gate"
```

---

### Task 4.2: Nightly full-sweep CTests (crash-free gate)

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/CMakeLists.txt`

- [ ] **Step 1: Add full CTests** (spec §12.1):

```cmake
add_test(NAME EthEestBlockchainFull COMMAND EthEestBlockchainRunner
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests)
set_tests_properties(EthEestBlockchainFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest;nightly" TIMEOUT 14400)

add_test(NAME EthEestBlockGranularFull COMMAND EthEestBlockGranular
    ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests)
set_tests_properties(EthEestBlockGranularFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest;nightly" TIMEOUT 14400)
```

- [ ] **Step 2: Local dry-run of the full sweep (crash-free gate — failures allowed, crashes not).**

Run: `ctest --test-dir build-bcos-evm-check -R 'EthEestBlockchainFull' --output-on-failure 2>&1 | tail -20`
Expected: process exits without segfault/uncaught exception (non-zero from failed vectors is acceptable at this stage; the CI job records baseline, see Task 4.3).

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/test/eth-eest-test/CMakeLists.txt && rtk git commit -m "build(eest): nightly full blockchain sweep ctests"
```

---

### Task 4.3: Wire nightly workflow + update matrix

**Files:**
- Modify: `.github/workflows/specs-tests-nightly.yml`
- Modify: `bcos-evm/test/eth-eest-test/eest-integration-matrix.md` (§6 baseline)

- [ ] **Step 1: Add a nightly job** running `ctest -L 'specs-tests-full' -R 'blockchain' --output-on-failure` with `continue-on-error: true` (crash-free gate; parity ratio tracked, not blocking) mirroring the existing granular-full job.

- [ ] **Step 2: Update matrix §6** with the measured Cancun ratio (M1) and note M2–M5 as Phase 4.x parity loop; flip the `EthEestBlockchainRunner` row from "smoke only" to "validate_block + MPT + invalid-block".

- [ ] **Step 3: Commit**

```bash
rtk git add .github/workflows/specs-tests-nightly.yml bcos-evm/test/eth-eest-test/eest-integration-matrix.md && rtk git commit -m "ci(eest): nightly blockchain full sweep + matrix baseline"
```

---

## Self-Review

**Spec coverage:** §3 architecture → Tasks 0.1–0.4, 2.4, 3.3 (layered types/loader/validation/runner). §4 types → 0.1. §5 loader → 0.3. §6 validateBlock 15 rules → 1.1/1.2/1.3. §7 MPT roots (extend GstStateHash) → 2.1/2.2/3.2. §8.1–8.4 runner/invalid-block/canonical → 2.4. §8.3.1 requests → 3.2. §9 blob math → 1.2. §10.3 withdrawal credit → 3.1. §11 file modifications → covered. §12 CTest → 4.1/4.2. §15 acceptance (M1 + CI gates) → 2.4 (≥50%), 3.3 (≥90%), 4.1/4.2/4.3. §16 phases → sections. §17 open questions: Q2 closed (GstStateHash reuse) → 2.1/2.2; Q4 resolveRevision → 0.2; Q5 Withdrawal type → 0.1; Q6 receipt/apply-result extension regression → 1.4 Step 5.

**Placeholder scan:** No "TODO/TBD". Heavy encoders (typed-tx RLP in 2.1, receipt RLP in 2.2) carry explicit per-type field-order tables rather than full code for all 5 types — an implementer has the exact field orderings and the shared `rlpEncode*` primitives. Requests collection from system contracts is intentionally XFAIL-guarded for M1 (spec §8.3.1) and deferred to M2, not a hidden placeholder.

**Type consistency:** `computeTxRoot/ReceiptsRoot/WithdrawalRoot/RequestsHash` all return `evmc_bytes32` (Global Constraints). `TestBlockHeader` hash fields are `evmc_bytes32`. `resolveRevision` returns `std::optional<evmc_revision>`. `validateBlock` signature identical across 1.1/1.2/1.3. `applyEthBlock` gains a trailing defaulted `withdrawals` param (3.1) — additive, existing callers unaffected (verified in 1.4 Step 5 / 2.4).

**Known follow-ups (not M1-blocking):** EIP-7918 Osaka blob-fee refinement (M3), requests system-contract collection (M2), PoW reorg canonical selection (M5), legacy `ethereum/tests` format (Phase 5), engine/sync (Phase 5).




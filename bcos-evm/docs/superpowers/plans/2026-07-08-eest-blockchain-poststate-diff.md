# EEST Blockchain postState Full Account Diff — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the EEST blockchain test runner a complete, partial-semantics `postState` account diff (nonce/balance/code/storage/absent) with actionable failure messages, replacing the incomplete inline `expectPostStateMatches`.

**Architecture:** Extract a standalone `BlockchainPostStateAssert` module into `bcos-evm-specs-tests-core`. Introduce `ExpectedPostAccount` with field-presence flags and a `PostStateExpectation` (hash-or-accounts). The loader populates it; the runner (`BlockchainRunCore.h`) and `BlockValidationTest.cpp` call the shared `assertPostState`. No EVM runtime changes.

**Tech Stack:** C++20, evmc types, `boost::property_tree` (JSON), Boost.Test (unit tests via `add_reference_test`), CMake, evmone.

**Spec:** `docs/superpowers/specs/2026-07-08-eest-blockchain-poststate-diff-design.md`

## Global Constraints

- **Harness-only:** No changes under `bcos-evm/eth/` or any EVM/state-transition code. (spec §1.4)
- **No new third-party dependencies.** Reuse `boost::property_tree` + Boost.Test. (spec §7)
- **Zero regressions:** `EthEestBlockchainSmoke` / `EthEestBlockchainFull` (incl. `blockchain_tests/static/` tree, `40855/40855` cases) must stay green. (spec §1.3, §9)
- **Partial EEST semantics:** only listed addresses; only JSON-present fields; extra accounts/slots ignored (except `storage: {}` all-zero mode). Never fail on unlisted accounts. (spec §2.2)
- **Naming:** `PostStateExpectation`, `ExpectedPostAccount`, `PostStateFieldDiff`, `PostStateAssertReport`, `assertPostState`, `AssertOptions` (parallels `StateTestAssert::AssertReport` / `assertResult`). (spec Frozen decisions)
- **Type match:** `ExpectedPostAccount::balance` is `bcos::u256` (matches `state::Account::balance` and `bcos::fromBigQuantity`), NOT `intx::uint256`. Storage is an **ordered** `std::vector<std::pair<evmc_bytes32,evmc_bytes32>>` to give deterministic first-diff order.
- **Build dir convention:** `build-bcos-evm-check` (matrix baseline).

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainPostStateAssert.h` | Types (`ExpectedPostAccount`, `PostStateExpectation`, `PostStateFieldDiff`, `PostStateAssertReport`, `AssertOptions`) + `assertPostState` decl | Create |
| `test/eth-eest-test/src/BlockchainPostStateAssert.cpp` | `assertPostState` algorithm (§4.4) | Create |
| `test/eth-eest-test/test/BlockchainPostStateAssertTest.cpp` | Boost.Test unit coverage | Create |
| `test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h` | **Add** `PostStateExpectation postExpectation` alongside existing raw `postState`/`postStateHash` (kept) | Modify |
| `test/eth-eest-test/src/BlockchainTestLoader.cpp` | Populate raw fields **and** `postExpectation` (presence flags, hash, precedence) in one pass | Modify |
| `test/helpers/BlockchainRunCore.h` | Remove inline `expectPostStateMatches`; call `assertPostState(test.postExpectation, ...)` with `eip158ClearEmpty` from `genesisRev` | Modify |
| `test/eth-eest-test/test/BlockValidationTest.cpp` | Redirect single `expectPostStateMatches` call site to `assertPostState`; drop invalid full-hash self-check. **Other ~6 bespoke probes keep using raw `picked->postState` unchanged** (they use `storage.find()`, incompatible with the ordered vector) | Modify |
| `test/eth-eest-test/CMakeLists.txt` | Add `.cpp` to core lib; register `BlockchainPostStateAssertTest` | Modify |
| `test/eth-eest-test/eest-integration-matrix.md` | Mark gap #3 / P1 done | Modify (Task 3) |
| `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md` | Fix §8.1 pseudocode (field-diff not root-of-view) | Modify (Task 3) |

---

## Task 0: Corpus + `null` parsing probe (blocking investigation)

**Files:** none modified (investigation only; findings recorded in commit message + spec §4.3 note if needed).

**Interfaces:**
- Consumes: nothing.
- Produces: a documented decision for Task 2 loader — (a) how many blockchain fixtures use `postState` object vs `postStateHash`, (b) whether any `postState` account is JSON `null` (absent), (c) whether `boost::property_tree` can distinguish `null` from `{}`.

- [ ] **Step 1: Count postState vs postStateHash fixtures**

Run (adjust `$EVM_REF_EEST_ROOT` to the configured fetch dir, e.g. `build-bcos-evm-check/_deps/eest-fixtures-src` or the value of `EVM_REF_EEST_ROOT`):

```bash
EEST=$EVM_REF_EEST_ROOT
rg -l '"postState"'     "$EEST/fixtures/blockchain_tests" | wc -l
rg -l '"postStateHash"' "$EEST/fixtures/blockchain_tests" | wc -l
```

Expected: both counts print (non-negative integers). Record them.

- [ ] **Step 2: Detect null / absent postState accounts**

```bash
rg -n '"0x[0-9a-fA-F]{40}"\s*:\s*null' "$EEST/fixtures/blockchain_tests" | head
```

Expected: either no matches (⇒ `Kind::Absent` only needs synthetic unit coverage) or a list (⇒ record example paths).

- [ ] **Step 3: Detect partial-field postState accounts (loose-field impact)**

```bash
# accounts that provide storage but omit balance, as a sanity sample
rg -l '"postState"' "$EEST/fixtures/blockchain_tests" | head -20 | while read f; do
  echo "== $f"; rg -n '"balance"|"nonce"|"code"|"storage"' "$f" | head;
done
```

Expected: qualitative confirmation of which fields authors typically include. Record whether any account omits `nonce`/`balance`.

- [ ] **Step 4: Confirm property_tree null behavior**

Rationale: `boost::property_tree::read_json` does not preserve a distinct JSON `null`; a `null` value typically yields an empty node indistinguishable from `{}`. Verify with a scratch check (no repo change):

```bash
cat >/tmp/pt_null_probe.cpp <<'EOF'
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <iostream>
int main() {
    std::string j = R"({"a": null, "b": {}, "c": {"x":"1"}})";
    boost::property_tree::ptree t; std::istringstream is(j);
    boost::property_tree::read_json(is, t);
    for (auto& [k, v] : t)
        std::cout << k << " data='" << v.data() << "' children=" << v.size() << "\n";
}
EOF
g++ -std=c++20 /tmp/pt_null_probe.cpp -o /tmp/pt_null_probe && /tmp/pt_null_probe
```

Expected: `a` and `b` are indistinguishable (both `data=''`, `children=0`). Record the exact output.

- [ ] **Step 5: Record decision**

Write one paragraph (commit body of Task 1, or append to spec §4.3) capturing counts + the loader strategy chosen:
- If Step 2 found **no** null accounts AND Step 4 confirms indistinguishability ⇒ loader treats empty/null node as `Kind::Present` presence-only; `Kind::Absent` is exercised by synthetic unit tests only (Task 1). This is the assumed default for Task 2.
- If Step 2 found null accounts ⇒ flag as a follow-up (needs a null-preserving parse); do NOT block Task 1/2 which still ship for the common case.

- [ ] **Step 6: Commit findings note**

```bash
git add docs/superpowers/specs/2026-07-08-eest-blockchain-poststate-diff-design.md
git commit -m "docs(eest): record postState corpus + property_tree null probe findings"
```

(If no spec edit was needed, skip the commit — Task 0 is investigation and may produce no diff.)

---

## Task 1: `BlockchainPostStateAssert` module + unit tests

**Files:**
- Create: `test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainPostStateAssert.h`
- Create: `test/eth-eest-test/src/BlockchainPostStateAssert.cpp`
- Create: `test/eth-eest-test/test/BlockchainPostStateAssertTest.cpp`
- Modify: `test/eth-eest-test/CMakeLists.txt:10-24` (add source to `bcos-evm-specs-tests-core`), `:96` area (register test)

**Interfaces:**
- Consumes: `TestStateView` (`include/bcos-evm/eth-eest-test/TestStateView.h`, method `get_account(addr) -> std::optional<state::Account>`, `accounts()`), `state::Account` (`balance:bcos::u256, nonce:uint64_t, code:bcos::bytes, storage:StorageMap`), `computeStateRoot(GstPostStateView)` (`GstStateHash.h`), `state::Bytes32Equal` (`eth/state/StateKeyHash.hpp`).
- Produces:
  - `struct ExpectedPostAccount { enum class Kind { Present, Absent }; Kind kind; bool hasNonce,hasBalance,hasCode,hasStorage; uint64_t nonce; bcos::u256 balance; bcos::bytes code; std::vector<std::pair<evmc_bytes32,evmc_bytes32>> storage; };`
  - `struct PostStateExpectation { std::optional<evmc_bytes32> hash; std::vector<std::pair<evmc_address,ExpectedPostAccount>> accounts; };`
  - `struct PostStateFieldDiff { evmc_address address; std::string field; std::optional<evmc_bytes32> slot; std::string got; std::string want; std::string message; };`
  - `struct PostStateAssertReport { bool passed{true}; std::vector<PostStateFieldDiff> diffs; std::string summary; };`
  - `struct AssertOptions { bool eip158ClearEmpty{true}; };`
  - `PostStateAssertReport assertPostState(TestStateView const& actual, PostStateExpectation const& expected, AssertOptions const& opts = {});`

- [ ] **Step 1: Create the header**

Create `test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainPostStateAssert.h`:

```cpp
#pragma once

#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/Account.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

/// One account's expected post-state. Presence flags mirror EEST partial semantics:
/// only fields whose JSON key was authored are asserted.
struct ExpectedPostAccount
{
    enum class Kind
    {
        Present,  // account must exist; check flagged fields
        Absent    // account must NOT exist in actual
    };

    Kind kind{Kind::Present};
    bool hasNonce{false};
    bool hasBalance{false};
    bool hasCode{false};
    bool hasStorage{false};  // `storage` key present (even if empty object)
    uint64_t nonce{0};
    bcos::u256 balance{0};
    bcos::bytes code;
    /// Listed slots in fixture order. Empty + hasStorage ⇒ "all actual slots must be zero".
    std::vector<std::pair<evmc_bytes32, evmc_bytes32>> storage;
};

/// Blockchain fixture post expectation: hash path OR partial account map.
/// Invariant: if `accounts` is non-empty it wins and `hash` is ignored (loader precedence).
struct PostStateExpectation
{
    std::optional<evmc_bytes32> hash;  // postStateHash
    std::vector<std::pair<evmc_address, ExpectedPostAccount>> accounts;
};

struct PostStateFieldDiff
{
    evmc_address address{};
    std::string field;              // "nonce"|"balance"|"code"|"storage"|"existence"|"postStateHash"
    std::optional<evmc_bytes32> slot;
    std::string got;
    std::string want;
    std::string message;            // human-readable one-liner
};

struct PostStateAssertReport
{
    bool passed{true};
    std::vector<PostStateFieldDiff> diffs;
    std::string summary;            // first diff message; empty on pass
};

struct AssertOptions
{
    /// Derived by caller from `genesisRev >= EVMC_SPURIOUS_DRAGON`; affects hash path only.
    bool eip158ClearEmpty{true};
};

/// Assert `actual` canonical-tip state against `expected` per spec §4.4.
PostStateAssertReport assertPostState(TestStateView const& actual,
    PostStateExpectation const& expected, AssertOptions const& opts = {});

}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 2: Write the failing unit test file**

Create `test/eth-eest-test/test/BlockchainPostStateAssertTest.cpp`:

```cpp
#define BOOST_TEST_MODULE BlockchainPostStateAssertTest
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}
evmc_bytes32 word(uint8_t last)
{
    evmc_bytes32 w{};
    w.bytes[31] = last;
    return w;
}
TestStateView viewWith(evmc_address const& a, state::Account acc)
{
    TestStateView v;
    v.insertAccount(a, std::move(acc));
    return v;
}
}  // namespace

BOOST_AUTO_TEST_CASE(empty_expectation_passes)
{
    TestStateView actual = viewWith(addr(1), {});
    PostStateExpectation exp;  // no hash, no accounts
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(hash_match_and_mismatch)
{
    state::Account acc;
    acc.balance = 500;
    acc.nonce = 1;
    TestStateView actual = viewWith(addr(0x10), acc);

    GstPostStateView gv;
    gv.eip158ClearEmpty = true;
    for (auto const& [a, ac] : actual.accounts())
        gv.accounts.emplace_back(a, ac);
    auto root = computeStateRoot(gv);

    PostStateExpectation good;
    good.hash = root;
    BOOST_CHECK(assertPostState(actual, good, {true}).passed);

    PostStateExpectation bad;
    bad.hash = word(0xff);
    auto rep = assertPostState(actual, bad, {true});
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("postStateHash") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(partial_nonce_only_ignores_balance)
{
    state::Account acc;
    acc.nonce = 7;
    acc.balance = 999;  // intentionally not matched
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 7;  // balance NOT flagged
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(balance_mismatch_fails)
{
    state::Account acc;
    acc.balance = 1;
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasBalance = true;
    e.balance = 2;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("balance") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(code_mismatch_fails)
{
    state::Account acc;
    acc.code = bcos::bytes{0x60, 0x80};
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasCode = true;  // expected empty code
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("code") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(listed_storage_missing_slot_is_zero)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(3));
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasStorage = true;
    e.storage.emplace_back(word(1), word(3));  // matches
    e.storage.emplace_back(word(2), word(0));  // missing ⇒ zero ⇒ ok
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(extra_actual_slot_ignored_in_listed_mode)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(3));
    acc.storage.emplace(word(9), word(9));  // not listed ⇒ ignored
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasStorage = true;
    e.storage.emplace_back(word(1), word(3));
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(empty_storage_object_requires_all_zero)
{
    state::Account nonZero;
    nonZero.storage.emplace(word(1), word(5));
    ExpectedPostAccount e;
    e.hasStorage = true;  // storage: {} ⇒ all slots must be zero
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(!assertPostState(viewWith(addr(1), nonZero), exp).passed);

    state::Account zeroed;
    zeroed.storage.emplace(word(1), word(0));  // explicit zero ⇒ ok
    BOOST_CHECK(assertPostState(viewWith(addr(1), zeroed), exp).passed);
}

BOOST_AUTO_TEST_CASE(storage_key_omitted_skips_storage)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(5));  // present but not checked
    ExpectedPostAccount e;
    e.hasStorage = false;  // storage key omitted
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(viewWith(addr(1), acc), exp).passed);
}

BOOST_AUTO_TEST_CASE(missing_account_fails)
{
    TestStateView actual;  // empty
    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 1;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("missing") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(absent_account_pass_and_present_fails)
{
    ExpectedPostAccount e;
    e.kind = ExpectedPostAccount::Kind::Absent;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);

    TestStateView empty;
    BOOST_CHECK(assertPostState(empty, exp).passed);

    auto rep = assertPostState(viewWith(addr(1), {}), exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("absent") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(presence_only_empty_object_requires_existence)
{
    ExpectedPostAccount e;  // Present, no has* flags
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(viewWith(addr(1), {}), exp).passed);
    BOOST_CHECK(!assertPostState(TestStateView{}, exp).passed);
}

BOOST_AUTO_TEST_CASE(accounts_take_precedence_over_hash)
{
    // both set: accounts path must be used, wrong hash ignored
    state::Account acc;
    acc.nonce = 3;
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 3;
    PostStateExpectation exp;
    exp.hash = word(0xff);  // wrong; must be ignored
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 3: Register in CMake**

In `test/eth-eest-test/CMakeLists.txt`, add the source to the core library (inside the `add_library(bcos-evm-specs-tests-core STATIC ...)` list, after `src/BlockchainTestLoader.cpp`):

```cmake
    src/BlockchainTestLoader.cpp
    src/BlockchainPostStateAssert.cpp
)
```

And register the test near the other `add_reference_test(...)` calls (after line ~96 `add_reference_test(BlockchainTestLoaderTest ...)`):

```cmake
add_reference_test(BlockchainPostStateAssertTest test/BlockchainPostStateAssertTest.cpp)
```

- [ ] **Step 4: Run the test to verify it fails (link error / assertions)**

Run:

```bash
cmake --build build-bcos-evm-check --target BlockchainPostStateAssertTest 2>&1 | rtk err
```

Expected: FAIL — undefined reference to `assertPostState` (source not yet implemented).

- [ ] **Step 5: Implement `assertPostState`**

Create `test/eth-eest-test/src/BlockchainPostStateAssert.cpp`:

```cpp
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"

#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth/state/StateKeyHash.hpp"
#include <bcos-utilities/DataConvertUtility.h>
#include <cstring>

namespace bcos::evm::reference_tests
{
namespace
{
bool bytes32Equal(evmc_bytes32 const& a, evmc_bytes32 const& b)
{
    return std::memcmp(a.bytes, b.bytes, 32) == 0;
}

bool isZero(evmc_bytes32 const& v)
{
    for (auto b : v.bytes)
        if (b != 0)
            return false;
    return true;
}

std::string hexAddr(evmc_address const& a)
{
    return "0x" + bcos::toHex(bcos::bytes(a.bytes, a.bytes + sizeof(a.bytes)));
}

std::string hex32(evmc_bytes32 const& v)
{
    return "0x" + bcos::toHex(bcos::bytes(v.bytes, v.bytes + sizeof(v.bytes)));
}

std::string hexBytes(bcos::bytes const& b)
{
    // truncate long code for messages
    if (b.size() > 32)
        return "0x" + bcos::toHex(bcos::bytes(b.begin(), b.begin() + 32)) + "…";
    return "0x" + bcos::toHex(b);
}

evmc_bytes32 storageOf(state::Account const& acc, evmc_bytes32 const& slot)
{
    auto it = acc.storage.find(slot);
    if (it != acc.storage.end())
        return it->second;
    return evmc_bytes32{};
}

evmc_bytes32 computeRootFromView(TestStateView const& view, bool eip158)
{
    GstPostStateView gv;
    gv.eip158ClearEmpty = eip158;
    for (auto const& [addr, acc] : view.accounts())
        gv.accounts.emplace_back(addr, acc);
    return computeStateRoot(gv);
}

void record(PostStateAssertReport& r, PostStateFieldDiff diff)
{
    if (r.summary.empty())
        r.summary = diff.message;
    r.diffs.push_back(std::move(diff));
    r.passed = false;
}
}  // namespace

PostStateAssertReport assertPostState(TestStateView const& actual,
    PostStateExpectation const& expected, AssertOptions const& opts)
{
    PostStateAssertReport report;

    // Precedence: partial account map wins over hash (spec §4.3).
    if (expected.accounts.empty())
    {
        if (expected.hash.has_value())
        {
            auto computed = computeRootFromView(actual, opts.eip158ClearEmpty);
            if (!bytes32Equal(computed, *expected.hash))
            {
                PostStateFieldDiff d;
                d.field = "postStateHash";
                d.got = hex32(computed);
                d.want = hex32(*expected.hash);
                d.message = "postStateHash mismatch got=" + d.got + " want=" + d.want;
                record(report, std::move(d));
            }
        }
        return report;
    }

    for (auto const& [address, exp] : expected.accounts)
    {
        auto got = actual.get_account(address);

        if (exp.kind == ExpectedPostAccount::Kind::Absent)
        {
            if (got.has_value())
            {
                PostStateFieldDiff d;
                d.address = address;
                d.field = "existence";
                d.got = "present";
                d.want = "absent";
                d.message = "postState account should be absent addr=" + hexAddr(address);
                record(report, std::move(d));
            }
            continue;
        }

        if (!got.has_value())
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "existence";
            d.got = "absent";
            d.want = "present";
            d.message = "postState account missing addr=" + hexAddr(address);
            record(report, std::move(d));
            continue;
        }

        auto const& acc = *got;

        if (exp.hasNonce && acc.nonce != exp.nonce)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "nonce";
            d.got = std::to_string(acc.nonce);
            d.want = std::to_string(exp.nonce);
            d.message = "postState nonce mismatch addr=" + hexAddr(address) + " got=" + d.got +
                        " want=" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasBalance && acc.balance != exp.balance)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "balance";
            d.got = bcos::toHex(bcos::toCompactBigEndian(acc.balance));
            d.want = bcos::toHex(bcos::toCompactBigEndian(exp.balance));
            d.message = "postState balance mismatch addr=" + hexAddr(address) + " got=0x" + d.got +
                        " want=0x" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasCode && acc.code != exp.code)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "code";
            d.got = hexBytes(acc.code);
            d.want = hexBytes(exp.code);
            d.message = "postState code mismatch addr=" + hexAddr(address) + " got=" + d.got +
                        " want=" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasStorage)
        {
            if (exp.storage.empty())
            {
                // storage: {} ⇒ every actual slot must be zero.
                for (auto const& [slot, val] : acc.storage)
                {
                    if (!isZero(val))
                    {
                        PostStateFieldDiff d;
                        d.address = address;
                        d.field = "storage";
                        d.slot = slot;
                        d.got = hex32(val);
                        d.want = "0x0";
                        d.message = "postState storage should be empty addr=" + hexAddr(address) +
                                    " slot=" + hex32(slot) + " got=" + d.got;
                        record(report, std::move(d));
                    }
                }
            }
            else
            {
                for (auto const& [slot, want] : exp.storage)
                {
                    auto val = storageOf(acc, slot);
                    if (!bytes32Equal(val, want))
                    {
                        PostStateFieldDiff d;
                        d.address = address;
                        d.field = "storage";
                        d.slot = slot;
                        d.got = hex32(val);
                        d.want = hex32(want);
                        d.message = "postState storage mismatch addr=" + hexAddr(address) +
                                    " slot=" + hex32(slot) + " got=" + d.got + " want=" + d.want;
                        record(report, std::move(d));
                    }
                }
            }
        }
    }

    return report;
}

}  // namespace bcos::evm::reference_tests
```

- [ ] **Step 6: Run unit tests to verify they pass**

Run:

```bash
cmake --build build-bcos-evm-check --target BlockchainPostStateAssertTest 2>&1 | rtk err
ctest -R BlockchainPostStateAssertTest --test-dir build-bcos-evm-check -C Debug --output-on-failure
```

Expected: build clean; all `BlockchainPostStateAssertTest` cases PASS.

- [ ] **Step 7: Commit**

```bash
git add test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainPostStateAssert.h \
        test/eth-eest-test/src/BlockchainPostStateAssert.cpp \
        test/eth-eest-test/test/BlockchainPostStateAssertTest.cpp \
        test/eth-eest-test/CMakeLists.txt
git commit -m "feat(eest): add BlockchainPostStateAssert module with partial postState semantics"
```

---

## Task 2: Add `postExpectation` and wire the runner (additive, non-breaking)

**Files:**
- Modify: `test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h:100-104`
- Modify: `test/eth-eest-test/src/BlockchainTestLoader.cpp:512-532`
- Modify: `test/helpers/BlockchainRunCore.h:108-148` (remove inline), `:383` (call site)
- Modify: `test/eth-eest-test/test/BlockValidationTest.cpp:656-696` (redirect + drop invalid full-hash check ONLY here)

**Interfaces:**
- Consumes: `assertPostState`, `PostStateExpectation`, `ExpectedPostAccount` (Task 1).
- Produces: `BlockchainTest::postExpectation` field (added **alongside** raw `postState`/`postStateHash`); loader populating both; runner calling `assertPostState`.

> **CRITICAL — do not remove the raw fields.** `BlockValidationTest.cpp` has ~6 bespoke probe cases (lines ~279, 382, 392, 454, 764, 951, 1011) that read `picked->postState` / `test.postState` as `state::Account` and call `expected.storage.find(...)`. `ExpectedPostAccount::storage` is an ordered `std::vector` with no `.find`, so removing the raw field breaks compilation across those probes. This task is **additive**: keep raw fields, add `postExpectation`, loader fills both. Converging those probes is optional (Task 4b). Note: `res.postState` / `full.postState` are `BlockApplyResult::postState` (a `TestStateView`) — a different, unaffected field.

- [ ] **Step 1: Add the `postExpectation` field**

In `BlockchainTestTypes.h`, add the include near the top (after existing includes):

```cpp
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
```

Replace lines 100-104:

```cpp
    evmc_bytes32 lastBlockHash{};
    /// postState account map (empty => compare via postStateHash instead).
    std::vector<std::pair<evmc_address, state::Account>> postState;
    std::optional<evmc_bytes32> postStateHash;
};
```

with (raw fields KEPT, new field ADDED):

```cpp
    evmc_bytes32 lastBlockHash{};
    /// Raw parsed post map/hash — retained for legacy BlockValidationTest probes.
    std::vector<std::pair<evmc_address, state::Account>> postState;
    std::optional<evmc_bytes32> postStateHash;
    /// Normative, presence-aware expectation used by the runner (spec §4.3).
    PostStateExpectation postExpectation;
};
```

- [ ] **Step 2: Update the loader to populate raw fields AND `postExpectation`**

In `BlockchainTestLoader.cpp`, replace lines 512-532 (the `if (auto ps = ...) { ... } else if (postStateHash) ...` block). This keeps the raw `bt.postState` / `bt.postStateHash` population and adds `bt.postExpectation` in the same loop:

```cpp
        if (auto ps = t.get_child_optional("postState"))
        {
            for (auto const& [addrStr, accTree] : *ps)
            {
                auto const addr = toAddr(addrStr);

                // Raw state::Account (legacy probes).
                state::Account acc{};
                // Presence-aware expectation (normative path).
                ExpectedPostAccount exp;

                if (auto s = opt(accTree, "nonce"))
                {
                    acc.nonce = toU64(*s);
                    exp.hasNonce = true;
                    exp.nonce = acc.nonce;
                }
                if (auto s = opt(accTree, "balance"))
                {
                    acc.balance = bcos::fromBigQuantity(*s);
                    exp.hasBalance = true;
                    exp.balance = acc.balance;
                }
                if (auto s = opt(accTree, "code"))
                {
                    acc.code = hexToBytes(*s);
                    exp.hasCode = true;
                    exp.code = acc.code;
                }
                if (auto st = accTree.get_child_optional("storage"))
                {
                    exp.hasStorage = true;
                    for (auto const& [k, v] : *st)
                    {
                        auto slot = toBytes32(k);
                        auto val = toBytes32(v.get_value<std::string>());
                        acc.storage[slot] = val;
                        exp.storage.emplace_back(slot, val);
                    }
                }
                // NOTE: boost::property_tree cannot distinguish JSON null from {} (Task 0);
                // absent accounts are not derivable from the corpus here and stay Present.
                bt.postState.emplace_back(addr, std::move(acc));
                bt.postExpectation.accounts.emplace_back(addr, std::move(exp));
            }
        }
        else if (auto s = t.get_optional<std::string>("postStateHash"))
        {
            bt.postStateHash = toBytes32(*s);
            bt.postExpectation.hash = bt.postStateHash;
        }
```

- [ ] **Step 3: Replace the inline `expectPostStateMatches` in `BlockchainRunCore.h`**

Delete the entire inline function (lines 108-148, from `inline std::optional<std::string> expectPostStateMatches(` through its closing `}`). Add the include near the top of the file (with the other `bcos-evm/eth-eest-test/*` includes):

```cpp
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
```

- [ ] **Step 4: Update the runner call site**

In `BlockchainRunCore.h` `runOneTest`, replace the call around line 383:

```cpp
    if (auto err = expectPostStateMatches(*canonicalState, test))
        return *err;
```

with:

```cpp
    AssertOptions const postOpts{.eip158ClearEmpty = (genesisRev >= EVMC_SPURIOUS_DRAGON)};
    if (auto report = assertPostState(*canonicalState, test.postExpectation, postOpts);
        !report.passed)
        return report.summary;
```

(`genesisRev` is already in scope from `resolveRevision` earlier in `runOneTest`.)

- [ ] **Step 5: Redirect the `BlockValidationTest.cpp` call site and drop the invalid full-hash self-check**

Replace lines 656-696 (from `if (auto postErr = detail::expectPostStateMatches(...)` through the closing `}` of the `for (auto const& [expAddr, expAcc] : picked->postState)` loop) with:

```cpp
    AssertOptions const postOpts{
        .eip158ClearEmpty = (profile->revision.revision >= EVMC_SPURIOUS_DRAGON)};
    if (auto report = assertPostState(res.postState, picked->postExpectation, postOpts);
        !report.passed)
        BOOST_FAIL("postState mismatch: " + report.summary);
```

Add the include near the top of `BlockValidationTest.cpp` (with the other `bcos-evm/eth-eest-test/*` includes):

```cpp
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
```

> The removed block (fixture-hash self-consistency + manual per-account loops at 659-696) relied on this specific fixture listing *full* accounts; that is invalid under partial semantics (spec §2.3, §6.2). The per-block header `stateRoot` check at lines 698-716 stays and remains authoritative.
>
> **Do NOT touch the other probe cases** (~279, 382, 392, 454, 764, 951, 1011). They keep reading raw `picked->postState` / `test.postState` and compile unchanged because the raw field is retained (Step 1). Converging them is optional Task 4b.

- [ ] **Step 6: Build the affected targets to verify compilation**

Run:

```bash
cmake --build build-bcos-evm-check --target BlockValidationTest EthEestBlockchainRunner EthEestBlockGranular 2>&1 | rtk err
```

Expected: clean build. `expectPostStateMatches` is gone; the raw `.postState`/`.postStateHash` references in the untouched probe cases still resolve (fields retained).

- [ ] **Step 7: Run the focused blockchain tests**

Run:

```bash
ctest -R 'BlockValidationTest|EthEestBlockchainSmoke|BlockchainTestLoaderTest' \
  --test-dir build-bcos-evm-check -C Debug --output-on-failure
```

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h \
        test/eth-eest-test/src/BlockchainTestLoader.cpp \
        test/helpers/BlockchainRunCore.h \
        test/eth-eest-test/test/BlockValidationTest.cpp
git commit -m "feat(eest): wire blockchain runner to shared assertPostState (partial semantics)"
```

---

## Task 3: Full regression + documentation updates

**Files:**
- Modify: `test/eth-eest-test/eest-integration-matrix.md` (gap #3, P1 checklist)
- Modify: `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md:597-602` (§8.1 pseudocode)

**Interfaces:**
- Consumes: everything from Tasks 1-2.
- Produces: green full blockchain sweep + updated docs.

- [ ] **Step 1: Run the full blockchain regression (static tree included)**

Run:

```bash
ctest -R 'EthEestBlockchain(Smoke|Full)' --test-dir build-bcos-evm-check -C Debug --output-on-failure
```

Expected: `EthEestBlockchainSmoke` and `EthEestBlockchainFull` PASS with the same case counts as the matrix baseline (`40855/40855` static). If any new failure appears, it is a real behavior regression — stop and diagnose before proceeding.

- [ ] **Step 2: Run the granular gtest gate**

Run:

```bash
ctest -R 'EthEestBlockGranular(Smoke|Full)' --test-dir build-bcos-evm-check -C Debug --output-on-failure
```

Expected: PASS (no regression from the type migration).

- [ ] **Step 3: Update the matrix**

In `test/eth-eest-test/eest-integration-matrix.md`:

- In "Known blockchain runner gaps", change item 3 from:

```
3. `postState` full account diff marked "Phase 2".
```

to:

```
3. `postState` full account diff — **DONE (2026-07-08)**: `assertPostState` compares nonce/balance/code/listed-storage/empty-storage/absent per EEST partial semantics; extra accounts/slots ignored.
```

- In the P1 checklist, change:

```
- [ ] Full `postState` account diff
```

to:

```
- [x] Full `postState` account diff — partial-semantics field diff via `BlockchainPostStateAssert` (`assertPostState`)
```

- [ ] **Step 4: Fix the parent spec §8.1 pseudocode**

In `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md`, replace the comment block at lines 599-602:

```cpp
    // Expectation.post_state 为 variant<TestStateView, h256>：
    //   - 持 h256：直接比 computeStateRoot(*canonical_state) == 该 hash
    //   - 持 TestStateView：比 computeStateRoot(canonical) == computeStateRoot(expectedView)
    expectPostStateMatches(*canonical_state, test.expectation.post_state);
```

with:

```cpp
    // postState 比对（见 2026-07-08 postState diff spec §4.4）：
    //   - postStateHash：computeStateRoot(*canonical_state) == hash
    //   - postState 账户表：逐账户 **部分字段 diff**（非 root-of-view 相等），
    //     仅比 JSON 出现的 nonce/balance/code/storage；未列账户/槽位忽略。
    assertPostState(*canonical_state, test.postExpectation, {eip158ClearEmpty});
```

- [ ] **Step 5: Commit docs**

```bash
git add test/eth-eest-test/eest-integration-matrix.md \
        docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md
git commit -m "docs(eest): mark postState account diff done; fix parent spec §8.1 pseudocode"
```

---

## Task 4b (optional): Converge remaining BlockValidationTest probes

**Files:** `test/eth-eest-test/test/BlockValidationTest.cpp`

Only if desired — not required for Phase 2 completion. The ~6 bespoke probe cases (~279, 382, 392, 454, 764, 951, 1011) read raw `picked->postState` / `test.postState` (with `storage.find()`). Converging them onto `assertPostState` (or `ExpectedPostAccount`) is what would eventually let the raw `postState` / `postStateHash` fields be removed from `BlockchainTest`. These probes carry stricter, intentional per-probe checks (e.g. "history slot 1 absent after block 1") that are NOT plain partial-semantics; each conversion must preserve that intent.

- [ ] **Step 1: Enumerate raw-field probe sites** — grep `->postState\b|\.postState\b|\.postStateHash\b` in `BlockValidationTest.cpp`, excluding `res.postState` / `full.postState` (those are `BlockApplyResult`).
- [ ] **Step 2: For each site**, either replace with `assertPostState` where intent matches §2.2 partial semantics, or keep the bespoke check with a comment explaining why it is stricter.
- [ ] **Step 3: If ALL raw-field sites are converted**, remove the raw `postState`/`postStateHash` fields from `BlockchainTestTypes.h` and their population from the loader; otherwise leave them.
- [ ] **Step 4: Run** `ctest -R BlockValidationTest --test-dir build-bcos-evm-check -C Debug --output-on-failure` → PASS.
- [ ] **Step 5: Commit** `refactor(eest): converge BlockValidationTest probes onto assertPostState`.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|--------------|------|
| §2.2 partial field rules (nonce/balance/code/storage) | Task 1 Step 5; unit tests Step 2 |
| §2.2 `storage: {}` all-zero vs omitted | Task 1 (`empty_storage_object_requires_all_zero`, `storage_key_omitted_skips_storage`) |
| §4.2 types (`ExpectedPostAccount` incl. `hasStorage`, `PostStateExpectation`) | Task 1 Step 1 |
| §4.3 loader precedence + presence flags | Task 2 Step 2 |
| §4.3 Impl Task 0 null probe | Task 0 |
| §4.4 algorithm (incl. absent, empty-storage) | Task 1 Step 5 |
| §4.5 runner integration + `eip158ClearEmpty` from `genesisRev` | Task 2 Steps 3-4 |
| §4.6 error message format | Task 1 Step 5 (messages) |
| §6.1 unit test matrix | Task 1 Step 2 |
| §6.2 minimal BlockValidationTest redirect | Task 2 Step 5 |
| §6.3 regression commands + corpus audit | Task 0, Task 3 Steps 1-2 |
| §8 matrix + parent-spec §8.1 fix | Task 3 Steps 3-4 |
| §10 Task 4b optional | Task 4b |

**Placeholder scan:** No TBD/TODO; every code step shows full code; the one runtime unknown (property_tree null) is resolved by the Task 0 probe and handled with an explicit loader fallback.

**Type consistency:** `ExpectedPostAccount::balance` is `bcos::u256` everywhere; storage is `std::vector<std::pair<evmc_bytes32,evmc_bytes32>>` (ordered) in both header and loader; `assertPostState` signature identical in header, cpp, and all call sites; `AssertOptions{.eip158ClearEmpty=...}` used consistently.

**Blast-radius check (plan re-check, 2026-07-08):** Verified via grep that `BlockchainTest::postState`/`postStateHash` are read by ~7 sites in `BlockValidationTest.cpp` (several using `storage.find()`), distinct from the same-named `BlockApplyResult::postState` (a `TestStateView`). Task 2 is therefore **additive** (raw fields retained); only the single `expectPostStateMatches` call site is redirected. This keeps the migration non-breaking and within the 1–2 day estimate.

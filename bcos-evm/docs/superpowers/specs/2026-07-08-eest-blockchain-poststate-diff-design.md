# EEST Blockchain postState Full Account Diff — Design Spec

**Date:** 2026-07-08  
**Status:** Draft — brainstorming output, pending user review  
**Scope:** Phase 2 harness enhancement — complete `postState` / `postStateHash` assertion in the EEST blockchain test runner  
**Architecture choice:** Approach B — extract `BlockchainPostStateAssert` module; optional-field `ExpectedPostAccount` model

**Related specs / docs:**

- `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md` — runner loop, `expectPostStateMatches` hook point (§8.1)
- `docs/superpowers/specs/2026-07-06-evm-execution-trace-design.md` — future `StateDiffReport` consumer (H8); not a blocker for this work
- `bcos-evm/test/eth-eest-test/eest-integration-matrix.md` — Known gap #3, P1 checklist item
- `bcos-evm/test/helpers/BlockchainRunCore.h` — current partial `expectPostStateMatches`
- `bcos-evm/test/eth-eest-test/test/BlockValidationTest.cpp` — existing ad-hoc code/storage assertions (partly **stricter** than target; see Appendix A)

**Frozen decisions (brainstorming):**

| Item | Choice |
|------|--------|
| Semantics | **EEST partial postState** — only listed addresses; only JSON-present fields are compared |
| `postStateHash` | **Hash-only** path unchanged (no field diff) |
| `postState` + `postStateHash` both present | **`postState` field diff takes precedence**; hash ignored (loader records both, assert prefers accounts) |
| Empty `postState` | **Skip** final account diff; per-block `stateRoot` header checks remain authoritative |
| `storage` key semantics | Omitted ⇒ no storage check; `{}` ⇒ **all** actual slots must be zero; listed ⇒ only listed slots (missing ⇒ zero) |
| Module boundary | **New** `BlockchainPostStateAssert.{h,cpp}` under `test/eth-eest-test/` |
| Error shape | Structured `PostStateAssertReport` (`PostStateFieldDiff` list) with address / field / got / want; string summary for gtest |
| NONEXISTENT accounts | **In scope** if present in loaded corpus; loader extended with `Kind::Absent` marker (JSON `null` / sentinel — probed in impl Task 0) |
| Full-state exhaustion | **Out of scope** — do not fail on unexpected extra accounts |
| `eip158ClearEmpty` (hash path) | Derived from `genesisRev >= EVMC_SPURIOUS_DRAGON`; no new `ForkProfile` field |
| EVM runtime | **No changes** — harness-only |

---

## 1. Problem Statement

### 1.1 Current state

Blockchain runner M1–M6 is **100% green** on the current EEST corpus, primarily via **per-block header checks** (`stateRoot`, `receiptsRoot`, `logsBloom`, `gasUsed`, …) and **`postStateHash`** when provided.

Final canonical-state validation calls `expectPostStateMatches` in `BlockchainRunCore.h`, but the implementation is **incomplete**:

| Check | Current | Required |
|-------|---------|----------|
| `postStateHash` | ✅ root compare | unchanged |
| Account presence (listed addr) | ✅ missing → fail | unchanged |
| `nonce` | ✅ always compared | compare **only if present in fixture JSON** |
| `balance` | ✅ always compared | compare **only if present in fixture JSON** |
| `code` | ❌ not compared | ✅ when `code` key present |
| `storage` (listed slots) | ✅ one direction | ✅ fixture → actual for **listed slots only** (missing ⇒ zero) |
| `storage: {}` (empty object) | ❌ not distinguished | ✅ **all** actual slots must be zero |
| Extra storage slots in actual (listed mode) | ❌ ignored | ❌ still ignored (partial semantics) |
| Extra accounts in actual | ❌ ignored | ❌ still ignored (partial semantics) |
| Account must be absent | ❌ not supported | ✅ when fixture marks absent |
| Error message | `"postState nonce mismatch"` | address + field + got/want hex |
| Field-presence tracking | ❌ loader fills defaults | ✅ `ExpectedPostAccount` with presence flags (incl. `hasStorage`) |

`BlockchainTestLoader` parses `postState` account trees (including `code`) into plain `state::Account`, **losing** which JSON keys were authored. That makes it impossible to implement correct EEST partial semantics with the current type.

`BlockValidationTest.cpp` contains ad-hoc code + storage checks for Prague/Osaka fixtures, but that logic is duplicated per-test and **not** normative: e.g. `eip7685_multi_type_requests_header_fields` scans *actual* storage slots against the fixture (want=0 for unlisted slots), which is **stricter** than the target partial semantics (§2.2). The extraction here defines the single normative algorithm (§4.4); the ad-hoc scans must not be copied verbatim.

### 1.2 Why now (despite M6 green)

1. **evmone / geth alignment** — clients that consume `postState` verify author-specified account fields; geth added storage verification ([PR #28443](https://github.com/ethereum/go-ethereum/pull/28443)).
2. **Diagnostic value** — when `stateRoot` fails, engineers manually diff; when `postState` is present, runner should pinpoint **address + field**.
3. **Fixture self-consistency** — postState sections are manually authored; comparing them catches fixture drift (rare but cheap).
4. **Complements H8 trace** — trace explains execution path; postState diff states **what** diverged in state.

### 1.3 Goals

1. When a blockchain fixture provides **`postState`**, assert all specified account fields against canonical tip state after `lastBlockHash` check.
2. When a fixture provides **`postStateHash`**, keep hash-only assertion (no regression).
3. Emit **actionable** failure messages (address, field, slot, got, want).
4. **Zero regressions** on M1–M6 / static blockchain corpora (`40855/40855` cases). **Note:** moving from "always compare nonce/balance" to "compare only JSON-present fields" is strictly *looser*; implementation must first audit the corpus for accounts that omit `nonce`/`balance`/`code` (see §6.3 audit) so any behavior change is recorded, not silent.
5. Unit tests cover partial-field semantics, code, listed-slot storage, empty-object storage, absent accounts.

### 1.4 Out of scope

- Changing EVM execution, state transition, or MPT hashing
- Exhaustive full-state diff (all accounts in trie)
- Statetest runner changes (`StateTestAssert` remains root/logsHash-focused)
- OpStack blockchain runner (separate track; may reuse module later)
- `StateDiffReport` / execution trace integration (H8 Phase 0 — optional follow-up to reuse formatter)
- PoW reorg / M5 canonical tip semantics changes

---

## 2. EEST Semantics Reference

### 2.1 Blockchain fixture post expectation

Per EEST / legacy GST blockchain format, top-level expectation is **one of**:

| Fixture field | Meaning |
|---------------|---------|
| *(none)* | Rely on last block header `stateRoot` only |
| `postStateHash` | Canonical tip state root must equal hash |
| `postState` | **Partial** account map — manual assertions |

`postState` is **not exhaustive**. It lists accounts and fields the **test author** wants verified. Omitted account fields mean **do not check that field**. Omitted accounts mean **do not check that address**.

This differs from statetest `post[<fork>][].hash`, which is the authoritative full-state root for that transition.

### 2.2 Account field rules

For each `(address, expected)` in `postState`:

| Field in JSON | Assertion |
|---------------|-----------|
| `nonce` | present ⇒ `actual.nonce == expected.nonce`; omitted ⇒ not checked |
| `balance` | present ⇒ `actual.balance == expected.balance`; omitted ⇒ not checked |
| `code` | present ⇒ `actual.code == expected.code` (byte-equal); omitted ⇒ not checked |
| `storage` **key omitted** | Storage **not** checked at all |
| `storage: {}` (empty object) | **All** slots in `actual.storage` for this account must be zero (any non-zero ⇒ fail) |
| `storage: { slot: want, … }` | For each listed `(slot, want)`: `actual.storage[slot] == want` (missing slot ⇒ zero); actual slots **not** listed are ignored |
| *(whole object empty `{}`)* | Account must **exist**; no field checks (presence-only) |
| JSON `null` / EEST absent sentinel | Account must **not** exist in actual (see §5.2) |

**Not asserted:**

- Accounts present in actual but not listed in `postState`
- Storage slots present in actual but not listed under that account (only in `storage: { … }` listed mode; **not** in `storage: {}` mode)
- `codeHash` directly (derived from `code` when `code` key present)

> **Note on empty vs omitted `storage`:** EEST `Account` treats an explicitly authored `storage: {}` as "this account holds no storage", which is *stronger* than omitting the key. The `hasStorage` presence flag (§4.2) is what disambiguates the two.

### 2.3 Relationship to per-block `stateRoot`

Per-block header `stateRoot` checks remain **authoritative** for consensus parity. Final `postState` diff is an **additional** layer when the fixture includes it. Both can pass independently; if header roots pass but `postState` fails, that indicates **fixture inconsistency** or a loader bug — still a runner FAIL.

---

## 3. Approaches Considered

### Approach A — Inline expand `BlockchainRunCore.h::expectPostStateMatches`

Add code comparison, presence flags, and formatting directly in the existing header inline function.

**Pros:** Smallest diff; no new files.  
**Cons:** `BlockchainRunCore.h` already large; mixes orchestration with assertion logic; hard to unit test without pulling entire runner.

### Approach B — Extract `BlockchainPostStateAssert` module (chosen)

New `{h,cpp}` pair under `test/eth-eest-test/` with:

- `ExpectedPostAccount` (+ presence flags)
- `assertPostState(expected, actual) -> PostStateAssertReport`
- Loader produces `ExpectedPostAccount` instead of bare `state::Account`

**Pros:** Testable in isolation; mirrors `StateTestAssert` pattern; clear boundary for future H8 `StateDiffReport` adapter.  
**Cons:** Loader type change touches `BlockchainTestTypes.h`.

### Approach C — Block on H8 `StateDiffReport`

Wait for execution-trace Phase 0 `StateDiffReport` and implement diff only through that facility.

**Pros:** Single diff formatter for statetest + blockchain.  
**Cons:** Delays P1 harness gap; trace work is larger and unrelated to EVM semantics; over-couples simple assert to trace collector.

**Decision: Approach B.** Ship focused assert module now; optionally route report formatting through H8 later.

---

## 4. Architecture

### 4.1 Component diagram

```text
BlockchainTestLoader
        │ parse postState / postStateHash / null(absent)
        ▼
BlockchainTest { postExpectation: PostStateExpectation }
        │
BlockchainRunCore::runOneTest()
        │ per-block header asserts (unchanged)
        │ canonical tip == lastBlockHash
        ▼
BlockchainPostStateAssert::assertPostState(canonicalView, postExpectation)
        │
        ├─ PostStateExpectation::Hash  → computeStateRootFromView vs hash
        └─ PostStateExpectation::Accounts → partial field diff
                │
                ▼
        PostStateAssertReport { passed, diffs[], summary }
                │
                ▼
        gtest FAIL / scan-eest-failures message
```

### 4.2 New types

**File:** `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainPostStateAssert.h`

```cpp
struct ExpectedPostAccount
{
    enum class Kind { Present, Absent };

    Kind kind{Kind::Present};
    bool hasNonce{false};
    bool hasBalance{false};
    bool hasCode{false};
    bool hasStorage{false};   // `storage` key present in JSON (even if {})
    uint64_t nonce{};
    intx::uint256 balance{};
    bcos::bytes code;
    // listed slots (empty + hasStorage ⇒ "all actual slots must be zero")
    std::map<evmc_bytes32, evmc_bytes32, Bytes32Less> storage;
};

struct PostStateFieldDiff
{
    evmc_address address{};
    std::string field;       // "nonce" | "balance" | "code" | "storage"
    std::optional<evmc_bytes32> slot;
    std::string got;
    std::string want;
    std::string message;     // human-readable one-liner
};

struct PostStateAssertReport
{
    bool passed{true};
    std::vector<PostStateFieldDiff> diffs;
    std::string summary;     // first diff or empty
};

struct PostStateExpectation
{
    // Invariant: at most one path is used. If `accounts` is non-empty it takes
    // precedence and `hash` is ignored (§4.3 precedence rule). Both empty ⇒ no-op.
    std::optional<evmc_bytes32> hash;  // postStateHash path
    std::vector<std::pair<evmc_address, ExpectedPostAccount>> accounts;
};

PostStateAssertReport assertPostState(
    TestStateView const& actual,
    PostStateExpectation const& expected,
    AssertOptions const& opts = {});

struct AssertOptions
{
    // derived by caller from genesisRev >= EVMC_SPURIOUS_DRAGON; hash path only
    bool eip158ClearEmpty{true};
};
```

**File:** `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/BlockchainTestTypes.h`

**Add** `postExpectation` **alongside** the existing raw fields (do **not** remove them):

```cpp
std::vector<std::pair<evmc_address, state::Account>> postState;   // raw parsed (legacy probes)
std::optional<evmc_bytes32> postStateHash;                         // raw parsed (legacy probes)
PostStateExpectation postExpectation;                             // normative, presence-aware
```

**Why keep the raw fields (critical, discovered during plan re-check):** `BlockValidationTest.cpp` has ~6 bespoke probe cases (lines ~279, 382, 392, 454, 764, 951, 1011) that read the expected accounts as `state::Account` and call `expected.storage.find(...)` — a `std::unordered_map` API. `ExpectedPostAccount` intentionally uses an **ordered `std::vector`** (for deterministic diffs), which has no `.find`, so removing/renaming the raw field would break compilation across those probes. Their conversion carries subtle per-probe semantics and is explicitly **optional** (§10 Task 4b). The loader therefore populates **both** the raw fields (for legacy probes) and `postExpectation` (for the normative runner path) in one parse pass. Raw fields may be dropped later once all probes converge.

> Note: `res.postState` / `full.postState` in these tests are `BlockApplyResult::postState` (a `TestStateView`) — a **different** field that shares the name; it is unaffected by this change.

### 4.3 Loader changes

**File:** `bcos-evm/test/eth-eest-test/src/BlockchainTestLoader.cpp`

The loader fills **both** the raw fields (legacy probes) and `postExpectation` (normative) in one pass.

Parsing rules:

1. `postStateHash` string → raw `postStateHash` **and** `postExpectation.hash`
2. `postState` object, per account: build the raw `state::Account` (as today) **and** an `ExpectedPostAccount`:
   - key → address (both)
   - JSON `null` (or discovered absent sentinel) value → `ExpectedPostAccount{ Kind::Absent }` (raw entry may be default `state::Account`)
   - object with keys:
     - set `hasNonce` if `nonce` key present
     - set `hasBalance` if `balance` key present
     - set `hasCode` if `code` key present
     - set `hasStorage` if `storage` key present (`get_child_optional("storage").has_value()`); parse sub-object into the ordered `storage` vector (may be empty)
     - whole object empty `{}` → `Kind::Present`, all `has*` false (presence-only)
3. **Precedence:** if both `postState` (non-empty) and `postStateHash` appear, `postExpectation.accounts` wins and `postExpectation.hash` is ignored (optionally emit a loader warning). Only when `postState` is absent/empty is `hash` used.

**Impl Task 0 — `null` parsing probe (blocking):** `boost::property_tree::read_json` does **not** preserve a distinct JSON `null`; a `null` value typically yields an empty child node, which is indistinguishable from `{}` (presence-only) under naive parsing. Before implementing `Kind::Absent`, verify the actual `read_json` behavior with (a) a synthetic fixture containing `"0x…": null` and (b) a real corpus grep. Document the concrete disambiguation strategy in the implementation plan — e.g. detect a raw-string `null` token, or switch this subtree to a `null`-preserving parse. Until then, `Kind::Absent` is exercised only by synthetic unit tests that construct `ExpectedPostAccount` directly.

**Corpus probe (implementation task):** scan the full `blockchain_tests` tree (incl. `static/`) for `postState` account entries that are `null` / omit `nonce`/`balance`/`code`, and count `postState` vs `postStateHash` fixtures (see §6.3). Record findings before coding the loose-field semantics.

### 4.4 Assert algorithm

**File:** `bcos-evm/test/eth-eest-test/src/BlockchainPostStateAssert.cpp`

```
function assertPostState(actual, expected):
  if expected.hash:
    computed = computeStateRootFromView(actual, opts.eip158ClearEmpty)
    if computed != expected.hash:
      return fail("postStateHash mismatch", got=computed, want=hash)
    return pass

  if expected.accounts empty:
    return pass

  build actualMap from actual.accounts()

  for (addr, exp) in expected.accounts:
    if exp.kind == Absent:
      if actualMap contains addr:
        return fail(address, field="existence", got="present", want="absent")
      continue

    it = actualMap.find(addr)
    if it == end:
      return fail(address, field="existence", got="absent", want="present")

    got = it->second
    if exp.hasNonce and got.nonce != exp.nonce:
      record diff nonce
    if exp.hasBalance and got.balance != exp.balance:
      record diff balance
    if exp.hasCode and got.code != exp.code:
      record diff code (truncate hex in message if > 256 bytes)
    if exp.hasStorage:
      if exp.storage empty:                 // storage: {}  ⇒ all-zero requirement
        for (slot, val) in got.storage:
          if val != zero:
            record diff storage slot (want=0)
      else:                                 // storage: {…} ⇒ listed slots only
        for (slot, want) in exp.storage:
          gotSlot = got.storage[slot] or zero
          if gotSlot != want:
            record diff storage slot
        // actual slots not listed are intentionally NOT checked (partial semantics)

  if any diffs:
    return fail with first diff as summary + full diff list in report
  return pass
```

**First-failure vs collect-all:** Collect all diffs internally; **summary** uses the first diff for gtest brevity. Stable order = iteration over `expected.accounts` in **loader-insertion order** (loader preserves fixture order), and within an account: nonce → balance → code → storage-by-slot (`memcmp` on the 32-byte slot key). Env `EEST_POSTSTATE_DIFF_VERBOSE=1` printing all diffs is **optional / post-MVP** and may be dropped.

### 4.5 Runner integration

**File:** `bcos-evm/test/helpers/BlockchainRunCore.h`

- Remove inline `expectPostStateMatches`
- After `lastBlockHash` check:

```cpp
// genesisRev already resolved earlier in runOneTest via resolveRevision(...)
AssertOptions opts{ .eip158ClearEmpty = (genesisRev >= EVMC_SPURIOUS_DRAGON) };
auto report = assertPostState(*canonicalState, test.postExpectation, opts);
if (!report.passed)
    return report.summary;
```

- `eip158ClearEmpty` is **derived from `genesisRev`** (already available in `runOneTest`), not stored on `ForkProfile`. It only affects the `postStateHash` root path; the field-diff path reads `TestStateView` contents as-is.

**File:** `bcos-evm/test/eth-eest-test/runners/eth/EthEestBlockchainRunner.cpp`

No logic change expected if failure propagation stays string-based.

### 4.6 Error message format

Single-line gtest default:

```
postState balance mismatch addr=0xabc… got=0x1 want=0x2
postState storage mismatch addr=0xabc… slot=0x01 got=0x00 want=0x03
postState storage should be empty addr=0xabc… slot=0x01 got=0x05 want=0x0   (storage: {} mode)
postState code mismatch addr=0xabc… got=0x6080… want=0x
postState account should be absent addr=0xabc…
postState account missing addr=0xabc…
postStateHash mismatch got=0x… want=0x…
```

Structured `PostStateAssertReport` retained for future H8 adapter:

```cpp
// Future: StateDiffReport.fromPostStateAssert(report)
```

---

## 5. Edge Cases

### 5.1 EIP-158 empty accounts

Partial field checks operate on **`TestStateView` contents** as the runner holds them after block execution (post-clearing). Do **not** re-apply EIP-158 clearing per account during field compare unless comparing via hash path.

Hash path uses `computeStateRootFromView` → `GstPostStateView` with `eip158ClearEmpty = true` (existing behavior).

### 5.2 Absent account marker

Target: address key mapped to JSON `null` in `postState` ⇒ `Kind::Absent`.

**Blocked on Impl Task 0 (§4.3):** `boost::property_tree` does not natively distinguish `null` from `{}`. The loader parse strategy for `null` must be verified before this path is trusted against the real corpus. If EEST serializes absent accounts with a string sentinel (e.g. `"NONEXISTENT"`) rather than `null`, adjust the loader after the corpus grep and record the concrete encoding in the implementation plan. Until confirmed, `Kind::Absent` behavior is proven only via synthetic unit tests.

### 5.3 Coinbase / system-account touches

No special case — if fixture lists coinbase with fields, assert them like any account.

### 5.4 Interaction with `postState` + populated last block header

When both last block `stateRoot` (already checked per block) and `postState` exist:

- Both must pass.
- Mismatch between fixture `postState` fields and header `stateRoot` in **fixture JSON** is an upstream fixture bug; runner still reports field diff against **actual execution**, not fixture internal consistency.

---

## 6. Testing Strategy

### 6.1 Unit tests — `BlockchainPostStateAssertTest.cpp`

| Case | Verifies |
|------|----------|
| `postStateHash_match` / `_mismatch` | Hash path |
| `empty_expectation_skips` | No-op |
| `partial_nonce_only` | Balance/code ignored when not in expected |
| `code_mismatch` | Code bytes |
| `storage_listed_slots` | Listed slots; missing slot ⇒ zero |
| `storage_empty_object_all_zero_pass` / `_nonzero_fails` | `storage: {}` ⇒ every actual slot must be zero |
| `storage_key_omitted_skips` | No `storage` key ⇒ storage not checked |
| `extra_storage_in_actual_ignored_listed_mode` | Partial semantics (listed mode only) |
| `extra_account_in_actual_ignored` | Partial semantics |
| `missing_account_fails` | existence |
| `absent_account_pass` / `present_fails` | Absent kind |
| `presence_only_empty_object` | `{}` expects account exists |
| `first_diff_stable_order` | Deterministic summary |

### 6.2 Integration — `BlockValidationTest.cpp` (minimal)

Scope is deliberately narrow: redirect the **single** `detail::expectPostStateMatches` call site (in `eip7685_multi_type_requests_header_fields`) to `assertPostState`. The other long Prague/Osaka probe cases keep their existing bespoke checks and are **not** rewritten as part of Phase 2 (their stricter actual-slot scans are out of the normative path; converging them is optional follow-up, see §10 Task 4b). All probes stay behind `SPECS_TESTS_EEST_ROOT`.

### 6.3 Regression gates

After implementation, re-run (no new failures allowed). CTest names verified against `CMakeLists.txt`: the blockchain gates are `EthEestBlockchainSmoke` / `EthEestBlockchainFull` (there is **no** `EthEestBlockchainStatic`; the `blockchain_tests/static/` tree is covered by `EthEestBlockchainFull --fixtures .../blockchain_tests`). Use the matrix-standard build dir `build-bcos-evm-check`:

```bash
ctest -R 'EthEestBlockchain(Smoke|Full)' --test-dir build-bcos-evm-check -C Debug --output-on-failure

# Explicit static sweep (whole tree incl. static/):
./build-bcos-evm-check/.../EthEestBlockchainRunner \
  --fixtures "$EVM_REF_EEST_ROOT/fixtures/blockchain_tests"

# Optional granular gtest gate:
ctest -R 'EthEestBlockGranular(Smoke|Full)' --test-dir build-bcos-evm-check -C Debug --output-on-failure
```

**Corpus audit (run before coding loose-field semantics, §1.3 goal 4):**

```bash
# postState object vs postStateHash-only fixtures
rg -l '"postState"'     "$EVM_REF_EEST_ROOT/fixtures/blockchain_tests" | wc -l
rg -l '"postStateHash"' "$EVM_REF_EEST_ROOT/fixtures/blockchain_tests" | wc -l

# fixtures whose postState accounts use null (absent) — validates Impl Task 0
rg -l '"postState"\s*:\s*\{[^}]*: *null' "$EVM_REF_EEST_ROOT/fixtures/blockchain_tests" | wc -l

# spot-check whether any postState account omits nonce/balance (loose-field impact)
```

### 6.4 Negative test

Synthetic `BlockchainTest` injected in unit test with intentionally wrong `code` in `postExpectation` must fail with `code mismatch` message before merging.

---

## 7. Build / CMake

Add to `bcos-evm/test/eth-eest-test/CMakeLists.txt`:

- `src/BlockchainPostStateAssert.cpp` → linked into existing EEST test targets using `BlockchainRunCore.h`
- `test/BlockchainPostStateAssertTest.cpp` → `EthEestBlockchainPostStateAssert` gtest target

No new third-party dependencies.

---

## 8. Documentation / Matrix Updates

After merge:

1. `eest-integration-matrix.md` — mark gap #3 and P1 checklist **done**; note partial semantics.
2. `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md` — add cross-link **and** revise the §8.1 `expectPostStateMatches` pseudocode in the parent spec body. Its current text implies `computeStateRoot(canonical) == computeStateRoot(expectedView)` for the `TestStateView` case; that is wrong for EEST partial `postState`. Replace with: "hash → root compare; account map → **partial field diff** per §4.4 of the 2026-07-08 spec (not root-of-view equality)." This prevents a future implementer from following the stale parent pseudocode.

---

## 9. Success Criteria

| Criterion | Measurement |
|-----------|-------------|
| Semantics | Unit tests prove partial-field behavior |
| Parity | M6 static `40855/40855` unchanged |
| Coverage | All JSON-present fields (`nonce`, `balance`, `code`, `storage`) asserted |
| Diagnostics | Failures include address + field + got/want |
| Isolation | No changes under `bcos-evm/eth/` |
| evmone gap closed | Matrix item "Full postState account diff" checked |

---

## 10. Implementation Plan Handoff

After spec approval, invoke **writing-plans** skill to produce:

- Task 0: Corpus + `null` parsing probe (§4.3 / §6.3) — blocking, informs loader design
- Task 1: Types + loader (`ExpectedPostAccount` incl. `hasStorage`, `PostStateExpectation`, precedence)
- Task 2: `BlockchainPostStateAssert.cpp` algorithm (incl. empty-storage all-zero, absent)
- Task 3: Wire `BlockchainRunCore.h` (derive `eip158ClearEmpty` from `genesisRev`), remove old inline function
- Task 4a: Unit tests (`BlockchainPostStateAssertTest.cpp`) + redirect the single `expectPostStateMatches` call site in `BlockValidationTest.cpp`
- Task 4b (optional): converge remaining ad-hoc BlockValidationTest probes onto `assertPostState`
- Task 5: Full blockchain regression + parent-spec §8.1 fix + matrix update

Estimated effort: **1–2 days** for Task 0–5 (harness-only, no EVM work); Task 4b is a separate optional cleanup that can slip.

---

## 11. Spec Self-Review (2026-07-08)

| Check | Result |
|-------|--------|
| Placeholder scan | No TBD sections; `null`/absent parsing + loose-field impact flagged as blocking Impl Task 0 |
| Internal consistency | Hash path vs partial-field path precedence explicit (§4.3); both compatible with per-block header stateRoot |
| Scope | Single harness feature; no EVM / trace coupling required; Task 4b optional |
| Ambiguity resolved | `storage` omitted vs `{}` vs listed distinguished by `hasStorage`; no exhaustive diff; root-of-view **not** used for partial maps; `eip158ClearEmpty` derived from `genesisRev` |
| Post-review fixes | CTest names corrected (`EthEestBlockchainSmoke/Full`); BlockValidationTest downgraded to non-normative; parent §8.1 pseudocode scheduled for correction |
| Plan re-check fix | Raw `postState`/`postStateHash` **kept** alongside new `postExpectation` — removing them would break ~6 `storage.find()` probes in `BlockValidationTest.cpp`; field migration is additive, converging legacy probes is optional (Task 4b) |

---

## Appendix A — Current vs Target (code reference)

**Current** (`BlockchainRunCore.h`):

- Compares `nonce`, `balance`, expected storage slots only
- No `code`
- No field presence
- Generic error strings

**Target** (normative — §4.4, **not** a verbatim copy of BlockValidationTest):

- Code byte comparison when `code` key present
- Storage: listed-slot check (missing ⇒ zero); `storage: {}` ⇒ all actual slots zero
- Optional-field semantics via `has*` flags
- `Kind::Absent` for null accounts
- Structured diff report

> **Caveat:** `BlockValidationTest.cpp` is *not* the normative reference. Some cases (e.g. `eip7685_multi_type_requests_header_fields`) scan actual storage slots and assert unlisted slots are zero — **stricter** than §2.2 listed-mode. The extracted `assertPostState` follows §4.4; do not replicate the actual-slot scan except where §2.2 `storage: {}` mode explicitly requires it.

**Loader** already reads `code` from JSON but the downstream assert dropped it — the fix is assert-side + type model (`has*` flags), not parser discovery. The one genuinely new parser concern is `null` / absent detection (Impl Task 0).

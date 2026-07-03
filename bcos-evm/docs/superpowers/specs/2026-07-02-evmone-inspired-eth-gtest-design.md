# evmone-Inspired Eth GTest Increment — Design Spec

**Date:** 2026-07-02  
**Status:** Draft — pending user review  
**Scope:** Increment B — borrow evmone testing patterns for bcos-evm Eth reference path only  
**Architecture choice:** Approach 1 — dedicated `test/eth-gtest/` module + `bcos-evm-test-eth-fixtures` static library  

**Related specs:**

- `docs/superpowers/specs/2026-06-21-bcos-evm-test-system-design.md` — parent test-system architecture (unchanged)
- `bcos-evm/docs/superpowers/specs/2026-06-30-gtest-state-block-eest-migration-design.md` — Pillar 3 implements Chapter 2–3 of that doc

**Frozen decisions (brainstorming):**

| Item | Choice |
|------|--------|
| Scope | evmone-borrowable increment only (not full test-pyramid redesign) |
| Execution path | Eth first; OPStack Phase 2 via `OpStackStateTransitionFixture` subclass |
| CI | All Phase 1 targets in PR smoke (`eth-gtest-smoke` label) |
| Framework | Google Test (GTest), not Boost.Test |

---

## 1. Problem Statement

bcos-evm already has:

- Manifest-driven EEST/GST runners under `test/eth-eest-test/`
- Hand-crafted Boost.Test unit tests under `test/eth/` (often hitting `InnerExecute` only)
- OPStack EEST adapter with `opstack-skip-list.json`

Gaps compared to evmone:

1. **No C++ `state_transition` fixture** — fast, in-process pre→tx→post tests on the full Eth execution path (`executeViaEth` / `StateTransitionExecute`)
2. **No systematic precompile matrix** — isolated per-address boundary cases with evmone oracle
3. **No Eth-path blockchain test runner** — block-level multi-tx semantics (OPStack has `OpStackEestBlockchainRunner`; Eth does not)

This spec adds three GTest pillars without replacing existing manifest runners or Boost unit tests.

---

## 2. Goals and Non-Goals

### 2.1 Phase 1 Goals

1. **`EthStateTransitionFixture`** — GTest base class mirroring evmone `state_transition.hpp`, executing through the real Eth orchestration path and asserting post-state, gas, and status.
2. **`PrecompileMatrix`** — parameterized GTest suites per precompile address with evmone as oracle (output + gas).
3. **`EthEestBlockGranular`** — GTest dynamic registration over EEST `blockchain_tests` smoke slice; validates cumulative `stateRoot`, receipts, block header fields.

All three register CTest targets with label `eth-gtest-smoke` and run on every PR (capability-gate).

### 2.2 Non-Goals (Phase 1)

- OPStack fixture implementation (interface only in §8)
- Full legacy `ethereum/tests` BlockchainTests ValidBlocks/InvalidBlocks sweep
- Replacing `eth-eest-test` manifest runners or migrating all Boost tests
- t8n tooling, fuzzer, micro-benchmarks
- BCOS baseline path adapter

---

## 3. Architecture (Approach 1)

### 3.1 Directory Layout

```
bcos-evm/test/eth-gtest/
  CMakeLists.txt
  fixtures/
    EthStateTransitionFixture.h
    EthStateTransitionFixture.cpp
    EthTransitionExpect.h
    PrecompileOracle.h          # evmone vs EthPrecompiles compare helper
  state/
    state_transition_intrinsic_reject_test.cpp
    state_transition_revert_nonce_test.cpp
    state_transition_eip7702_test.cpp
    state_transition_eip6780_test.cpp
    state_transition_eip7623_test.cpp
  precompiles/
    precompile_matrix_ecrecover_test.cpp
    precompile_matrix_sha256_test.cpp
    precompile_matrix_identity_test.cpp
    precompile_matrix_modexp_test.cpp
    precompile_matrix_bls_test.cpp
    precompile_matrix_secp256r1_test.cpp
  runners/
    EthEestBlockGranular.cpp    # main + RegisterTest loop
  helpers/
    BlockTransition.h
    BlockTransition.cpp         # applyEthBlock()
```

### 3.2 Static Library

```cmake
add_library(bcos-evm-test-eth-fixtures STATIC
    fixtures/EthStateTransitionFixture.cpp
    fixtures/PrecompileOracle.cpp
    helpers/BlockTransition.cpp
)
target_link_libraries(bcos-evm-test-eth-fixtures PUBLIC
    bcos-evm-eth
    bcos-evm-specs-tests-core   # StateTestAssert, loaders (blockchain only)
    evmone::evmone
    GTest::gtest
)
```

**Dependency rule:** `state/` and `precompiles/` binaries link `bcos-evm-test-eth-fixtures` only — they do **not** require EEST assets at configure time. `EthEestBlockGranular` links specs-tests-core and requires `EEST_ROOT` (same as existing `eth-eest-test`).

### 3.3 Component Diagram

```
 PR smoke (ctest -L eth-gtest-smoke)
 ┌──────────────────┬───────────────────┬─────────────────────┐
 │ EthStateTransition│ PrecompileMatrix  │ EthEestBlockGranular│
 │ GTest             │ GTest             │ GTest               │
 └────────┬─────────┴─────────┬─────────┴──────────┬──────────┘
          │                   │                    │
          └───────────────────┼────────────────────┘
                              ▼
                 bcos-evm-test-eth-fixtures
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
     bcos-evm-eth      bcos-evm-specs-tests-core  evmone
```

---

## 4. Pillar 1 — EthStateTransitionFixture

### 4.1 API (aligned with evmone `state_transition.hpp`)

```cpp
struct ExpectedAccount {
    bool exists = true;
    std::optional<uint64_t> nonce;
    std::optional<bcos::u256> balance;
    std::optional<bcos::bytes> code;
    std::unordered_map<bcos::bytes32, bcos::bytes32> storage;
};

struct EthTransitionExpect {
    std::optional<bcos::protocol::TransactionStatus> txError;  // preCheck reject
    evmc_status_code status = EVMC_SUCCESS;
    std::optional<int64_t> gasUsed;
    std::optional<int64_t> gasRefund;
    std::unordered_map<evmc_address, ExpectedAccount> post;
    std::optional<bcos::crypto::HashType> stateHash;  // optional MPT root
};

class EthStateTransitionFixture : public ::testing::Test {
protected:
    bcos::evm_standard::RevisionConfig revisionConfig;
    bcos::evm::state::BlockInfo block;
    // transaction fields: sender, to, value, data, gasLimit, accessList, type, ...
    bcos::evm::reference_tests::TestStateView pre;
    EthTransitionExpect expect;

    static constexpr evmc_address DefaultSender = /* 0xe100713F... */;
    static inline evmc::VM vm{evmc_create_evmone()};

    void SetUp() override;
    void TearDown() override;  // run transition + assert
};
```

### 4.2 Execution Entry

`TearDown()` calls a single helper:

```cpp
EthTransitionResult runEthTransition(
    TestStateView& pre,
    const EthTransitionTx& tx,
    const BlockInfo& block,
    const RevisionConfig& cfg,
    evmc::VM& vm);
```

Implementation wraps **`EthMessageAdapter::execute()`** (same primitive as EEST runners). No new execution shortcut through `InnerExecute` unless the test explicitly targets VM-only behavior (out of scope for this fixture).

### 4.3 Assertion Semantics

| Condition | Assert |
|-----------|--------|
| `expect.txError` set | Transaction rejected before EVM; post state unchanged; `gasUsed == 0` or full buyGas refund per ADR-015 |
| Valid tx, `expect.status` | `evmc_status_code` matches |
| `expect.gasUsed` | Matches settlement output |
| `expect.post` | Per-account nonce/balance/code/storage |
| `expect.stateHash` | Optional; use when MPT root available |

Reuse `StateTestAssert` helpers from `bcos-evm-specs-tests-core` where field comparison logic already exists.

### 4.4 Phase 1 Test Cases (minimum set)

| File | Scenario | Fork |
|------|----------|------|
| `state_transition_intrinsic_reject_test.cpp` | Intrinsic gas / initcode limit rejection; full buyGas refund | Cancun/Prague |
| `state_transition_revert_nonce_test.cpp` | Included REVERT still bumps sender nonce | Cancun |
| `state_transition_eip7702_test.cpp` | Delegation + empty auth list rejection | Prague |
| `state_transition_eip6780_test.cpp` | Same-tx selfdestruct semantics | Cancun |
| `state_transition_eip7623_test.cpp` | Calldata floor gas on included path | Prague |

These mirror gaps found in EEST full runs and fixes from commit `078805a45` (refund, intrinsic reject).

### 4.5 Migration Policy

New EIP regression tests **should** use `EthStateTransitionFixture` first. Existing Boost tests in `test/eth/` remain until migrated; migration is opportunistic, not a Phase 1 gate.

---

## 5. Pillar 2 — Precompile Matrix

### 5.1 Pattern

Each precompile gets a GTest file with `TEST_F(PrecompileMatrix, ...)` or `TEST_P(PrecompileMatrixParam, ...)`:

| Dimension | Cases |
|-----------|--------|
| Empty input | success or revert per spec |
| Boundary length | min gas, max gas, OOG |
| Invalid input | expected `evmc_status_code` |
| Revision gate | call at `rev-1` → undefined / revert |

### 5.2 Oracle

`PrecompileOracle` runs the same input through:

1. evmone built-in precompile (via `evmc_execute`)
2. bcos `EthPrecompiles` routing through host

Compare: output bytes, `gas_left`, status code.

Reference pattern: `test/opstack/EvmoneRefundSpikeTest.cpp`.

### 5.3 Phase 1 Coverage

| Address | EIP | Priority |
|---------|-----|----------|
| 0x01 | ecrecover | P0 |
| 0x02 | SHA-256 | P0 |
| 0x04 | identity | P0 |
| 0x05 | modexp (7823 gas) | P0 |
| 0x0a | BLS12-381 (2537) | P1 |
| 0x100 | secp256r1 (7951) | P1 |

Align case inputs with `eth-eest-precompile-probe` manifest vectors where possible.

---

## 6. Pillar 3 — EthEestBlockGranular

Implements **Chapter 2–3** of `2026-06-30-gtest-state-block-eest-migration-design.md` with PR-smoke constraints from this spec.

### 6.1 `applyEthBlock()`

```cpp
struct BlockApplyResult {
    TestStateView postState;
    std::vector<TransactionReceiptMeta> receipts;
    int64_t gasUsed = 0;
    // bloom if needed
};

BlockApplyResult applyEthBlock(
    TestStateView& preState,
    std::span<const EthTransitionTx> transactions,
    const BlockInfo& blockInfo,
    const ForkProfile& profile,
    evmc::VM& vm,
    bcos::crypto::Hash& hashImpl);
```

Steps:

1. For each tx in order: `EthMessageAdapter::execute()`, accumulate state diff
2. Block post-processing: coinbase reward = 0 (state test convention), apply withdrawals if present
3. Compute state root via existing `GstStateHash` / MPT helper
4. Return aggregated receipts and `gasUsed`

### 6.2 GTest Registration

Same dual granularity as evmone / 06-30 spec:

- **Directory input:** one GTest case per JSON file
- **Single file:** one GTest case per block index / variant

CLI:

```bash
EthEestBlockGranular --fixtures $EEST_ROOT/fixtures/blockchain_tests \
  --manifest manifests/eth/eth-eest-blockchain-smoke.json \
  --gtest_filter='*withdrawals*'
```

### 6.3 PR Smoke Slice

New manifest `test/eth-eest-test/manifests/eth/eth-eest-blockchain-smoke.json`:

- `--limit 20` hard cap in CTest definition
- Curated patterns: `eip4895_withdrawals`, multi-tx block, one invalid-header negative case
- Target wall time: **≤ 60s** on CI runner

Full EEST blockchain directory: label `eth-gtest-full`, nightly only.

### 6.4 Relationship to OpStack Runner

| | Eth | OPStack |
|---|-----|---------|
| Runner | `EthEestBlockGranular` | `OpStackEestBlockchainRunner` |
| Execution | `EthMessageAdapter` | `OpStackExecuteViaHost` |
| Skip policy | None (strict parity) | `opstack-skip-list.json` |

---

## 7. CI Integration

### 7.1 CTest Labels

| Label | Targets | When |
|-------|---------|------|
| `eth-gtest-smoke` | `EthStateTransitionGTest`, `PrecompileMatrixGTest`, `EthEestBlockGranular` (smoke manifest) | Every PR |
| `eth-gtest-full` | `EthEestBlockGranular` (full fixtures dir) | Nightly |

### 7.2 CMake Targets

```cmake
# eth-gtest/CMakeLists.txt
add_executable(EthStateTransitionGTest state/*.cpp)
target_link_libraries(EthStateTransitionGTest PRIVATE bcos-evm-test-eth-fixtures GTest::gtest_main)
gtest_discover_tests(EthStateTransitionGTest TEST_PREFIX eth-gtest/state/)

add_executable(PrecompileMatrixGTest precompiles/*.cpp)
# ...

add_executable(EthEestBlockGranular runners/EthEestBlockGranular.cpp)
target_link_libraries(EthEestBlockGranular PRIVATE bcos-evm-test-eth-fixtures bcos-evm-specs-tests-eth)
gtest_discover_tests(EthEestBlockGranular TEST_PREFIX eth-gtest/block/)
```

### 7.3 capability-gate Addition

```bash
ctest -L 'eth-gtest-smoke' --output-on-failure
```

Requires `BCOS_EVM_SPECS_TESTS=ON` only for `EthEestBlockGranular`; state + precompile binaries build under `TESTS=ON` without FetchContent.

### 7.4 Time Budget

| Target | Budget |
|--------|--------|
| `EthStateTransitionGTest` | ≤ 15s |
| `PrecompileMatrixGTest` | ≤ 15s |
| `EthEestBlockGranular` (smoke) | ≤ 60s |

If smoke exceeds budget, shrink manifest slice — do not remove from PR gate.

---

## 8. Phase 2 Extension (OPStack — not implemented in Phase 1)

Reserve subclass only; no Phase 1 code.

```cpp
class OpStackStateTransitionFixture : public EthStateTransitionFixture {
protected:
    OpStackSkipPolicy skipPolicy;  // loads opstack-skip-list.json rules

    EthTransitionResult runTransition(...) override;  // opStackExecuteViaHost
    void assertPost(const EthTransitionExpect& expect) override;  // skip G1/G2 fields
};
```

Skip dimensions: sender nonce, balance on fee recipients (`OP_BASE_FEE_RECIPIENT`, `OP_L1_FEE_RECIPIENT`), L1Block predeploy (G3).

---

## 9. GTest Dependency

bcos-evm `test/eth/` currently uses Boost.Test. This module introduces GTest as an **additive** dependency for `test/eth-gtest/` only.

- Source GTest via Hunter or existing project third-party (match root CMake policy)
- Do not convert existing Boost tests in Phase 1
- `gtest_discover_tests` registers subtests for CI filtering (`--gtest_filter` in local dev)

---

## 10. Implementation Order

| Step | Deliverable | PR gate |
|------|-------------|---------|
| 1 | CMake scaffold + `bcos-evm-test-eth-fixtures` empty lib + GTest wiring | build |
| 2 | `EthStateTransitionFixture` + intrinsic reject + revert nonce tests | smoke |
| 3 | 7702 + 6780 + 7623 state tests | smoke |
| 4 | Precompile matrix P0 (ecrecover, sha256, identity, modexp) | smoke |
| 5 | `applyEthBlock` + `EthEestBlockGranular` + blockchain smoke manifest | smoke |
| 6 | Precompile P1 (bls, secp256r1) | smoke |
| 7 | capability-gate `eth-gtest-smoke` label | CI |

Estimated: 3–4 focused PRs.

---

## 11. Success Criteria

Phase 1 complete when:

1. All three GTest binaries pass under `ctest -L eth-gtest-smoke` on PR CI
2. At least 5 `EthStateTransitionFixture` cases cover known EEST parity gaps
3. Precompile matrix P0 passes with evmone oracle on all cases
4. Blockchain smoke manifest runs ≥ 10 EEST block fixtures with `stateRoot` assertion
5. No regression to existing `specs-tests-smoke` manifest runners
6. Design doc approved and implementation plan written (`writing-plans` skill)

---

## 12. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| PR CI time increase | Strict case count caps; state/precompile stay in-process |
| GTest + Boost coexistence | Separate directory; no cross-link |
| `EthMessageAdapter` API drift | Fixture calls same adapter as EEST runners |
| Blockchain smoke flaky on EEST pin upgrade | Pin in `upstream-pins.json`; manifest uses stable slice |
| Duplicate coverage with EEST state manifest | C++ fixture targets **fast regression**; EEST remains compliance sweep |

---

## Appendix A — Comparison with evmone

| evmone | bcos-evm (this spec) |
|--------|----------------------|
| `state_transition.hpp` + `evmone-unittests` | `EthStateTransitionFixture` + `EthStateTransitionGTest` |
| `precompiles_*_test.cpp` | `PrecompileMatrixGTest` + `PrecompileOracle` |
| `evmone-blockchaintest` + EEST fixtures | `EthEestBlockGranular` + smoke manifest |
| `test/state` MPT machine | Reuse `TestStateView` + `GstStateHash` (existing) |

---

## Appendix B — Spec Self-Review Checklist

- [x] No TBD placeholders in scope or API sections
- [x] CI decision (all PR smoke) consistent with §7
- [x] Phase 2 explicitly deferred
- [x] No contradiction with 2026-06-21 parent spec (increment only)
- [x] Pillar 3 references 06-30 spec without duplicating full Chapter 2 prose
- [x] Single implementation plan scope (one module, 3 binaries, 1 static lib)

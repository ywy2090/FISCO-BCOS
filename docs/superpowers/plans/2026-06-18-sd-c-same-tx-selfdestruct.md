# SD-C Same-Tx SELFDESTRUCT Bytecode Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `TE_FC_E_SD_same_tx_create_destroy_todo` with dual-path real bytecode tests proving EIP-6780 SD-C semantics (Eth destroys same-tx CREATE; FISCO retains).

**Architecture:** Shared init bytecode header (`PUSH20 0xbb; SELFDESTRUCT`) drives two harnesses: `TransactionExecutorImpl` (FISCO + `FiscoHostExtension`) and `CompatHostShim` (Eth reference, no extension). SD-B refactored to consume the same constants for the B/C matrix.

**Tech Stack:** C++20, Boost.Test, evmone, `TransactionExecutorImpl`, `CompatHostShim` / `Eip2929ExecuteViaHostFixture`, `EVMAccount`, `InMemoryStateView`

## Global Constraints

- Spec: `docs/superpowers/specs/2026-06-18-sd-c-same-tx-selfdestruct-design.md` (approved)
- **No production code changes** in `bcos-evm` or `bcos-executor` (tests + comments only)
- **No** SD-A, JSON fixture loader, nested factory CREATE, or `allowSelfdestruct` behavior changes
- Prague revision required: `feature_evm_cancun` + `feature_evm_prague`, `BlockVersion::MAX_VERSION`
- Shared beneficiary: `0x00000000000000000000000000000000000000bb`
- Shared SELFDESTRUCT tail: `73000000000000000000000000000000000000bbff`
- Sender for both paths: `0x0000000000000000000000000000000000000001`
- Commands use `rtk` prefix (repository CLAUDE.md)
- `CompatTransactionExecutorPhaseETest.cpp` and `CompatExecuteViaHostPhaseETest.cpp` already have `SKIP_UNITY_BUILD_INCLUSION ON` — no CMake change expected

---

## File Map

| File | Responsibility |
|------|----------------|
| `transaction-executor/tests/SelfdestructCompatBytecode.h` | Shared SD-B/SD-C bytecode + address constants |
| `transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp` | SD-B refactor, FISCO SD-C test, doc stubs |
| `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp` | Eth reference SD-C test |
| `bcos-executor/test/unittest/evmone/compat/CompatSelfdestructTest.cpp` | Cross-ref comment on `FC_SD_C_*` stub |

---

### Task 1: Shared bytecode header

**Files:**
- Create: `transaction-executor/tests/SelfdestructCompatBytecode.h`

**Interfaces — Produces:**
```cpp
namespace bcos::evm::test::selfdestruct_compat {
constexpr std::string_view kSelfdestructTargetHex;   // SD-B pre-seeded contract
constexpr std::string_view kBeneficiaryHex;
constexpr std::string_view kSelfdestructTail;        // PUSH20 + SELFDESTRUCT
constexpr std::string_view kSelfdestructRuntimeCode; // alias for SD-B
constexpr std::string_view kSelfdestructInitCode;    // alias for SD-C CREATE data
}
```

- [ ] **Step 1: Create header**

```cpp
/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared SELFDESTRUCT compat bytecode for SD-B / SD-C matrix tests.
 *  @file SelfdestructCompatBytecode.h
 */
#pragma once

#include <string_view>

namespace bcos::evm::test::selfdestruct_compat
{
constexpr std::string_view kSelfdestructTargetHex =
    "0000000000000000000000000000000000000012";
constexpr std::string_view kBeneficiaryHex =
    "00000000000000000000000000000000000000bb";
constexpr std::string_view kSelfdestructTail =
    "73000000000000000000000000000000000000bbff";
constexpr std::string_view kSelfdestructRuntimeCode = kSelfdestructTail;
constexpr std::string_view kSelfdestructInitCode = kSelfdestructTail;
}  // namespace bcos::evm::test::selfdestruct_compat
```

- [ ] **Step 2: Verify compile** — no standalone target; compilation verified in Task 2

- [ ] **Step 3: Commit**

```bash
rtk git add transaction-executor/tests/SelfdestructCompatBytecode.h
rtk git commit -m "test(transaction-executor): add shared SELFDESTRUCT compat bytecode constants"
```

---

### Task 2: FISCO executive SD-C test + SD-B dedupe

**Files:**
- Modify: `transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp`

**Interfaces — Consumes:** `selfdestruct_compat::kSelfdestructTargetHex`, `kBeneficiaryHex`, `kSelfdestructRuntimeCode`, `kSelfdestructInitCode` from Task 1

- [ ] **Step 1: Add include and remove local constants**

At top of file, after existing includes:

```cpp
#include "SelfdestructCompatBytecode.h"
```

Delete the anonymous-namespace block (lines 25–30):

```cpp
constexpr std::string_view kSelfdestructTargetHex = ...
constexpr std::string_view kSelfdestructBeneficiaryHex = ...
constexpr std::string_view kSelfdestructRuntimeCode = ...
```

Add namespace alias inside `namespace bcos::evm::test`:

```cpp
namespace sd = selfdestruct_compat;
```

- [ ] **Step 2: Update SD-B test to use `sd::` constants**

In `seedSelfdestructContract()` and `TE_FC_E_SD_existing_contract_keeps_code`:

```cpp
auto const target = unhexAddress(std::string(sd::kSelfdestructTargetHex));
// ...
boost::algorithm::unhex(sd::kSelfdestructRuntimeCode, std::back_inserter(code));
// ...
auto const targetHex = std::string(sd::kSelfdestructTargetHex);
// ...
auto const beneficiary = unhexAddress(std::string(sd::kBeneficiaryHex));
```

- [ ] **Step 3: Replace SD-C TODO with real FISCO test**

Remove `TE_FC_E_SD_same_tx_create_destroy_todo` entirely.

Add after `TE_FC_E_SD_fisco_hook_documented`:

```cpp
BOOST_AUTO_TEST_CASE(TE_FC_E_SD_same_tx_create_destroy_fisco)
{
    task::syncWait([this]() -> task::Task<void> {
        co_await seedSender(u256(0x1000000));

        bytes initCode;
        boost::algorithm::unhex(sd::kSelfdestructInitCode, std::back_inserter(initCode));
        auto deployTx = transactionFactory.createTransaction(0, "", initCode, {}, 0, "", "", 0);
        deployTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto receipt = co_await executor.executeTransaction(
            storage, makeBlockHeader(), *deployTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(!receipt->contractAddress().empty());

        auto const deployed = unhexAddress(receipt->contractAddress());
        ledger::account::EVMAccount deployedAccount(storage, deployed, false);
        BOOST_REQUIRE(co_await deployedAccount.exists());
        auto const code = co_await deployedAccount.code();
        BOOST_REQUIRE(code.has_value());
        BOOST_CHECK(!code->get().empty());

        auto const beneficiary = unhexAddress(std::string(sd::kBeneficiaryHex));
        ledger::account::EVMAccount beneficiaryAccount(storage, beneficiary, false);
        BOOST_CHECK(!co_await beneficiaryAccount.exists());
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_pair_b_keep_c_destroy_documented)
{
    BOOST_TEST_MESSAGE(
        "SD-B/C matrix: pre-existing runtime SELFDESTRUCT keeps code on FISCO (SD-B); "
        "same-tx CREATE init SELFDESTRUCT keeps code on FISCO (SD-C fisco) but destroys on "
        "Eth reference (SD-C eth) per EIP-6780.");
    BOOST_CHECK(true);
}
```

- [ ] **Step 4: Build and run FISCO Phase E suite**

```bash
cmake --build build --target test-transaction-executor -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./build/transaction-executor/tests/test-transaction-executor \
  --run_test=CompatTransactionExecutorPhaseE --report_level=detailed
```

Expected:
- `TE_FC_E_SD_existing_contract_keeps_code` — PASS
- `TE_FC_E_SD_same_tx_create_destroy_fisco` — PASS
- `TE_FC_E_SD_same_tx_create_destroy_todo` — **absent** (removed)
- All other Phase E executor cases — PASS

- [ ] **Step 5: Commit**

```bash
rtk git add transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp
rtk git commit -m "test(transaction-executor): add FISCO SD-C same-tx selfdestruct harness"
```

---

### Task 3: Eth reference SD-C test

**Files:**
- Modify: `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp`

**Interfaces — Consumes:** `SelfdestructCompatBytecode.h` from Task 1; `Eip2929ExecuteViaHostFixture::CompatHostShim`; `CompatFeatureProfile::pragueEnabled()`

- [ ] **Step 1: Add includes**

```cpp
#include "SelfdestructCompatBytecode.h"
#include <boost/algorithm/hex.hpp>
```

- [ ] **Step 2: Add Eth reference test case**

Insert before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE(TE_FC_E_SD_same_tx_create_destroy_eth_reference)
{
    namespace sd = bcos::evm::test::selfdestruct_compat;

    Eip2929ExecuteViaHostFixture fixture;
    auto features = CompatFeatureProfile::pragueEnabled();
    fixture.ledgerConfig.setFeatures(features);
    auto const pragueRev = revisionConfigFrom(
        features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    evmc_address sender{};
    sender.bytes[19] = 0x01;
    fixture.fund(sender, bcos::u256(0x1000000));

    bytes initCode;
    boost::algorithm::unhex(sd::kSelfdestructInitCode, std::back_inserter(initCode));

    Eip2929ExecuteViaHostFixture::CompatHostShim shim(
        fixture, pragueRev, sender, evmc_address{}, EVMC_CREATE, nullptr, 0, 2'000'000);
    shim.mutableMessage().input_data = initCode.data();
    shim.mutableMessage().input_size = initCode.size();

    task::syncWait([&]() -> task::Task<void> {
        co_await shim.prepare();
        auto const created = shim.mutableMessage().recipient;
        auto result = co_await shim.execute();

        BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);

        auto const post = fixture.stateView.get_account(created);
        bool const destroyed = !post.has_value() || post->code.empty();
        BOOST_CHECK_MESSAGE(destroyed,
            "SD-C Eth reference: same-tx CREATE+init SELFDESTRUCT should destroy contract "
            "(EIP-6780 exception); FISCO path retains code via allowSelfdestruct=false.");
    }());
}
```

**Notes for implementer:**
- Keep `initCode` alive through `execute()` (stack-local `bytes` vector).
- `CompatHostShim` builds `EthHost` with `extension=nullptr` → default `allowSelfdestruct=true`.
- CREATE address comes from `deriveMessage` inside `prepare()`; read `recipient` **after** `prepare()`, **before** `execute()`.
- FISCO CREATE addressing uses `blockNum_contextID_seq` hash (not RLP); Eth shim uses the same `deriveMessage` path — no need to match `receipt->contractAddress()` from Task 2.

- [ ] **Step 3: Build and run executeViaHost Phase E suite**

```bash
cmake --build build --target test-transaction-executor -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./build/transaction-executor/tests/test-transaction-executor \
  --run_test=CompatExecuteViaHostPhaseE --report_level=detailed
```

Expected:
- `TE_FC_E_SD_same_tx_create_destroy_eth_reference` — PASS
- All existing Phase E executeViaHost cases — PASS

**If Eth test fails with contract not destroyed:** Check `pragueRev.eth().revision >= EVMC_CANCUN` and `eip6780` flag; confirm `initCode` is not empty; confirm `EVMC_CREATE` kind and gas ≥ 2'000'000.

- [ ] **Step 4: Commit**

```bash
rtk git add transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp
rtk git commit -m "test(transaction-executor): add Eth reference SD-C same-tx selfdestruct test"
```

---

### Task 4: Legacy compat cross-reference

**Files:**
- Modify: `bcos-executor/test/unittest/evmone/compat/CompatSelfdestructTest.cpp:52-58`

- [ ] **Step 1: Add cross-ref to `FC_SD_C_*` stub**

Replace the `BOOST_TEST_MESSAGE` body in `FC_SD_C_cancun_same_tx_create_then_selfdestruct_todo`:

```cpp
BOOST_AUTO_TEST_CASE(FC_SD_C_cancun_same_tx_create_then_selfdestruct_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-C TODO (legacy stub): real harness lives in transaction-executor/tests/ — "
        "TE_FC_E_SD_same_tx_create_destroy_fisco (FISCO retains) and "
        "TE_FC_E_SD_same_tx_create_destroy_eth_reference (Eth destroys). "
        "Pair with SD-B TE_FC_E_SD_existing_contract_keeps_code for EIP-6780 matrix.");
    BOOST_CHECK(true);
}
```

Optionally update `FC_SD_B_*` message similarly (one line cross-ref to `TE_FC_E_SD_existing_contract_keeps_code`) — not required by spec but improves discoverability.

- [ ] **Step 2: Commit**

```bash
rtk git add bcos-executor/test/unittest/evmone/compat/CompatSelfdestructTest.cpp
rtk git commit -m "docs(test): cross-ref legacy SD-C stub to transaction-executor harness"
```

---

### Task 5: Final verification

**Files:** None (verification only)

- [ ] **Step 1: Run both Phase E suites together**

```bash
./build/transaction-executor/tests/test-transaction-executor \
  --run_test=CompatTransactionExecutorPhaseE
./build/transaction-executor/tests/test-transaction-executor \
  --run_test=CompatExecuteViaHostPhaseE
```

Expected: zero failures.

- [ ] **Step 2: Confirm success criteria from spec**

| Criterion | Check |
|-----------|-------|
| `TE_FC_E_SD_same_tx_create_destroy_todo` removed | `rg same_tx_create_destroy_todo transaction-executor/tests/` → no matches |
| FISCO SD-C asserts code retained | `TE_FC_E_SD_same_tx_create_destroy_fisco` `BOOST_CHECK(!code->get().empty())` |
| Eth SD-C asserts destroyed | `destroyed = !post.has_value() \|\| post->code.empty()` |
| Shared header used | `rg SelfdestructCompatBytecode transaction-executor/tests/` → 2+ files |
| Doc stub added | `TE_FC_E_SD_pair_b_keep_c_destroy_documented` exists |

- [ ] **Step 3: Optional — update remaining-tasks tracker**

If `.superpowers/sdd/remaining-tasks.md` exists and lists SD-C TODO, mark SD-C harness complete under T-17 Phase E.

- [ ] **Step 4: Final commit (only if Step 3 edits a tracker file)**

```bash
rtk git add .superpowers/sdd/remaining-tasks.md
rtk git commit -m "docs(sdd): mark T-17 SD-C same-tx selfdestruct harness complete"
```

---

## Spec Coverage Checklist

| Spec § | Task |
|--------|------|
| §5 Shared bytecode header | Task 1 |
| §6 FISCO executive test | Task 2 |
| §7 Eth reference test | Task 3 |
| §8 Documentation stubs | Task 2 (`pair_b_keep_c_destroy`), Task 4 (legacy cross-ref) |
| §9 Assertion matrix | Tasks 2–3 assertions |
| §10 Files to change | Tasks 1–4 |
| §11 Verification commands | Tasks 2, 3, 5 |
| §13 Success criteria | Task 5 |

**Non-goals explicitly excluded:** SD-A, JSON fixtures, production `FiscoHostExtension` changes, CMake new targets.

---

## Risk Playbook

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| FISCO deploy receipt non-zero status | Insufficient sender balance / gas | Ensure `seedSender(0x1000000)` and Prague features |
| FISCO contract has empty code after CREATE+SD | Unexpected hook change | **Do not** change production code; file issue — test expects non-empty |
| Eth test: contract still has code | Revision below Cancun or host extension attached | Verify `pragueEnabled()`, `extension=nullptr` in shim |
| Compile error `sd::` not found | Missing include | Add `#include "SelfdestructCompatBytecode.h"` |
| Unity build ODR clash | New .cpp without SKIP | Only header added — no action; existing SKIP covers test .cpp files |

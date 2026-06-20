# SD-C Same-Tx CREATE→SELFDESTRUCT Bytecode Harness

**Status:** Approved  
**Date:** 2026-06-18  
**Scope:** T-17 Phase E closure — replace `TE_FC_E_SD_same_tx_create_destroy_todo` with dual-path real tests  
**Related:** SD-B (`TE_FC_E_SD_existing_contract_keeps_code`), EIP-6780, `FiscoHostExtension::allowSelfdestruct`

---

## 1. Problem

T-17 Phase E documents SELFDESTRUCT compat under EIP-6780 in three tiers:

| ID | Scenario | Mainnet Prague+ (EIP-6780) | FISCO current |
|----|----------|----------------------------|---------------|
| SD-B | Pre-existing contract, CALL runtime SELFDESTRUCT | Code retained | Code retained (hook blocks) |
| SD-C | Same transaction CREATE init → SELFDESTRUCT | **Contract destroyed** | **Contract retained** (hook blocks) |
| SD-A | Pre-Cancun legacy baseline | Out of scope here | Out of scope here |

SD-B is implemented in `CompatTransactionExecutorPhaseETest.cpp`. SD-C remains a documentation stub (`TE_FC_E_SD_same_tx_create_destroy_todo`). The legacy `bcos-executor` compat file `CompatSelfdestructTest.cpp` also stubs `FC_SD_C_*` and defers to transaction-executor integration tests.

SD-C is the primary detector for the **conditional** branch of EIP-6780: same-tx-created contracts may still be destroyed on mainnet, while pre-existing contracts may not (SD-B). FISCO currently blocks all SELFDESTRUCT at `EthHost::selfdestruct` via `FiscoHostExtension::allowSelfdestruct() == false`, so both SD-B and SD-C observe “code retained” on the FISCO executive path. The harness must prove that distinction on the Eth reference path.

---

## 2. Goals

1. Replace the SD-C TODO stub with **real bytecode execution** on two paths:
   - **FISCO executive:** `TransactionExecutorImpl` (full stack + `FiscoHostExtension`)
   - **Eth reference:** `CompatHostShim` / `executeViaHost` without FISCO hook (default `allowSelfdestruct=true`)
2. Form an explicit **SD-B / SD-C contrast matrix** using shared beneficiary and SELFDESTRUCT tail bytecode.
3. Keep scope minimal: no JSON fixture loader, no SD-A, no change to `FiscoHostExtension` production behavior.

---

## 3. Non-Goals

- SD-A (pre-Cancun legacy selfdestruct baseline)
- JSON-driven state fixtures (`prague_same_tx_selfdestruct.json`) and shared loaders
- Nested factory CREATE patterns (CREATE inside CREATE init)
- Changing `allowSelfdestruct` or implementing full EIP-6780 tracking in FISCO production code
- Migrating or deleting `CompatSelfdestructTest.cpp` stubs (add cross-reference comment only)

---

## 4. Chosen Approach

**Approach A — shared init bytecode, split harness dual tests** (approved over JSON fixture and nested-factory options).

Rationale:

- Top-level CREATE with init code `PUSH20 beneficiary; SELFDESTRUCT` is the minimal same-tx CREATE→SELFDESTRUCT scenario.
- Reuses Phase E fixture patterns and SD-B constants.
- Eth reference intentionally omits `FiscoHostExtension` so evmone + EIP-6780 same-tx exception can destroy the contract; this is the control path for mainnet semantics.

---

## 5. Bytecode Harness

### 5.1 Shared constants

New header: `transaction-executor/tests/SelfdestructCompatBytecode.h`

```cpp
namespace bcos::evm::test::selfdestruct_compat {
constexpr std::string_view kBeneficiaryHex =
    "00000000000000000000000000000000000000bb";
constexpr std::string_view kSelfdestructTail =
    "73000000000000000000000000000000000000bbff";  // PUSH20 + SELFDESTRUCT
constexpr std::string_view kSelfdestructRuntimeCode = kSelfdestructTail;
constexpr std::string_view kSelfdestructInitCode = kSelfdestructTail;
}
```

SD-B uses `kSelfdestructRuntimeCode` on a pre-seeded contract. SD-C uses `kSelfdestructInitCode` as the **CREATE transaction data** (init code).

### 5.2 Execution flow

```
Sender
  └─ CREATE tx (to="", data=init bytecode)
       └─ CREATE frame → contract address = deriveMessage(sender, nonce)
            └─ init: PUSH20 0xbb; SELFDESTRUCT
                 ├─ Eth reference (no extension): host allows → evmone EIP-6780 same-tx destroy
                 └─ FISCO executive (extension): allowSelfdestruct=false → no destroy
```

### 5.3 Address derivation

- FISCO path: use `receipt->contractAddress()` from `TransactionExecutorImpl` after deploy.
- Eth path: use the same `deriveMessage` / `FiscoTxAdapter` rules as `CompatHostShim::prepare()` so CREATE recipient matches FISCO address semantics for the test sender and nonce.

---

## 6. FISCO Executive Test

**File:** `transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp`  
**Case:** `TE_FC_E_SD_same_tx_create_destroy_fisco` (replaces `_todo`)

**Fixture:** `CompatTransactionExecutorPhaseEFixture` (unchanged)

- Prague features: `feature_evm_cancun`, `feature_evm_prague`, `MAX_VERSION` block header
- Sender: `0x0000000000000000000000000000000000000001`, nonce seeded to `1`, sufficient balance

**Steps:**

1. `seedSender`
2. Build deploy tx: `createTransaction(0, "", initBytes, {}, 0, "", "", 0)` with `kSelfdestructInitCode`
3. `executor.executeTransaction(...)`
4. Assert:
   - `receipt->status() == 0`
   - `receipt->contractAddress()` non-empty
   - Contract at deployed address: `exists()`, `code()` non-empty
   - Beneficiary `0xbb`: `!exists()`

**Expected FISCO outcome:** Same as SD-B — contract survives because `FiscoHostExtension::allowSelfdestruct` always returns `false`.

---

## 7. Eth Reference Test

**File:** `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp`  
**Case:** `TE_FC_E_SD_same_tx_create_destroy_eth_reference`

**Fixture:** `Eip2929ExecuteViaHostFixture` + `CompatHostShim`

- Features: Prague (`CompatFeatureProfile::pragueEnabled()` or equivalent Phase E Prague set)
- Sender: `0x01`, funded, nonce consistent with shim `prepare()`
- Message: `EVMC_CREATE`, `input_data` = init bytecode, sufficient gas (e.g. 2_000_000)

**Host configuration:** `CompatHostShim` constructs `EthHost` with `extension = nullptr` → default `HostExtension::allowSelfdestruct` returns `true`.

**Steps:**

1. `fund(sender)`
2. Build `CompatHostShim(fixture, pragueRev, sender, {}, EVMC_CREATE)`
3. Set `input_data` / `input_size` to init bytes
4. `prepare()` → `execute()`
5. Resolve CREATE contract address from state / derived recipient after `prepare()`
6. Assert:
   - `result.status_code == EVMC_SUCCESS`
   - Created address: `!account_exists(addr)` **or** `get_code_size(addr) == 0`
   - (Optional) beneficiary balance increased if state view exposes balance

**Expected Eth outcome:** Contract created in the same transaction is destroyed per EIP-6780 same-tx exception — contrasts with SD-B (pre-existing, code kept) and FISCO SD-C (hook blocks destroy).

---

## 8. Documentation Stubs

| Case | Action |
|------|--------|
| `TE_FC_E_SD_fisco_hook_documented` | Keep — explains FISCO `allowSelfdestruct=false` |
| `TE_FC_E_SD_pair_b_keep_c_destroy_documented` | **Add** — documents SD-B/C matrix and dual-path intent |
| `TE_FC_E_SD_same_tx_create_destroy_todo` | **Remove** — replaced by real tests |
| `FC_SD_C_cancun_same_tx_create_then_selfdestruct_todo` | Keep stub; add comment pointing to TE cases |

---

## 9. Assertion Matrix

| Case | Init vs runtime | Path | Contract after tx | Beneficiary account |
|------|-----------------|------|-------------------|---------------------|
| SD-B | Runtime on seeded `0x12` | FISCO executive | Code **retained** | Does not exist |
| SD-C | Init on CREATE | FISCO executive | Code **retained** | Does not exist |
| SD-C | Init on CREATE | Eth reference | **Destroyed** / empty code | May receive balance |

---

## 10. Files to Change

| File | Change |
|------|--------|
| `transaction-executor/tests/SelfdestructCompatBytecode.h` | **Create** — shared constants |
| `transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp` | Replace SD-C todo; include shared header; optionally dedupe SD-B constants |
| `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp` | Add Eth reference SD-C case |
| `bcos-executor/test/unittest/evmone/compat/CompatSelfdestructTest.cpp` | Cross-ref comment on `FC_SD_C_*` |
| `transaction-executor/tests/CMakeLists.txt` | No new target; confirm existing sources include changed files |

No production code changes in `bcos-evm` or `bcos-executor` for this task.

---

## 11. Verification

```bash
./transaction-executor/tests/test-transaction-executor --run_test=CompatTransactionExecutorPhaseE
./transaction-executor/tests/test-transaction-executor --run_test=CompatExecuteViaHostPhaseE
```

Both suites must pass. New cases must not regress existing Phase E cases.

---

## 12. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| CREATE address mismatch on Eth path | Use same sender/nonce as FISCO test; resolve address post-`prepare()` |
| evmone revision below Cancun skips EIP-6780 | Force Prague revision config in both tests |
| `CompatHostShim` not representative of full `executeViaHost` | Documented as intentional Eth control path without FISCO hook; sufficient for SD-C semantic contrast |
| Init SELFDESTRUCT returns SUCCESS but empty runtime code | Assert `code_size == 0` or `!exists` rather than only receipt status |

---

## 13. Success Criteria

- [ ] `TE_FC_E_SD_same_tx_create_destroy_todo` removed; no remaining SD-C TODO in Phase E executor suite
- [ ] FISCO SD-C test asserts contract code retained after CREATE+init SELFDESTRUCT
- [ ] Eth reference SD-C test asserts contract destroyed or zero code size
- [ ] Shared bytecode header used by SD-B and SD-C cases
- [ ] `ctest` / manual `--run_test` green for both Phase E suites

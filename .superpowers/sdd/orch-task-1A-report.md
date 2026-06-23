# Task 1A Report: Pure orchestration helpers

**Status:** DONE_WITH_CONCERNS  
**Commit:** `b868cc708 feat(evm): add orchestration gas and result helpers`

---

## Files created/changed

| File | Action |
| --- | --- |
| `bcos-evm/eth/orchestration/adoptEvmcResult.h` | Created |
| `bcos-evm/eth/orchestration/debitIntrinsicGas.h` | Created |
| `bcos-evm/test/eth/DebitIntrinsicGasTest.cpp` | Created |
| `bcos-evm/test/CMakeLists.txt` | Modified — registered `DebitIntrinsicGasTest` target + ctest |

---

## Interfaces implemented

### `adoptEvmcResult.h`

```cpp
EVMCResult adoptEvmcResult(evmc::Result&& result, bcos::crypto::Hash const& hashImpl);
```

Mirrors the three anonymous `adoptResult` copies in `ExecuteViaEth.cpp`, `ExecuteViaHost.cpp`, and `OpStackExecuteViaHost.cpp`: releases raw evmc result, maps status via `evmcStatusToErrorMessage`, returns `EVMCResult(raw, status)`.

### `debitIntrinsicGas.h`

- `enum class IntrinsicDebitMode { None, AuthOnly, Eip7623, OpStackEntry }`
- `enum class IntrinsicDebitFailure { None, GasLimitMinimum, CalldataOutOfGas, AuthTupleOutOfGas, OpStackIntrinsicOutOfGas }`
- `struct IntrinsicGasPolicy { mode, authorizationListPresent, authTupleCount, accessList, web3TypedTxKind }`
- `struct DebitIntrinsicGasOutcome { ok, failure, gasLeftOnFailure, debitAmount }`
- `DebitIntrinsicGasOutcome debitIntrinsicGas(evmc_message& message, IntrinsicGasPolicy const& policy)`

**Behavior by mode:**

| Mode | Behavior |
| --- | --- |
| `None` | Return ok, no mutation, `debitAmount=0` |
| `AuthOnly` | If auth list present with count > 0, require and subtract auth cost only; fail `AuthTupleOutOfGas` |
| `Eip7623` | `gasLimitMinimumWithAuth` → `calldataGas` checks (same order as `ExecuteViaEth.cpp` L110–144); subtract `preExecutionDebit + authCost`; structured failures only |
| `OpStackEntry` | `preExecutionDebit + authCost`; fail `OpStackIntrinsicOutOfGas` if insufficient; subtract on success; **no** `executeEntryFloorDataGasCheck` |

Does **not** construct `EVMCResult` on failure — returns structured outcome for `mapIntrinsicFailure` hook (Task 2+).

---

## TDD evidence

### Step 1 — Failing tests written

Created `DebitIntrinsicGasTest.cpp` with four cases exactly per brief:
- `none_mode_does_not_debit`
- `auth_only_mode_debits_auth_cost`
- `eip7623_mode_reports_structured_calldata_failure`
- `opstack_entry_mode_debits_intrinsic_without_floor_mapping`

### Step 2 — Expected RED (headers missing)

Before implementation: target would not exist. Confirmed pre-implementation state.

### Step 5 — Build/run attempt

```bash
cmake ..                          # reconfigure to pick up new target — OK
cmake --build build --target DebitIntrinsicGasTest  # COMPILE FAIL (see below)
ctest --test-dir build -R "^DebitIntrinsicGas$"     # not reached
```

**Expected GREEN after compile fix:** All four test cases should pass — logic mirrors existing inline code in `ExecuteViaEth.cpp` (Eip7623/AuthOnly) and `OpStackExecuteViaHost.cpp` `computeIntrinsicGasDebit` (OpStackEntry).

---

## Self-review against brief

| Requirement | Status |
| --- | --- |
| `adoptEvmcResult.h` matches brief snippet | ✅ |
| All enums/structs/interfaces per brief | ✅ |
| `None` / `AuthOnly` / `Eip7623` / `OpStackEntry` semantics | ✅ |
| `debitIntrinsicGas` does not construct `EVMCResult` | ✅ |
| `OpStackEntry` skips floor checks | ✅ |
| `eth/` includes only `eth/` headers (no `bcos/`/`opstack/`) | ✅ |
| Test file matches brief | ✅ |
| CMake registration matches brief | ✅ |
| Tests GREEN | ❌ blocked by compile error (deferred) |

---

## Compile issues deferred

Per user **compile deferral** directive — not fixed in this task; unified compile-fix pass expected after all orchestration tasks.

1. **`DebitIntrinsicGasTest.cpp` L39 — `bytesConstRef` construction**
   - Brief/test uses: `bcos::bytesConstRef(calldata)` where `calldata` is `bcos::bytes`
   - Codebase convention (e.g. `Eip7623PrecheckTest.cpp` L47): `bcos::bytesConstRef(&calldata)`
   - Error: no viable conversion from `vector<unsigned char>` to `RefDataContainer<const unsigned char>`
   - **Fix:** Change to `bcos::bytesConstRef(&calldata)` in one-line fix during compile-fix pass

2. **CMake reconfigure required** — new test target needs `cmake ..` before first build (done once; documented for CI).

No library CMake changes needed — headers are header-only under existing `bcos-evm-eth` include paths.

---

## Concerns

- Test compile blocked by brief's `bytesConstRef(calldata)` vs codebase `bytesConstRef(&calldata)` — trivial one-line fix deferred per directive.
- No additional unit tests for `GasLimitMinimum`, `AuthTupleOutOfGas`, or `OpStackIntrinsicOutOfGas` failure paths — brief only specified four happy/one-failure cases; additional coverage can land in Task 2 pipeline tests.

---

## Next steps (Task 1B+)

- Fix `bytesConstRef` in test during compile-fix pass → GREEN `DebitIntrinsicGas`
- Task 1B: OpStack `preDebitEntry` + message sync fix
- Task 2: Wire `debitIntrinsicGas` into `runOrchestration` + replace inline debit blocks in `executeVia*`

---

## Review fix (Important — post b868cc708)

**Issue:** `eip7623_mode_reports_structured_calldata_failure` set `message.gas = calldataGas - 1` (39 for 1-byte calldata) but expected `CalldataOutOfGas`. `debitIntrinsicGas` Eip7623 mode checks `gasLimitMinimumWithAuth` before `calldataGas`; with gas=39 vs minimum≈21040, failure is `GasLimitMinimum`.

**Changes:**
1. Renamed test → `eip7623_mode_reports_structured_gas_limit_minimum_failure`; expected failure → `GasLimitMinimum`.
2. Fixed `bytesConstRef(&calldata)` compile issue.
3. Removed unreachable `return outcome` after switch in `debitIntrinsicGas.h`.

**CalldataOutOfGas test skipped:** For any calldata, `gasLimitMinimumWithAuth ≥ 21000 + calldataGas` while the second check is `message.gas < calldataGas`. No single `message.gas` can satisfy `gas ≥ gasLimitMinimumWithAuth` and `gas < calldataGas`. The `CalldataOutOfGas` branch mirrors `ExecuteViaEth.cpp` ordering for pipeline mapping but is unreachable in isolated unit tests; coverage deferred to Task 2 integration tests if `input.message.gas ≠ message.gas` paths exist.

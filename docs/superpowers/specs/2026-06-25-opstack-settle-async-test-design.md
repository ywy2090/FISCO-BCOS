# OpStack settle* Async Layer — Unit Test Design

**Date:** 2026-06-25  
**Status:** Implemented  
**Related:** ADR-021 · `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md` §7.4  
**Scope:** Test-only PR — zero production code changes

---

## 1. Problem

ADR-021 §2.2 defines two settlement layers:

| Layer | Functions | Test surface today |
| --- | --- | --- |
| Sync | `finalizeNormal`, `finalizeDeposit` | ✅ `OpStackSettlementTest`, `OpStackDepositSettlementTest` |
| Async | `settleNormal`, `settleDeposit` | ❌ No covering tests (codegraph flagged) |

The async layer is the **bridge-only** post-pipeline entry:

```text
settleNormal:  finalizeNormal → refundGas → gasPool.returnGas → return settled
settleDeposit: finalizeDeposit → gasPool.returnGas → return settled
```

Without direct tests, wiring bugs (wrong argument order, stale `feeCtx` gas fields, skipped `refundGas`) only surface in integration tests.

---

## 2. Goal

Add a dedicated async-layer test module that closes the ADR-021 interface gap with **14 unit tests** (8 `settleNormal` + 6 `settleDeposit`), using:

- **`GasPoolSpy`** — captures `returnGas(remaining, used)` arguments
- **Balance oracle** — proves `refundGas` ran with the same effect as manual `finalizeNormal` + `refundGas`

No changes to `opstack/` production sources.

---

## 3. Non-Goals

- Re-testing `finalize*` gas math (covered by existing sync tests)
- Re-testing `refundGas` coinbase/L1/operator routing (covered by `OpStackTxFeeLedgerCtxTest`)
- Full `opStackExecute` E2E (covered by `OpStackSettlementCharacterizationTest` + deposit integration tests)
- Production test seams or `BCOS_EVM_TESTING` hooks in `OpStackSettlement.cpp`

---

## 4. Architecture

### 4.1 New files

| File | Role |
| --- | --- |
| `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp` | All 14 async matrix cases |
| `bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h` | `GasPoolSpy`, ctx/feeCtx builders, balance oracle helpers |

Existing sync tests remain unchanged:

- `OpStackSettlementTest.cpp` — `finalizeNormal` only
- `OpStackDepositSettlementTest.cpp` — `finalizeDeposit` only

### 4.2 Test harness

```cpp
struct GasPoolSpy {
    int returnGasCallCount{0};
    uint64_t lastRemaining{0};
    uint64_t lastUsed{0};
    GasPoolHooks hooks();
};
```

**`refundGas` verification (no prod seam):**

1. `buyGas(ctx, feeCtx)` to establish debited balances
2. Snapshot recipient balances (sender, coinbase, fee recipients)
3. `task::syncWait(settleNormal(...))`
4. Clone ctx → run `finalizeNormal` + `refundGas` on clone → compare balances
5. Assert `GasPoolSpy` received `(settled.gasRemaining, settled.gasUsed)`

**Dependencies:** `bcos-evm-op`, `bcos-task/Wait.h`, `state/InMemoryEvmStateReader.h`

### 4.3 CMake

Register in `bcos-evm/test/cmake/OpStackTests.cmake`:

```cmake
add_executable(OpStackSettleAsyncTest opstack/OpStackSettleAsyncTest.cpp)
target_include_directories(OpStackSettleAsyncTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackSettleAsyncTest PRIVATE bcos-evm-op)
add_test(NAME OpStackSettleAsync COMMAND OpStackSettleAsyncTest)
```

---

## 5. Test Matrix

### 5.1 `settleNormal` (8 cases)

| ID | Case name | Setup | Assertions |
| --- | --- | --- | --- |
| N1 | `settle_normal_completed_wires_refund_and_gas_pool` | `exitKind=Completed`, gas 100k→80k left, `buyGas` | Spy args = finalize oracle; balances match manual refund; return value = oracle |
| N2 | `settle_normal_rules_rejected_wires_partial_refund` | `exitKind=RulesRejected`, `EVMC_REVERT`, 60k left | Same as N1; `gasUsed` > 0 |
| N3 | `settle_normal_exception_handled_wires_partial_refund` | `exitKind=ExceptionHandled`, same evmc as N2 | Same as N2 |
| N4 | `settle_normal_intrinsic_reject_return_gas_full_limit` | `exitKind=IntrinsicRejected` | Spy: `(originalGasLimit, 0)`; return value gasUsed=0 |
| N5 | `settle_normal_gas_afford_reject_return_gas_full_limit` | `exitKind=GasAffordRejected` | Same as N4 |
| N6 | `settle_normal_null_return_gas_hook_no_crash` | Empty `GasPoolHooks{}` | No throw; return value correct |
| N7 | `settle_normal_call_frame_skips_refund_routing` | `m_call=true`, `m_skipTransactionChecks=true`, `m_noBaseFee=true`, tip/fee=0 | Return correct; sender balance unchanged by refund step |
| N8 | `settle_normal_deposit_flag_skips_refund_routing` | `feeCtx.m_isDepositTx=true` (defensive) | `refundGas` no-op inside ledger; spy still gets finalize output |

### 5.2 `settleDeposit` (6 cases)

| ID | Case name | Setup | Assertions |
| --- | --- | --- | --- |
| D1 | `settle_deposit_success_commits_and_returns_gas_pool` | checkpoint + balance bump; `Completed`+`EVMC_SUCCESS` | Spy args = finalize oracle; nonce+1; checkpoint cleared; balance committed |
| D2 | `settle_deposit_revert_returns_actual_gas` | checkpoint + balance bump; `Completed`+`EVMC_REVERT` | `gasUsed` < gasLimit; reverted; nonce+1; spy args correct |
| D3 | `settle_deposit_entry_failure_uses_gas_limit` | checkpoint; `IntrinsicRejected` | Spy: `(0, originalGasLimit)`; reverted; nonce+1 |
| D4 | `settle_deposit_gas_afford_reject_entry_failure` | checkpoint; `GasAffordRejected` | Same as D3 |
| D5 | `settle_deposit_null_return_gas_hook_no_crash` | Empty `GasPoolHooks{}` | No throw; finalize side effects preserved |
| D6 | `settle_deposit_exception_handled_entry_failure` | checkpoint; `ExceptionHandled` | Same non-Completed track as D3 |

---

## 6. Invariants Under Test

1. `settleNormal` always calls `refundGas` after `finalizeNormal` (ledger may no-op internally)
2. `settleDeposit` never calls `refundGas` or `buyGas`
3. `gasPool.returnGas(remaining, used)` arguments come from **`OpStackSettlementResult`**, not `OpStackFeeContext` gas fields
4. Return value of `settle*` equals `finalize*` output for the same inputs

---

## 7. Regression Gate

All must pass after implementation:

- `OpStackSettleAsync` (new)
- `OpStackSettlement`
- `OpStackDepositSettlement`
- `OpStackTxFeeLedgerCtx`
- `OpStackSettlementCharacterization`

---

## 8. Documentation Updates

| File | Change |
| --- | --- |
| `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md` §7.4 | Mark `settle*` unit tests **Implemented** (was optional) |
| `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` | Add test coverage row for async layer |

---

## 9. Risks

| Risk | Mitigation |
| --- | --- |
| Balance oracle flaky due to shared mutable state | Clone ctx for manual refund path; or sequential assert on same ctx with known starting balances |
| N7/N8 refund skip paths hard to observe | Assert sender balance delta == 0 after settle vs before refund would-have-run |
| Overlap with `OpStackTxFeeLedgerCtxTest` | Async tests assert **wiring + spy args**; ledger test keeps **routing detail** |

---

## 10. Implementation Plan

See `docs/superpowers/plans/2026-06-25-opstack-settle-async-test.md` (to be created via writing-plans after spec approval).

Estimated steps:

1. Add `OpStackSettleTestHelpers.h`
2. Add `OpStackSettleAsyncTest.cpp` with 14 cases
3. Register CMake target
4. Run regression gate
5. Update PR2 spec §7.4 + ADR-021 test table

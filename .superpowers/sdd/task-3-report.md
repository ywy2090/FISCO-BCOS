# Task 3 Report: Wire `refundGas` adapter

## Status

**PASS** — all deliverables complete.

## Commit

`346719e5041764c5e1c8e80698f8ae9290606ddd` — `feat(opstack): delegate refundGas to OpStackPostSettlementPlan`

## Test summary

| Test | Result | Details |
|------|--------|---------|
| `OpStackTxFeeLedgerCtxTest` | **PASS** | 4 test cases, balance routing unchanged |
| `OpStackPostSettlementCharacterizationTest` | **PASS** | 9 test cases (2 review additions) |

```bash
cd build
cmake --build . --target OpStackTxFeeLedgerCtxTest OpStackPostSettlementCharacterizationTest -j$(sysctl -n hw.ncpu)
./bcos-evm/test/OpStackTxFeeLedgerCtxTest
./bcos-evm/test/OpStackPostSettlementCharacterizationTest
```

## Deliverables

| Item | Result |
|------|--------|
| `refundGas` returns `OpStackPostSettlementPlan`; delegates to `planOpStackPostSettlement` | Done |
| `refundIsthmusOperatorCost` deleted from header and `.cpp` | Done |
| `OpStackTxFeeLedgerCtxTest` captures return value | Done |
| `OpStackPostSettlementPlan.cpp` added to `OpStackIntrinsicGasSyncTest` sources | Done |
| Review: `operator_at_limit_no_sender_refund` | Added |
| Review: `l1_hook_not_invoked_on_post` | Added |

## Build fix (Task 2 carry-over)

`OpStackPostSettlementInputs.h`: `gasRemaining` narrowed from `uint64_t` → `int64_t` via `static_cast` (required once mapper is included from `OpStackTxFeeLedger.cpp`).

## Follow-up (out of scope)

- `RefundIsthmusTest.cpp` still calls deleted `refundIsthmusOperatorCost`; ADR-026 mandates delete/port to characterization (Task 4 / cleanup).
- Task 4: wire `settleNormal` + `projectNormalReceiptMeta` to consume returned plan.

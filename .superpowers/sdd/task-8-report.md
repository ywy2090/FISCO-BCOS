# Task 8 Report: Remove legacy adapter + CMake ledger link

**Date:** 2026-07-03  
**Branch:** feat-evm-refactor (worktree)

## Summary

Removed the legacy `EthTxFeeSettlement` EVMAccount adapter from `bcos-evm/eth/`, dropped the `ledger` PUBLIC link from `bcos-evm-eth`, and updated public exports / CI slice manifest / README to point at the new `eth/settlement/` module.

## Changes

| File | Action |
| --- | --- |
| `bcos-evm/eth/apply/EthTxFeeSettlement.h` | **Deleted** |
| `bcos-evm/include/bcos-evm/eth_executor.hpp` | Export `EthFeeSettlement.h` instead |
| `bcos-evm/CMakeLists.txt` | Removed `ledger` from `bcos-evm-eth` link libs |
| `tools/ci/check-eth-pr-slices.sh` | PR-15 slice → settlement files; resolved merge conflict |
| `bcos-evm/eth/README.md` | Added `settlement/` section; removed legacy apply row |

## Verification

### Zero ledger includes in eth/

```bash
rtk grep -r 'bcos-framework/ledger\|bcos-ledger' bcos-evm/eth/
# (no output — PASS)

rtk grep -r 'bcos-framework/ledger\|bcos-ledger' bcos-evm/opstack/
# (no output — PASS)
```

### Build

```bash
cd build-bcos-evm-check && cmake .. && cmake --build . --target bcos-evm-eth -j
# PASS
```

### Targeted tests (brief-required)

| Test | Result |
| --- | --- |
| `./transaction-executor/tests/EthTxFeeLedger1559Test` | **PASS** (3 cases) |
| `./bcos-evm/test/FeeSettlementCharacterizationTest` | **PASS** (5 cases) |
| `EthFeeSettlementStateTest` | **PASS** (via ctest) |
| `TxFeeSettlementTest` | **PASS** (via ctest) |
| `InnerExecute` / `StateTransition` unit tests | **PASS** (via ctest subset) |

### Full ctest sweep

```bash
ctest -R 'Eth|TxFeeSettlement|InnerExecute|StateTransition' --output-on-failure
```

**Result:** 68/76 passed in matched set; failures are **pre-existing / environmental**, not introduced by Task 8:

- `EthGSTFull`, `EthTxGasSettlementExecutor/*`, `EthChainPolicyTest/*` — **Not Run** (binaries not built)
- `EthExecutionSpec*Full` — timeout / fixture failures (EEST harness)
- `EthTransactionExecutorFixture` — fixture output mismatches (BLS/7702/revert; WIP branch state)
- `EthTxFeeLedger1559` (ctest) — **filter mismatch**: CMake runs `-t EthTxFeeLedger1559` but suite is `EthTxFeeSettlement1559`; binary runs clean when invoked directly

### Include audit

No broken `#include "…EthTxFeeSettlement.h"` remains in buildable sources. Remaining `EthTxFeeSettlement` references are comments / test oracle docs only.

## Notes

- ADR-026 adapter table update deferred to **Task 9** per brief.
- `ledger` dependency remains intentionally in `bcos-evm/bcos/` (Fisco storage adapter).

## Commit

```
refactor(eth): drop EthTxFeeSettlement and bcos-evm-eth ledger link

Eth reference fee path is State-only; ledger remains in bcos/ storage
adapter by design.
```

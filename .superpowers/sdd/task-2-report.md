# Task 2 Report — EthFixtureAdapter + FixtureAssert

**Status:** Done  
**Commit:** `66b8839cd4d85654508b5a076c20ea32be17f170`  
**Message:** `feat(test): add EthFixtureAdapter and fixture assertion helpers`

## Summary

Added two header-only helpers under `bcos-evm/test/fixtures/` for T-09 `executeViaEth` fixture validation:

1. **`EthFixtureAdapter.h`**
   - `makePragueRevisionConfig()` — inline Prague `RevisionConfig` matching `EthPolicy::computeRevisionConfig` for block ≥ 22M (`EVMC_PRAGUE`, `warm_access`, Cancun EIPs, `eip2537`, `eip7623`, `calldata_floor_per_token=10`).
   - `buildExecuteViaEthInput()` — maps `FixtureCase` → `ExecuteViaEthInput`:
     - `tx.to` present → `EVMC_CALL`; absent → `EVMC_CREATE`
     - `evmc_message` from `fixture.tx` + `fixture.txProps.isStatic`
     - `blockInfo` from `fixture.block`
     - empty `blockHashes` lambda (same as `PragueStateTest`)
     - `gasPrice` from `fixture.tx.gasPrice`

2. **`FixtureAssert.h`**
   - `assertFixtureResult()` — compares `evmcResult.status`, output bytes (`sameBytes`), logs count, and gas used with tolerance when `expected.gasUsed != 0`.

## Files Created

| File | Purpose |
|------|---------|
| `bcos-evm/test/fixtures/EthFixtureAdapter.h` | Fixture → `ExecuteViaEthInput` adapter |
| `bcos-evm/test/fixtures/FixtureAssert.h` | Shared result assertions |

## Verification

| Check | Result |
|-------|--------|
| Header compile (via temporary `#include` in `PragueStateTest.cpp`) | PASS |
| Dedicated test target | N/A — Task 2 is headers only; Task 3 adds `ExecuteViaEthFixtureTest` |
| Runtime tests | N/A |

## Concerns / Notes

1. **`EthPolicy.h` not included** — Including `EthPolicy.h` pulls in `protocol::BlockHeader`, which is unavailable in lightweight test TU link contexts. Prague flags are inlined from `RevisionConfig.h` instead; values mirror `EthPolicy::computeRevisionConfig` for Prague.
2. **`eip7702` default false** — `EthPolicy` does not set `eip7702`; flag left at default. EIP-7702 fixture cases (Task 4+) may need explicit config extension.
3. **Gas assertion vs EIP-7623** — `assertFixtureResult` uses `gasBefore - gas_left`. Callers should pass `fixture.tx.gasLimit` as `gasBefore`; EIP-7623 intrinsic deduction inside `executeViaEth` may require non-zero `gas_used_tolerance` on imported fixtures.
4. **`evmc_message` data lifetime** — `input.message.input_data` points into `fixture.tx.data`; caller must keep `fixture` alive for the duration of `executeViaEth`.

## Next (Task 3)

- `ExecuteViaEthFixtureTest.cpp` consuming these headers
- CMake target + ctest registration

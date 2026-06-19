# Task 3 Report: 嵌套 CALL 失败测试（TDD RED）

**Status:** ✅ Complete (RED phase)  
**Date:** 2026-06-19  
**Depends on:** Task 2 (`fd02f558d` — EthHost holds `vm` + `blockHashes`; `call()` still stub)

## Summary

Added `NestedCallHostTest` exercising a two-contract nested CALL through `vm.execute(host, …)` with `EthHost` directly (no `CompatHostShim`). The test **fails as expected** because `EthHost::call()` is still a stub that returns `EVMC_SUCCESS` without recursively executing callee bytecode.

## Test Design

| Role | Address | Bytecode behavior |
|------|---------|-------------------|
| Runner | `0x01` | `CALL` → `0x02`, then `RETURN` 32 bytes from memory |
| Callee | `0x02` | `MSTORE8` `0x42` at memory[0], `RETURN` 1 byte |

**Assertion:** top-level `vm.execute` returns `EVMC_SUCCESS` and returndata contains byte `0x42`.

**Build:** standalone target (PragueStateTest pattern) — links eth state sources directly, avoiding `bcos-evm` lib compile break from `ExecuteViaHost`.

## Files

| File | Change |
|------|--------|
| `bcos-evm/test/state/NestedCallHostTest.cpp` | New nested CALL test |
| `bcos-evm/test/CMakeLists.txt` | Register `NestedCallHostTest` target + CTest entry |

## TDD RED Evidence

```bash
ctest --test-dir build/bcos-evm/test -R NestedCallHost --output-on-failure
```

```
1/1 Test #10: NestedCallHost ...................***Failed    0.24 sec
Running 1 test case...
NestedCallHostTest.cpp:101: error: in "NestedCallHostTest/runner_call_callee_returns_0x42":
  returndata should contain 0x42 from nested CALL, actual=0x

*** 1 failure is detected in the test module "NestedCallHostTest"
```

**Interpretation:**

- `result.status_code == EVMC_SUCCESS` — **PASS** (runner bytecode executes; stub `call()` returns success)
- `returndata contains 0x42` — **FAIL** (stub does not run callee; memory/returndata stays empty)

This is the intended RED gate for Task 4 (recursive `call()` via `m_vm`).

## Next (Task 4)

Implement `EthHost::call()` to execute nested messages with `m_vm.execute`, propagating callee returndata into caller memory. `NestedCallHost` should turn GREEN.

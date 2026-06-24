# Task 4 Report: Slim OpStackExecuteViaHost.cpp

**Status:** DONE  
**Commit:** `f6ed18976`  
**Depends on:** Task 3 (`8250c6d23`)

---

## Files changed

| File | Action |
| --- | --- |
| `bcos-evm/opstack/OpStackExecuteViaHost.cpp` | Modified — delegate hooks to profile; remove inline `buildHooks` / `applySettlement` |
| `bcos-evm/opstack/OpStackOrchestrationProfile.h` | Modified — expose `applySettlement` for early-exit path |
| `bcos-evm/opstack/OpStackOrchestrationProfile.cpp` | Modified — promote `applyOpStackSettlement` to public `applySettlement` |
| `bcos-evm/test/CMakeLists.txt` | Modified — add profile source to `OpStackIntrinsicGasSyncTest` link set |

---

## Implementation summary

Replaced local orchestration wiring in `OpStackExecuteViaHost.cpp` with the shared profile pattern (deposit + normal paths):

```cpp
OpStackOrchestrationProfile::Session session{input, txData};
auto hooks = OpStackOrchestrationProfile::buildHooks(session);
// TODO: OrchestrationErrorPolicy (candidate 4)
hooks.mapIntrinsicFailure = [](OrchestrationContext& c, IntrinsicDebitFailure) {
    c.evmcResult = makeOutOfGasLimitResult();
};
hooks.mapException = [](OrchestrationContext& c, std::exception_ptr) {
    c.evmcResult = makeInternalErrorResult();
};
runOrchestration(ctx, hooks);
```

| Removed from ExecuteViaHost | Moved to |
| --- | --- |
| `applySettlement` lambda (~17 lines) | `OpStackOrchestrationProfile::applySettlement` |
| `buildHooks` lambda (~43 lines) | `OpStackOrchestrationProfile::buildHooks` |
| Includes: `OpStackGasSettlement.h`, `OpStackPreDebitEntry.h`, test hook | Profile / internals |

**Early-exit settlement:** Normal-path wrapper still calls `OpStackOrchestrationProfile::applySettlement(session, output.evmcResult)` when `exitKind != KernelCompleted` and not intrinsic/pre-debit rejection (preserves L218 behavior).

**CMake fix:** `OpStackIntrinsicGasSyncTest` compiles `OpStackExecuteViaHost.cpp` directly (not via `bcos-evm-op`); added `OpStackOrchestrationProfile.cpp` to its source list.

---

## Build & test evidence

```bash
cmake --build build --target bcos-evm-op OpStackOrchestrationProfileTest OpStackExecuteViaHostSmokeTest OpStackIntrinsicGasSyncTest -j$(sysctl -n hw.ncpu)
ctest --test-dir build -R 'OpStackOrchestrationProfile|OpStackExecuteViaHost|OpStackIntrinsicGasSync' --output-on-failure
```

**Result:** ✅ GREEN — 3/3 ctest entries pass

| Test | Result |
| --- | --- |
| `OpStackExecuteViaHost` | ✅ Passed |
| `OpStackIntrinsicGasSync` | ✅ Passed |
| `OpStackOrchestrationProfile` | ✅ Passed |

---

## Self-review against brief

| Requirement | Status |
| --- | --- |
| Deposit path uses profile + error hooks | ✅ |
| Normal path uses profile + error hooks | ✅ |
| Remove duplicate `buildHooks` / `applySettlement` | ✅ |
| Early-exit `applySettlement` preserved | ✅ via `applySettlement` static |
| `OpStackOrchestrationInternals.h` for error mappers | ✅ |
| Include `OpStackOrchestrationProfile.h` | ✅ |
| Build + ctest trio | ✅ |
| Commit message | ✅ |

---

## Notes

- `mapIntrinsicFailure` / `mapException` remain at call site with `OrchestrationErrorPolicy` TODO (candidate 4), matching Fisco `ExecuteViaHost.cpp` pattern.
- Net reduction: ~50 lines removed from `OpStackExecuteViaHost.cpp` (294 → 243 lines).

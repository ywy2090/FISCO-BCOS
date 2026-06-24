# Task 4 Report: Slim ExecuteViaEth → EthOrchestrationProfile

**Status:** DONE  
**Commit:** (pending) `refactor(bcos-evm): executeViaEth delegates hooks to EthOrchestrationProfile`  
**Depends on:** Task 3 (`7a743b390`)

---

## Files changed

| File | Action |
| --- | --- |
| `bcos-evm/eth/ExecuteViaEth.cpp` | Modified — delegate chain hooks to `EthOrchestrationProfile::buildHooks` |

---

## Migration summary

Replaced inline hook wiring (former L69–128) with profile delegation, mirroring `OpStackExecuteViaHost.cpp`:

```cpp
EthOrchestrationProfile::Session session{input, output};
auto hooks = EthOrchestrationProfile::buildHooks(session);
// TODO: OrchestrationErrorPolicy (candidate 4)
hooks.mapIntrinsicFailure = …;
hooks.mapException = …;
runOrchestration(ctx, hooks);
```

| Hook | Location after Task 4 |
| --- | --- |
| `preExecute` | `EthOrchestrationProfile::buildHooks` |
| `intrinsicPolicy` | `EthOrchestrationProfile::buildHooks` |
| `preKernel` | `EthOrchestrationProfile::buildHooks` |
| `postAdopt` | `EthOrchestrationProfile::buildHooks` |
| `mapIntrinsicFailure` | `ExecuteViaEth.cpp` wrapper (TODO: OrchestrationErrorPolicy) |
| `mapException` | `ExecuteViaEth.cpp` wrapper (TODO: OrchestrationErrorPolicy) |

**Removed includes** (now only used by profile): `Eip7702.h`, `ExecuteViaEthPreCheck.h`, `Transfer.h`, `gas/Eip1559.h`, `orchestration/NormalizeIncludedTxVmerr.h`.

**Retained includes:** `EthOrchestrationProfile.h`, `OrchestrationPipeline.h`, `EthHostExtension.h`, `HashUtils.hpp` (convertLogs), `EvmTrace.h`, `Exceptions.h` (mapException).

Zero semantic change — wrapper still owns error mappers and EIP-7623 snapshot gating via `hooks.intrinsicPolicy.mode`.

---

## Build & test evidence

```bash
cmake --build build --target EthOrchestrationProfileTest ExecuteMessageSmokeTest -j$(sysctl -n hw.ncpu)
ctest --test-dir build -R 'EthOrchestrationProfile|ExecuteMessageSmoke' --output-on-failure
```

**Compile:** ✅ Built `EthOrchestrationProfileTest`, `ExecuteMessageSmokeTest`

**Run (GREEN):**

```
Test #151: ExecuteMessageSmoke ..............   Passed    0.92 sec
Test #203: EthOrchestrationProfile ..........   Passed    0.47 sec
100% tests passed, 0 tests failed out of 2
```

---

## Self-review against brief

| Requirement | Status |
| --- | --- |
| `#include EthOrchestrationProfile.h` | ✅ |
| Replace inline hooks with Session + buildHooks | ✅ |
| Keep mapIntrinsicFailure / mapException in wrapper with TODO | ✅ |
| Remove unused includes | ✅ |
| Zero semantic change | ✅ |
| Mirror OpStackExecuteViaHost pattern | ✅ |
| Smoke tests GREEN | ✅ |
| Commit message | ✅ |

---

## Notes for follow-up

1. **OrchestrationErrorPolicy (candidate 4):** migrate `mapIntrinsicFailure` / `mapException` from Eth/OpStack/Fisco wrappers into shared policy.
2. P3 Eth profile wiring complete — all chain hooks now live in `EthOrchestrationProfile.cpp`.

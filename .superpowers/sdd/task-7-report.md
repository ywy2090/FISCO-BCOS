# Task 7 (P6) Report — Doc sweep + test rename

**Baseline:** `2bad353bb` (HEAD at start)  
**Commit:** `068d8ca7b` — `chore(bcos-evm): sync architecture docs and deductIntrinsicGas test naming`

## 1. architecture-overview.md — mermaid + §3.1

Updated stale Tier-E execution symbols to ADR-029/030 canonical names:

| Location | Before | After |
|----------|--------|-------|
| Mermaid kernel nodes | `TxExecutionRunner::run`, `runExecutionFrame()` | `runEvmKernelTopLevel`, `runCallFrame()` |
| §2 ASCII kernel box | `TxExecutionRunner`, `runExecutionFrame()` | `runEvmKernelTopLevel`, `runCallFrame()` |
| §3 kernel entry diagram | `TxExecutionRunner::run`, `runExecutionFrame` | `runEvmKernelTopLevel`, `runCallFrame` |
| §3 code snippet | `TxExecutionRunner::run` | `runEvmKernelTopLevel` |
| §3.1 frame flow | same stale pair | `runEvmKernelTopLevel` → `runCallFrame` |

Prose in §3 now references `TxExecutionRunner::runEvmKernelTopLevel` and `runCallFrame` consistently.

## 2. ctest / test file rename

| Item | Before | After |
|------|--------|-------|
| ctest target | `DebitIntrinsicGas` | `DeductIntrinsicGas` |
| executable | `DebitIntrinsicGasTest` | `DeductIntrinsicGasTest` |
| source file | `test/eth/DebitIntrinsicGasTest.cpp` | `test/eth/DeductIntrinsicGasTest.cpp` |
| `BOOST_TEST_MODULE` | `DebitIntrinsicGasTest` | `DeductIntrinsicGasTest` |

Changed in `bcos-evm/test/cmake/EthTests.cmake`. Also updated test path in `eth-layer-design-review.md`.

**Note:** `DebitIntrinsicGasOutcome` type name in ADR/docs unchanged — out of P6 scope (type rename is separate Tier work).

## 3. `eth/reference/` → `eth/apply/` (docs only)

Scanned `bcos-evm/docs/` (excluding `docs/superpowers/`): **0 path references to replace.**

Prior phases already migrated directory paths. Sole remaining mention is ADR-030 checklist line documenting that `eth/reference/` must not appear in `eth/README.md` (meta, not a stale path).

## Verification

```bash
rg 'runExecutionFrame|TxExecutionRunner::run[^E]' bcos-evm/docs/architecture-overview.md  # 0 matches
rg 'DebitIntrinsicGas' bcos-evm/test/cmake/EthTests.cmake bcos-evm/test/eth/DeductIntrinsicGasTest.cpp  # 0 matches
rg 'eth/reference/' bcos-evm/docs/  # 1 match — ADR-030 checklist only
```

## Files touched

- `bcos-evm/docs/architecture-overview.md`
- `bcos-evm/docs/eth-layer-design-review.md` (test path reference)
- `bcos-evm/test/cmake/EthTests.cmake`
- `bcos-evm/test/eth/DeductIntrinsicGasTest.cpp` (renamed from `DebitIntrinsicGasTest.cpp`)

# Task C4-1 — 删除遗留 HostContext 路径

## Files to delete (bcos-evm/eth)
- `eth/vm/HostContext.h`
- `eth/HostContextPolicy.h`
- `eth/eip2929/*`
- `eth/precompiled/PrecompiledRegistrar.*` (if only used by HostContext path)
- `eth/precompiled/BuiltinPrecompiles.*` (if only HostContext)

## Files to delete (transaction-executor)
- `bcos-transaction-executor/vm/HostContext.h` (forward header)

## Modify
- `transaction-executor/**` remove HostContext references → use executeViaHost / bcos-evm APIs
- `bcos-evm/CMakeLists.txt` remove deleted sources
- Any includes pointing to deleted files

## Do NOT delete
- `bcos-executor/src/vm/HostContext.*` (Scope-A / C7+)

## Acceptance
- `rg 'HostContext|PrecompiledRegistrar|eip2929|BuiltinPrecompiles' bcos-evm transaction-executor` → no hits (except comments OK)
- `rg '#include "bcos-executor' bcos-evm/eth` → 0
- `cmake --build build --target bcos-evm` PASS
- `ctest --test-dir build/bcos-evm/test` PASS

Report: sdd/task-C4-1-report.md

# Task C4-1 Report

## Scope
- Removed legacy `HostContext` path under `bcos-evm/eth`:
  - deleted `eth/vm/HostContext.h`
  - deleted `eth/HostContextPolicy.h`
  - deleted `eth/eip2929/*`
- Removed legacy precompile registrar path in `bcos-evm/eth/precompiled`:
  - deleted `PrecompiledRegistrar.h/.cpp`
  - introduced builtin registry API in `EthBuiltinRegistry.h/.cpp`
  - rewired `PrecompiledManager` / `PrecompiledImpl` / `PrecompiledContract` to builtin registry lookup
- Updated build wiring:
  - `bcos-evm/CMakeLists.txt` and `bcos-evm/test/CMakeLists.txt` switched from registrar source to builtin registry source
- Transaction-executor side:
  - removed forward-hostcontext path usage textually and aligned vm wrapper filenames to `ExecuteFrame*`
  - updated precompiled manager lookup to builtin registry APIs

## Acceptance Checks

### 1) Legacy keyword scan
Command:

`rtk rg "HostContext|PrecompiledRegistrar|eip2929|BuiltinPrecompiles" bcos-evm transaction-executor`

Result:
- no output (exit code 1 = no match)

### 2) bcos-executor include guard in eth
Command:

`rtk rg "#include \"bcos-executor" bcos-evm/eth`

Result:
- no output (exit code 1 = no match)

### 3) Build target
Command:

`rtk cmake --build build --target bcos-evm`

Result:
- PASS (`Built target bcos-evm`)

### 4) Test suite
Command:

`rtk ctest --test-dir build/bcos-evm/test`

Result:
- PASS (`100% tests passed, 0 tests failed out of 8`)

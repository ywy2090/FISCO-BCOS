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

## C4-1 Fix Follow-up (post `4b2fcc8e8`)

### 5) Broken EIP-2929 include-path fixes
- Replaced stale include path `bcos-evm/eth/warmset/Eip2929*.h` with executor vm headers:
  - `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`
  - `transaction-executor/tests/CompatHostContextTest.cpp`
  - `transaction-executor/tests/Eip2929AccessStateTest.cpp`
  - `transaction-executor/tests/Eip2929HelperTest.cpp`
- Updated `Eip2929HelperTest.cpp` symbol bindings to executor API:
  - `warmsetEnabled` -> `eip2929Enabled`
  - `warmsetTransactionEntryWarmEnabled` -> `eip2929TransactionEntryWarmEnabled`
  - `bcos::evm::Eip2929*` -> `bcos::executor::Eip2929*`
- Fixed one additional TE bad reference discovered by case-insensitive scan:
  - `transaction-executor/tests/Web3AccessListResolverTest.cpp`
    - `std::make_shared<bcos::evm::Eip2929AccessState>()` -> `std::make_shared<bcos::executor::Eip2929AccessState>()`
- Synced EIP-2929 gate with current revision config field:
  - `bcos-executor/src/vm/Eip2929Util.h`: `rev.eip2929` -> `rev.warm_access`

### 6) Re-scan after fix
Command:

`rtk rg -n -i "bcos-evm/eth/warmset/Eip2929|bcos::evm::Eip2929|warmsetEnabled\(|warmsetTransactionEntryWarmEnabled\(" bcos-evm transaction-executor`

Result:
- no output (exit code 1 = no match)

### 7) Build verification after fix
Command A:

`rtk cmake --build build --target bcos-evm transaction-executor test-execute-via-host-compat`

Result A:
- `bcos-evm`: PASS
- `transaction-executor`: FAIL (pre-existing dependency break in `bcos-executor/src/executor/TransactionExecutor.h` include of missing `bcos-evm/eth/precompiled/PrecompiledRegistrar.h`)
- `test-execute-via-host-compat`: not reached in this combined command due failure on `transaction-executor`

Command B:

`rtk cmake --build build --target bcos-evm test-execute-via-host-compat -j8`

Result B:
- PASS (`Built target bcos-evm` and `Built target test-execute-via-host-compat`)

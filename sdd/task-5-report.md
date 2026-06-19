## Task 5 Report - EthPrecompiles::tryDispatchInCall + PrecompileInCallTest

### TDD Status
- **Verified existing API:** `EthPrecompiles::tryDispatchInCall()` already present from Task 4 (3b6ac60e1); public API in `EthPrecompiles.hpp/.cpp` wraps `dispatch()` and returns `std::optional<evmc::Result>`.
- **GREEN:** `ctest --test-dir build/bcos-evm/test -R PrecompileInCall --output-on-failure` passes.

### Implemented Changes

1. **`transition.cpp` dedupe**
   - Top-level precompile dispatch now delegates to `EthPrecompiles::tryDispatchInCall()` instead of duplicating `dispatch()` + manual receipt gas mapping.
   - Receipt fields derived via existing `resultOutputToBytes()` and `calcGasUsed()` helpers.

2. **`PrecompileInCallTest.cpp`**
   - Contract bytecode: store `0xdeadbeef` in memory, `CALL` identity precompile `0x04`, `RETURN` 32 bytes.
   - Direct `EthHost::call()` to identity precompile verifies `tryDispatchInCall` path.
   - `vm.execute()` on caller contract verifies nested precompile dispatch through real `EthHost::call()` recursion.

3. **`bcos-evm/test/CMakeLists.txt`**
   - Registered `PrecompileInCallTest` executable and `PrecompileInCall` ctest target.

### Verification
```bash
ctest --test-dir build/bcos-evm/test -R "PrecompileInCall|PragueState|NestedCallHost|Eip2929AccessHost" --output-on-failure
```
- Result: **4/4 passed**

### Commit
```
test(eth): add precompile-in-call test and dedupe dispatch
```

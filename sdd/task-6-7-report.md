## Task 6–7 Report — BlockHashHost + NestedRevertWarm

### Status
**Complete.** Both new tests build and pass via PragueState-style standalone linking (direct eth sources + evmone, not `bcos-evm` lib).

### Task 6: `BlockHashHostTest.cpp`
- Contract bytecode: `PUSH1 99` → `BLOCKHASH` → `PUSH1 0` → `SSTORE` → `STOP` (`60634060005500`).
- `BlockHashes` lambda returns known hash (`0x…cdab` in bytes[30..31]) for block 99; `txContext.block_number = 100`.
- Asserts:
  - Direct `host.get_block_hash(99)` matches expected.
  - `BLOCKHASH` opcode invokes lambda once (`blockHashCalls == 1`, `queriedBlock == 99`).
  - Stored slot 0 equals expected hash.
- Uses `EVMC_CANCUN` so `BLOCKHASH` goes through `EthHost::get_block_hash` (Prague/EIP-2935 reads history from state instead).

### Task 7: `NestedRevertWarmTest.cpp`
- Runner @0x01: `EXTCODESIZE` warms 0x03, `CALL` callee @0x02.
- Callee @0x02: warms 0x04 via `EXTCODESIZE`, then `REVERT`.
- After top-level success:
  - `state.is_address_warm(0x03)` and `host.access_account(0x03) == EVMC_ACCESS_WARM`.
  - Child-only warm 0x04 reverted: cold in both APIs.

### CMake
- Added `BlockHashHostTest` and `NestedRevertWarmTest` targets in `bcos-evm/test/CMakeLists.txt` with same source bundle as `NestedCallHostTest` / `PragueStateTest`.

### Verification
```bash
cmake --build build --target BlockHashHostTest NestedRevertWarmTest -j$(sysctl -n hw.ncpu)
ctest --test-dir build/bcos-evm/test -R "BlockHashHost|NestedRevertWarm" --output-on-failure
```
Result: **2/2 passed**.

### Commit
```
test(eth): add BlockHash and nested revert warm tests
```

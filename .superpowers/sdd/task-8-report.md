## Task 8 Report — OpStackPreCheck + DepositTx type

### Status
DONE

### Delivered
- Added `bcos-framework/bcos-framework/executor/OpStackTxType.h` with `DEPOSIT_TX_TYPE = 0x7E`.
- Added `bcos-evm/opstack/OpStackDepositTx.h` to model deposit transaction metadata.
- Added `bcos-evm/opstack/OpStackPreCheck.h/.cpp`:
  - `isDepositTx(input)` derived from `web3TypedTxKind == 0x7E` or `depositTx.has_value()`.
  - Deposit precheck path:
    - rejects system deposit (`isSystemTransaction`),
    - calls gas-pool sub hook when provided,
    - skips nonce / fee / balance checks.
  - Non-deposit precheck path:
    - nonce validation (`input.nonce` vs `state.get_nonce(sender)`),
    - EIP-1559 cap shape (`gasFeeCap >= gasTipCap` and `gasFeeCap >= baseFee`),
    - blob fee-cap check (`blobGasFeeCap >= blockBlobBaseFee` when blob hashes exist),
    - EIP-7702 auth-list shape (`CREATE + non-empty auth list` rejected).
- Updated `bcos-evm/opstack/OpStackExecuteViaHost.h`:
  - removed manual `isDepositTx` input field,
  - added derived-input fields needed by precheck (`depositTx`, `nonce`, blob/auth shape, gas pool sub hook).
- Wired `opStackPreCheck()` into `bcos-evm/opstack/OpStackExecuteViaHost.cpp` before `buyGas`.
- Updated `bcos-evm/CMakeLists.txt` to include `OpStackPreCheck.cpp`.

### Tests (TDD)
- Added `bcos-evm/test/opstack/DepositTxPreCheckTest.cpp`:
  - system deposit rejected,
  - deposit skips nonce/fee checks and still invokes gas-pool hook,
  - non-deposit nonce rejection,
  - non-deposit EIP-1559 cap rejection,
  - non-deposit blob fee-cap rejection,
  - non-deposit CREATE+auth-list shape rejection.
- Updated `bcos-evm/test/CMakeLists.txt` to register `DepositTxPreCheck`.

### Verification
```bash
rtk cmake --build build-c3-3 --target DepositTxPreCheckTest -j8
rtk ctest --test-dir build-c3-3/bcos-evm/test -R DepositTxPreCheck --output-on-failure
rtk cmake --build build-c3-3/bcos-evm/test -j8
rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure
```

### CTest Summary
- `DepositTxPreCheck`: **1/1 passed**
- full `bcos-evm` regression: **25/25 passed**

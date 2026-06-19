## Task 10 Report — L1BlockPredeploy + storage

### Status
DONE

### Delivered
- Added `bcos-evm/opstack/L1BlockStorage.h/.cpp`:
  - parses Isthmus L1 attributes calldata (`176` bytes),
  - packs slot `3` (`l1FeeScalars`) and slot `8` (`operatorFeeParams`),
  - unpacks getter values for scalar/constant selectors.
- Added `bcos-evm/opstack/L1BlockPredeploy.h/.cpp`:
  - native dispatch for `0x4200...0015`,
  - supports getter selectors:
    - `l1BaseFee()` `0x519b4bd3`
    - `baseFeeScalar()` `0xc5985918`
    - `blobBaseFeeScalar()` `0x68d5dca6`
    - `l1BlobBaseFee()` `0x84189161`
    - `operatorFeeScalar()` `0x4d5d9a2a`
    - `operatorFeeConstant()` `0x16d3bc7f`
  - supports setter selector `setL1BlockValuesIsthmus()` `0x098999be`,
  - enforces depositor-only guard (`OP_DEPOSITOR_ACCOUNT`) for setter,
  - writes fee storage slots `1/3/7/8` via `state.set_storage`.
- Updated `bcos-evm/opstack/OpHostExtension.h`:
  - injected `state::State*` through constructor,
  - routes `tryChainPrecompile` for `OP_L1_BLOCK_PREDEPLOY` to `L1BlockPredeploy::dispatch`.
- Updated `bcos-evm/opstack/OpStackExecuteViaHost.cpp`:
  - constructs `OpHostExtension extension(&state)` to enable mutable predeploy writes.
- Updated `bcos-evm/opstack/OpStackConstants.h`:
  - added `OP_DEPOSITOR_ACCOUNT`.
- Updated build wiring:
  - `bcos-evm/CMakeLists.txt`: add `L1BlockStorage.cpp` and `L1BlockPredeploy.cpp`,
  - `bcos-evm/test/CMakeLists.txt`: add `L1BlockPredeployTest`.
- Added fixture:
  - `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin` (`176` bytes).

### Tests (TDD)
- Added `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`:
  - `setter_unpacks_isthmus_fixture_into_slots`: verifies setter unpacks fixture and writes slots `1/3/7/8`,
  - `setter_rejects_non_depositor_sender`: verifies depositor-only guard,
  - `getters_return_slot_values_after_setter`: verifies all getter selectors return expected values after setter.

### Verification
```bash
rtk cmake -S . -B build-bcos-evm-check -DTESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-bcos-evm-check --target L1BlockPredeployTest -j8
rtk ctest --test-dir build-bcos-evm-check/bcos-evm/test -R L1BlockPredeploy --output-on-failure
rtk ctest --test-dir build-bcos-evm-check/bcos-evm/test --output-on-failure
```

### CTest Summary
- `L1BlockPredeploy`: **1/1 passed** (3 test cases)
- full `bcos-evm` regression: **28/28 passed**

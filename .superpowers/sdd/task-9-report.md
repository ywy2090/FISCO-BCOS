## Task 9 Report — Deposit execution path

### Status
DONE

### Delivered
- Updated `bcos-evm/opstack/OpStackExecuteViaHost.cpp` deposit branch to mirror spec §7.6 / §9 step 4:
  - apply `depositTx.mint` to sender **before** checkpoint,
  - run `state.checkpoint()` and then `executeMessage` (deposit gas price fixed to `0`),
  - on success: run `postExecuteGasSettlement` only (no §7.4 routing to `0x19/0x1A/0x1B` or coinbase),
  - on failure: `state.revert()` to deposit checkpoint, then `nonce++`, and force `gasUsed = gasLimit`.
- Kept non-deposit path behavior unchanged (`buyGas` + settlement + refund routing).

### Tests (TDD)
- Added `bcos-evm/test/opstack/DepositMintTest.cpp`:
  - `deposit_mint_is_applied_before_execution` asserts sender receives mint in deposit path.
- Added `bcos-evm/test/opstack/DepositNoFeeRoutingTest.cpp`:
  - `deposit_skips_fee_routing_recipients` asserts no balance credits to coinbase / `0x19` / `0x1A` / `0x1B`,
  - `deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` asserts failure path semantics (`revert`, `nonce++`, `gasUsed=gasLimit`, mint preserved).
- Updated `bcos-evm/test/CMakeLists.txt` to register `DepositMint` and `DepositNoFeeRouting`.

### Verification
```bash
rtk cmake -S . -B build-bcos-evm-check -DTESTS=ON
rtk cmake --build build-bcos-evm-check --target DepositMintTest DepositNoFeeRoutingTest -j8
rtk ./build-bcos-evm-check/bcos-evm/test/DepositMintTest
rtk ./build-bcos-evm-check/bcos-evm/test/DepositNoFeeRoutingTest
rtk cmake --build build-bcos-evm-check --target StateJournalRevertTest StateRefundTest EthHostExtensionHooksTest FiscoHostExtensionTest ExecuteViaHostSmokeTest ExecuteMessageSmokeTest StateHostSmokeTest WarmTransactionEntryTest Eip2929AccessHostTest SstoreStatusTest SstoreRefundTest PragueStateTest NestedCallHostTest PrecompileInCallTest BlockHashHostTest NestedRevertWarmTest EvmoneRefundSpikeTest OpStackExecuteViaHostSmokeTest DepositTxPreCheckTest DepositMintTest DepositNoFeeRoutingTest RollupCostTest OpStackFeeTest OpStackFloorGasTest CalcRefundTest RefundIsthmusTest OpStackSettlementTest -j8
rtk ctest --output-on-failure --test-dir build-bcos-evm-check/bcos-evm/test
```

### CTest Summary
- `DepositMint`: **1/1 passed**
- `DepositNoFeeRouting`: **1/1 passed** (2 test cases)
- full `bcos-evm` regression: **27/27 passed**

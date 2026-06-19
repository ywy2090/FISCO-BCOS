# Final OpStack Isthmus Fix Report (F-1 / F-2 / F-3)

## Fix scope

- F-2 (must fix): `opStackPreCheck` non-deposit path now rejects sender accounts that have code but are not EIP-7702 delegation design (`parseDelegationTarget` based).
- F-3 (must add tests): added and registered:
  - `Eip7702DelegationSenderTest`
  - `Eip7702ClearDelegationTest`
- F-1 (prefer implement): `executeEntryChecks` now performs intrinsic gas debit before EVM execution for `opStackExecuteViaHost` path (base + calldata + access list + create + auth tuple gas), preventing systematic under-reporting of `gasUsed`.

## Code changes

- `bcos-evm/opstack/OpStackPreCheck.cpp`
  - include `bcos-evm/eth/Eip7702.h`
  - add sender-code gate in non-deposit transaction checks:
    - empty code => pass
    - delegation code => pass
    - non-delegation code => `Malformed`

- `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
  - include `bcos-evm/eth/gas/EthTxGasSettlement.h`
  - add intrinsic gas calculation in `executeEntryChecks`
  - add OOG return when `availableGas < intrinsicGas`
  - subtract intrinsic gas from `txData.m_message.gas` before execution
  - wire access list / typed tx kind / auth tuple count into `txData` for intrinsic gas calculation

- `bcos-evm/opstack/OpStackTxExecutor.h`
  - extend `OpStackTxExecutionData` with intrinsic-gas context:
    - `m_accessList`
    - `m_web3TypedTxKind`
    - `m_authTupleCount`

- `bcos-evm/test/opstack/Eip7702DelegationSenderTest.cpp` (new)
  - verifies sender with delegation code passes precheck
  - verifies sender with non-delegation bytecode is rejected

- `bcos-evm/test/opstack/Eip7702ClearDelegationTest.cpp` (new)
  - verifies auth tuple with zero target clears existing delegation code and increments nonce

- `bcos-evm/test/CMakeLists.txt`
  - register `Eip7702DelegationSenderTest`
  - register `Eip7702ClearDelegationTest`

## Validation

- Full `bcos-evm` ctest:
  - `rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure`
  - result: **40/40 passed**

- Stage B merge gate (Task 15):
  - `rtk ctest --test-dir build-c3-3/bcos-evm/test -R "RollupCost|OpStackFee|RefundIsthmus|CalcRefund|OpStackFloorGas|OpStackSettlement|OpStackExecuteViaHost|DepositTxPreCheck|DepositMint|DepositNoFeeRouting|L1AttributesDeposit|L1AttributesDepositFailure|GasFeeCapBalance|BlobGasBalance|L1BlockGetter|Eip7702PreCheck|Eip7702DelegationSender|Eip7702ApplyAuthorization|Eip7702ClearDelegation|CanTransfer|IsthmusPostExecutionPolicy" --output-on-failure`
  - result: **21/21 passed**

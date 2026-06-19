## Task 4 Report - EthHost::call() recursive vm.execute()

### TDD Status
- RED: `ctest --test-dir build/bcos-evm/test -R NestedCallHost --output-on-failure` failed before implementation.
- GREEN: `./build/bcos-evm/test/NestedCallHostTest --log_level=test_suite` and
  `ctest --test-dir build/bcos-evm/test -V -R NestedCallHost --output-on-failure` both pass after implementation.

### Implemented Changes
- `EthHost::routeCall()` no longer invokes `prepareMessage`; CREATE/CREATE2 keep address alignment and now use `State::pin_warm_create_address()`.
- `EthHost::call()` now executes full strategy-A flow:
  - route and chain-precompile dispatch
  - built-in precompile dispatch via `EthPrecompiles::tryDispatchInCall()`
  - delegatecall-to-precompile gate
  - `prepareMessage()` before value transfer
  - child-frame `checkpoint()` -> `vm.execute()` -> `commit()/revert()`
- Added `EthHost::resolveExecutionCode()` helper:
  - CREATE/CREATE2 executes `msg.input_data`
  - CALL-family resolves account code from state
- Added `EthPrecompiles::tryDispatchInCall()` wrapper on top of existing `dispatch()`.

### Verification
- `ctest --test-dir build/bcos-evm/test -R "NestedCallHost|PragueState|Eip2929AccessHost|EthHostExtensionHooks|FiscoHostExtension" --output-on-failure`
  - Result: 5/5 passed.

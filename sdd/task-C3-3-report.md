# Task C3-3 Report — ExecuteViaHost compat regression harness

## Status

- Implemented `transaction-executor/tests/ExecuteViaHostCompatTest.cpp` with 3 required compat cases on `executeViaHost`.
- Wired CMake in `transaction-executor/tests/CMakeLists.txt`:
  - excluded new file from unity merge in `test-transaction-executor`,
  - added minimal standalone test target `test-execute-via-host-compat`.
- Full `test-transaction-executor` build is blocked by existing WIP compile errors in `bcos-executor`; minimal target builds and passes.

## Implemented Cases

1. **auth fail path**
   - file: `transaction-executor/tests/ExecuteViaHostCompatTest.cpp`
   - case: `auth_fail_path_returns_checker_result`
   - behavior:
     - `revisionConfig.enable_auth_check = true`
     - inject `authChecker` returning `EVMC_REVERT`
     - assert early-return status is `EVMC_REVERT`, with empty logs / empty state diff.

2. **revert logs gate (`fix_revert_logs`)**
   - file: `transaction-executor/tests/ExecuteViaHostCompatTest.cpp`
   - case: `revert_logs_fix_gate_controls_revert_logs_visibility`
   - behavior:
     - contract bytecode emits one `LOG0` then `REVERT`
     - `fix_revert_logs = false`: logs retained
     - `fix_revert_logs = true`: logs cleared
     - both runs keep `EVMC_REVERT`.

3. **empty account CALL via executeViaHost**
   - file: `transaction-executor/tests/ExecuteViaHostCompatTest.cpp`
   - case: `empty_account_call_via_execute_via_host_returns_success`
   - behavior:
     - sender funded, callee account absent/code empty
     - assert `EVMC_SUCCESS`, no logs, empty state diff.

## Build / Test Verification

Executed in this workspace:

- `cmake -S . -B build-c3-3 -DTESTS=ON`
- `cmake --build build-c3-3 --target test-transaction-executor -j8` ❌ (blocked by unrelated WIP)
- `cmake --build build-c3-3 --target test-execute-via-host-compat -j8` ✅
- `ctest --test-dir build-c3-3 -R "^ExecuteViaHostCompat$" --output-on-failure` ✅

Direct executable spot-check:

- `./build-c3-3/transaction-executor/tests/test-execute-via-host-compat --run_test=ExecuteViaHostCompatTest/auth_fail_path_returns_checker_result` ✅

## Blocker Details (non-C3-3 scope)

`test-transaction-executor` transitively builds `bcos-executor` and fails on existing signature mismatch / API drift, e.g.:

- `TransactionExecutive.cpp` VMFactory::create argument mismatch.
- `TransactionExecutive.cpp` `eip2929Enabled` call signature mismatch.
- `TransactionExecutor` constructor type mismatch (`std::shared_ptr<evm::VMFactory>` vs `std::shared_ptr<bcos::executor::VMFactory>`).

These failures are pre-existing WIP in current branch and not introduced by C3-3 harness changes.
# Task C3-3 Report — ExecuteViaHost compat regression harness

## Status

- Implemented `transaction-executor/tests/ExecuteViaHostCompatTest.cpp` with the required 3 compat regression cases on `executeViaHost`.
- Added CMake wiring in `transaction-executor/tests/CMakeLists.txt` for standalone target `test-execute-via-host-compat` and ctest case `ExecuteViaHostCompat`.
- Kept `TransactionExecutorImpl` unchanged.
- Full `test-transaction-executor` build is currently blocked by unrelated WIP errors in `bcos-executor`; standalone target is used as the minimal verification path.

## Pattern Migration

Referenced and migrated testing patterns from:

- `transaction-executor/tests/CompatHostContextTest.cpp`
  - compat-style focused regression assertions and fixture helper style.
- `bcos-executor/test/unittest/evmone/compat/CompatHostContextHarness.h`
  - lightweight harness/helper composition pattern for isolated unit checks.

## Implemented Cases

In `transaction-executor/tests/ExecuteViaHostCompatTest.cpp`:

1. `auth_fail_path_returns_checker_result`
   - auth gate via `ExecuteViaHostInput.authChecker` short-circuit.
   - verifies `EVMC_REVERT` and no side effects (`logs` and `stateDiff` are empty).
2. `revert_logs_fix_gate_controls_revert_logs_visibility`
   - bytecode emits `LOG0` then `REVERT`.
   - verifies `fix_revert_logs=false` keeps logs; `fix_revert_logs=true` clears logs.
3. `empty_account_call_via_execute_via_host_returns_success`
   - empty callee account `CALL` through `executeViaHost`.
   - verifies `EVMC_SUCCESS`, empty logs, empty state diff.

## CMake Wiring

Updated `transaction-executor/tests/CMakeLists.txt`:

- `set_source_files_properties("ExecuteViaHostCompatTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)`
- standalone executable:
  - `add_executable(test-execute-via-host-compat main.cpp ExecuteViaHostCompatTest.cpp)`
  - links: `bcos-evm`, `evmone::evmone`, `Boost::unit_test_framework`
- standalone ctest:
  - `add_test(NAME ExecuteViaHostCompat ... COMMAND test-execute-via-host-compat)`

## Verification

Executed in this workspace:

- `cmake --build build-c3-3 --target test-transaction-executor -j4` -> **failed** (unrelated WIP in `bcos-executor`)
- `cmake --build build-c3-3 --target test-execute-via-host-compat -j4` -> **passed**
- `ctest --test-dir build-c3-3 -R '^ExecuteViaHostCompat$' --output-on-failure` -> **passed (1/1)**

## Blocker Details (Out of C3-3 Scope)

`test-transaction-executor` transitive build currently fails in `bcos-executor` with existing branch-WIP mismatches, including:

- `TransactionExecutive.cpp` call-site vs `VMFactory::create` signature mismatch.
- `TransactionExecutive.cpp` `eip2929Enabled` call signature mismatch.
- `TransactionExecutor` constructor VMFactory type mismatch.

These are pre-existing WIP issues in this branch and not introduced by C3-3 harness changes.

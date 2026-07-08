# Task C3-3 — 3.18.0 回归（executeViaHost harness）

## Files
- Create: `transaction-executor/tests/ExecuteViaHostCompatTest.cpp` (migrate from `CompatHostContextTest.cpp`)
- Reference: `bcos-executor/test/unittest/evmone/compat/CompatHostContextHarness.h` (pattern only)

## Steps (plan)
1. feature flag matrix + fix_error_handling per FIB (§20.1)
2. revert logs flag OFF/ON (§19.4)
3. L-A dual path (precheck ON buyGas/refundGas; OFF consumeBalance) — may stub if harness heavy
4. Compare with release-3.18.0 snapshots / existing TE tests

## Constraints
- Use executeViaHost path, NOT old HostContext in bcos-evm/eth
- Do NOT switch TransactionExecutorImpl (C5)
- If full 3.18.0 matrix too heavy, implement minimal ported cases + document defer list in report
- Tests in transaction-executor/tests/

## Acceptance
- At least 3 compat cases ported (auth fail, revert logs, empty CALL or CREATE)
- Compiles and ctest passes for new target OR documents build blocker

Report: sdd/task-C3-3-report.md

# T-09 Eth E2E Progress

Plan: `docs/superpowers/plans/2026-06-18-t09-eth-e2e-validation.md`
Spec: `docs/superpowers/specs/2026-06-18-t09-eth-e2e-validation-design.md`
Base at start: `cf3b22f7c`

- Task 1: complete (cf3b22f7c..a0569050b, review clean)
- Task 2: complete (a0569050b..a95869e3e, review approved minor)
- Task 3: complete (a95869e3e..3426cec6a, scope creep fixed)
- Task 4: complete (3426cec6a..be80e6cc0, 10 fixtures)
- Task 5: complete (be80e6cc0..21420cc92, 15 fixtures)
- Task 6: complete (21420cc92..623e1fa78, 20 fixtures + convert script)
- Task 7: complete (ctest 2/2, spec done criteria met)
- Final review: pending

## I-1 / I-2 fix (final review important findings)

- **I-1 stRevert_revertDepth**: outer bytecode fixed — removed stray `PUSH1 0` before `GAS` so `CALL` targets `0x66`; added `PUSH1 0 PUSH1 0` before `REVERT`; expected `gas_used=2632`.
- **I-2 stCreate2_basic**: factory rewritten to `CODECOPY` + `CREATE2` (salt=0) deploying init code `602a60005260206000f3`; expected `gas_used=32030`.
- **Verify**: `./build/bcos-evm/test/ExecuteViaEthFixtureTest` — 20/20 fixtures pass.

Commits: cf3b22f7c..623e1fa78 (6 commits)

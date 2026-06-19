# SDD Progress Ledger — bcos-evm Plan C

## Completed
- C0-1: complete (09b9747, review clean)
- C0-2a: complete (8f10ea0+9c755c3, review clean)
- C0-2b: complete (6a001d30, review clean)
- C0-2c: complete (3352d043+5102bbc6, review clean)
- C0-3: complete (71f1f2a0, review clean)
- C0-4: complete (3fcbca8e, review clean)
- C1-0: complete (482d1b41, review clean)
- C1-1: complete (fbf4c07→1042ec811 fix, review clean)
- C1-2: complete (f712d49f, review clean)
- C2-1: complete (f64ad5a6, review clean)
- C2-2: complete (ccf8dd75c..bd20ef40a, review clean; minors: PragueState CMake manual sources, intrinsic gas doc)
- C3-1: complete (f712d49f4..ccf8dd75c+d875c4e6b fix, review clean; minor M1: nonce revert assertion in test)

- C3-2: complete (d875c4e6b..3d1717169+cb45c39ad fix, review clean; Step2-4 tests deferred to C3-3)

- C3-3: complete (cb45c39ad..9272d7ded, review clean; minors: report duplicate, fix_error_handling weak branch)

- C4-1: complete (9272d7ded..9200b933f incl. 22a2e42aa EIP2929 fix; bcos-evm eth HostContext/eip2929 deleted; bcos-evm 8/8 PASS; TE ExecuteFrame→C5)

- C5-1: complete (9200b933f..a608f6029, review clean)
- B1 fix: eip2929Enabled dual-param overload restored (post-final-review)
- build fix: transaction-executor + bcos-executor compile (8147b8d2b)
- C5-2: scheduler FIB101_102_103_104_SchedulerTest 10/10 PASS (Rollbackable namespace fix in FIB100 tests)
- build/test: test-transaction-executor compiles; namespace/header fixes; FIB88 cases migrated to ExecuteViaHostCompat
- fix: EVMCResult move-assign now copies `status` field (FIB88 compat regression)
- C4-2: complete — CompatHostContextTest 50/50 migrated to `CompatExecuteViaHost` + production warm-set parity
  - `warmTransactionEntry` now prewarms precompiles, gates EIP-2930 list, warms CREATE code address
  - `State`: `warm_up_*_no_journal`, `pin_warm_create_address`, pinned warmth survives revert
  - `EthHost`: CREATE frame pins contract address warmth
  - `ctest -R 'CompatExecuteViaHost'` — 50/50 PASS; `ctest --test-dir build/bcos-evm/test` — 9/9 PASS

## Status
Plan C tasks C0–C5 complete. C4-2 CompatHostContext migration complete. Acceptance gates:
- `ctest --test-dir build/bcos-evm/test` — 9/9 PASS
- `ctest -R 'CompatExecuteViaHost'` — 50/50 PASS
- `ctest -R '^ExecuteViaHostCompat$'` — PASS (incl. FIB88 error-path cases)
- `ctest -R 'FIB101_102_103_104_SchedulerTest'` — 10/10 PASS
- `test-transaction-executor` + production libs build OK

## Deferred (non-blocking)
- Legacy ExecuteFrame harness tests excluded from `test-transaction-executor` build (superseded by CompatExecuteViaHost):
  `CompatHostContextTest` (source retained), `TestHostContext`, `FIB88_92` (partial → ExecuteViaHostCompat), `Web3AccessListResolverTest`
- Per-task review minors (PragueState CMake, nonce revert assertion, report duplicate)

## Next
- Layer Refactor Step 1: complete (Task 8/9 accepted)
- Layer Refactor Step 2–4 in progress (subagent-driven)
- Task 1: complete (8147b8d..63e01c8b8, review clean) — HostExtension hook rename
- Task 2: complete (63e01c8b..fd02f558d, review approved w/ issues) — EthHost VM+BlockHashes; bcos-evm lib compile deferred to Step 2 ExecuteViaHost fix
- Task 3: complete (fd02f558d..d50bff17a, review approved w/ issues) — NestedCallHostTest TDD RED
- Task 4: complete (d50bff17a..3b6ac60e1, review clean after fix) — recursive EthHost::call + delegate gate order fix
- Optional: open PR / merge to base branch

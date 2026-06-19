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
- Layer Refactor Step 1: complete (Task 8/9, 13/13 bcos-evm tests)
- Layer Refactor Step 2 in progress
- Task 10: complete (f2bdf2e..805e6730b) — FiscoRevisionConfig split + ExecuteViaHost EthHost fix
- Task 11: complete (805e6730..7ca7eac40) — eth::executeMessage extract
- Task 12: complete (7ca7eac40..b9f47dca6) — slim ExecuteViaHost + thin transition; bcos-evm 14/14 + ExecuteViaHostCompat 7/7
- Task 13: complete (b9f47dca6..996305dd4) — FiscoConstants.h; FiscoHostExtension/ExecuteViaHost/FiscoPolicy 常量解耦；存量 precompiled/ExecutiveWrapper 仍含 bcos-executor include（Task 14 范围）
- Task 14: complete (996305dd4..62433c71d) — ExecutiveWrapper 上移 TE；删除 externalCaller stub；CompatHostShim 走 EthHost::call()；CompatExecuteViaHost 50/50 + ExecuteViaHostCompat|FIB101 11/11
- Task 15: complete (62433c71d..7578112e8 + fix) — Step 2 验收；恢复 ExecuteViaHost FISCO 薄层（balance transfer/NotFoundCode/BALANCE_TRANSFER_GAS）；bcos-evm 14/14 + CompatExecuteViaHost 50/50 + FIB101 10/10
- Task 16: complete (pending commit) — 新增 OpHostExtension + OpStackExecuteViaHost；OpStackTxExecutor 改为 State/evmc_message 记账；新增 OpStackExecuteViaHostSmokeTest（3 cases）
- Task 15 Step 2: 验收已执行（build-c3-3）
  - `bcos-evm/test`：12/14 PASS（fail: WarmTransactionEntry, NestedCallHost）
  - `CompatExecuteViaHost`：50/50 PASS
  - `ExecuteViaHostCompat|FIB101`：14/17 PASS（FIB101 10/10 PASS；fail: fib88 两个 case）
  - fix: `bcos-evm/test/CMakeLists.txt` 为 5 个 state test target 补齐 `../eth/executeMessage.cpp` 链接
- Task 16 Step 3: 验收已执行（build-c3-3）
  - `ctest -R OpStackExecuteViaHost`：1/1 PASS
  - `ctest --test-dir build-c3-3/bcos-evm/test`：15/15 PASS
- Optional: open PR / merge to base branch

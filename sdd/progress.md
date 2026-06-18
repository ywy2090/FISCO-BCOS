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

## Status
Plan C tasks C0–C5 complete. bcos-evm 8/8 + ExecuteViaHostCompat PASS.
transaction-executor + bcos-executor build unblocked (VMFactory namespace, TE compile fixes).

## Next
- Optional: address deferred minors from per-task reviews
- Optional: restore ExecuteFrame.cpp for legacy TE tests (CompatHostContextTest etc.)

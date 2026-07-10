# DIVERGENCES.md — op-geth differential gate ledger

Plan: `bcos-evm/docs/superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md` ("预注册的 gate 纪律").

**This file is the single source of truth for every exemption the replayer
(`OpStackT8nVectorReplayTest` / `T8nVectorReplayTest.cpp`) is allowed to treat as non-fatal.**
No expected/actual numeric value is ever hardcoded in the replayer's C++ — it only reads this
file's machine-parseable `ALLOWLIST` lines (see "Machine format" below) to decide whether a
mismatch it just observed is already filed here under an exempting status. An unlisted
`(vectorId, field)` mismatch, or one listed under a non-exempting status, still fails the ctest
run (`DIVERGE <vector> <field> want=<> got=<>`, red). A mismatch this file *does* cover prints
`KNOWN-DIVERGE <vector> <entry-id> field=<field> want=<> got=<>` (a Boost.Test warning, not a
failure) instead.

## Attribution (predeclared in the plan, three-way, no other option)

- **(a) `bcos-evm/opstack` defect.** A real correctness gap in production code. Filed here with
  `status=PENDING-FIX` until its own fix (and, per the plan, its own separate plan/PR — this gate
  reports, it does not repair) lands; then flip the status (or delete the entry once the vector
  goes green) in the same commit as the fix.
- **(b) generator (`opt8n`) defect.** Found → generator fixed → **the entire vector batch is
  regenerated from the fixed generator** (plan rule 3: "生成器与向量同 commit 入库；重生成必须整批") →
  the divergence should disappear entirely, so no lasting ledger entry exists for it. (Two such
  bugs were found and fixed *before* any vector was committed, so they never became DIVERGENCES.md
  entries — see "Fixed pre-commit" below for the paper trail the plan's discipline still requires.)
- **(c) accepted difference.** A real, permanent behavioral difference between the harness and
  ground truth that a human has reviewed and decided not to fix. Filed with
  `status=PENDING-USER-SIGNOFF` until a user explicitly signs off (at which point the status
  becomes `SIGNED-OFF` and it becomes exempt) — **the gate never self-authorizes an (c) entry**.

## Machine format ("ALLOWLIST" lines)

Each exempted `(vectorId, field)` pair needs one HTML-comment line (invisible when this file
renders, trivially `std::regex`-parseable by `DivergenceLedger::loadFromFile` in
`T8nVectorReplayTest.cpp`):

```
<!-- ALLOWLIST vectorId=<id> field=<field> entry=<ENTRY-ID> attribution=<a|b|c> status=<STATUS> want=<hex> got=<hex> -->
```

`field` must match the comparator's own field string exactly (`receipts[N].gasUsed`,
`blockGasUsed`, `postState.<address>.balance`, `tx[N].rawConsistency.<name>`, etc. — see
`T8nVectorReplayTest.cpp`'s `checkAccount`/`checkReceipt`/`applyEip1559Tx` for the exact strings
each check uses). Only `attribution=a status=PENDING-FIX` and `attribution=c
status=SIGNED-OFF` are treated as exempt; every other combination (including `attribution=c
status=PENDING-USER-SIGNOFF`) still fails the build — this is deliberate: a `(c)` entry must never
go green before a human has actually signed off.

`want` and `got` are **required** and are matched exactly (as the same hex string the comparator
itself would print in a `DIVERGE`/`KNOWN-DIVERGE` message: `checkU256`/`checkU64` produce
`0x`-prefixed lowercase hex with no zero-padding via `hexU256`/`hexU64`; `checkBytes`/
`checkBytes32`/`checkAddress` produce `bcos::toHexStringWithPrefix` output). They pin the
exemption to the *one specific* wrong value pair this entry was filed for, not merely to the
`(vectorId, field)` pair. `lookupExempt` (`T8nVectorReplayTest.cpp`) requires `(vectorId, field,
want, got)` to match as a four-tuple before granting the exemption — matching on `(vectorId,
field)` alone would let *any* future mismatch on that same field ride through as if it were the
already-filed one, silently absorbing a brand-new, unrelated regression. If a mismatch's `want` or
`got` ever drifts from what's on file here (the underlying defect changes shape, a new bug appears
alongside the known one, the vector is regenerated with different values, etc.), the entry no
longer matches and the build goes red again — exactly as it should, since "the value drifted" means
this is no longer proven to be *only* the already-attributed divergence.

---

## FINDING-1 (CONFIRMED, attribution a — `bcos-evm/opstack` consensus-level defect)

**Root cause** (`bcos-evm/opstack/settlement/OpStackTxFinalize.cpp:45-50`):

```cpp
void applyDepositPostExecuteSettlement(
    StateTransitionContext const& ctx, OpStackTxFinalizeResult& out)
{
    // Deposits have no Regolith floorDataGas charge.
    applyPostExecuteSettlement(ctx, 0, out);
}
```

This hardcodes `floorDataGas=0` for **every** deposit transaction, success or revert. The comment
conflates two different op-geth eras: pre-Regolith deposits really do report `gasUsed=gasLimit`
(or `0` for system txs) unconditionally, with no floor involved at all
(`core/state_transition.go:625-642`, guarded by `!rules.IsOptimismRegolith`) — but the EIP-7623
floor-adjustment block a few lines later (`core/state_transition.go:644-662`,
`if rules.IsPrague { if st.gasUsed() < floorDataGas { ... } }`) runs **unconditionally** for every
transaction once Prague-equivalent rules are active (Isthmus qualifies), deposit or not, and
post-Regolith deposits (`core/state_transition.go:681-689`) return the *already floor-adjusted*
`st.gasUsed()`. So real op-geth (pinned v1.101702.2) **does** apply the calldata floor to
post-Regolith (i.e. every Isthmus/Jovian) deposit `gasUsed`; `bcos-evm/opstack` does not, for any
deposit whose calldata's floor cost (`tokens(data) * 10`, tokens = 4×nonzero + 1×zero) exceeds its
normal intrinsic-plus-execution gas.

**Consequence**: any block containing a data-heavy deposit under Isthmus+ gets a receipt
`gasUsed`/`cumulativeGasUsed`/`blockGasUsed` that is *lower* than a real op-geth-based OP-Stack
chain would produce — a receipts-root-level consensus divergence, not merely a display quirk.
Independently predicted by the second-round spec review for `bcos-evm-ref` (spec §4.3, "Isthmus
下 7623 floor 适用 deposit 无豁免") before this gate existed; this is the machine-found production
instance of that predicted category.

**Fix direction** (recorded for the follow-up plan, not applied here — gate is report-only, per
the plan's Task 3 note "不修 opstack（FINDING #1 的修复另立 plan）"): `finalizeDeposit`'s call sites
already have `sidecar.floorDataGas` computed for the entry-side precheck
(`OpStackFloorGasPrecheck.cpp:26`); `applyDepositPostExecuteSettlement` needs to be threaded that
same value instead of the literal `0`.

**First discovered**: Task 2, on `isthmus_transfer_basic`'s L1-attributes deposit (176-byte
calldata, 18 nonzero + 158 zero bytes → floor = 2300 vs normal calldata gas 920, delta 1380 gas).

**Amplified / re-confirmed** in Task 3's wave-1 vectors — **same root cause, merged into this one
entry** (not filed as separate findings), per the plan's explicit instruction:

| Vector | calldata | floor vs normal delta | fork |
|---|---|---|---|
| `isthmus_transfer_basic` | 176B (18 nonzero + 158 zero) | 1,380 gas | Isthmus |
| `isthmus_transfer_multi_nonce` | same 176B L1-attributes calldata (tx0) | 1,380 gas | Isthmus |
| `isthmus_deposit_large_calldata` | 2,000B, all nonzero (`0xAB` × 2000) | 48,000 gas | Isthmus |
| `jovian_deposit_large_calldata` | 2,000B, all nonzero (`0xAB` × 2000) | 48,000 gas | Jovian |

Confirms the defect is fork-independent (Isthmus and Jovian both wrong the same way — expected,
since `applyDepositPostExecuteSettlement`'s `0` literal doesn't consult the fork schedule at all)
and scales with calldata weight, not just calldata length — `isthmus_deposit_large_calldata`'s
all-nonzero payload produces a ~35x larger gas delta than the original mixed-content 176-byte
vector despite only ~11x more bytes, exactly matching the floor formula's `4x` nonzero-byte
weighting.

Every affected vector's non-deposit fields (postState balances/nonces, non-deposit receipts,
`_op_l1_fee`, `_op_deposit_nonce`, `_op_deposit_receipt_version`) all pass — this is scoped
precisely to deposit `gasUsed` (and the `blockGasUsed` sum it feeds into), consistent with the
root cause above.

```
<!-- ALLOWLIST vectorId=isthmus_transfer_basic field=receipts[0].gasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x5b04 got=0x55a0 -->
<!-- ALLOWLIST vectorId=isthmus_transfer_basic field=blockGasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0xad0c got=0xa7a8 -->
<!-- ALLOWLIST vectorId=isthmus_transfer_multi_nonce field=receipts[0].gasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x5b04 got=0x55a0 -->
<!-- ALLOWLIST vectorId=isthmus_transfer_multi_nonce field=blockGasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x1511c got=0x14bb8 -->
<!-- ALLOWLIST vectorId=isthmus_deposit_large_calldata field=receipts[0].gasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x18a88 got=0xcf08 -->
<!-- ALLOWLIST vectorId=isthmus_deposit_large_calldata field=blockGasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x18a88 got=0xcf08 -->
<!-- ALLOWLIST vectorId=jovian_deposit_large_calldata field=receipts[0].gasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x18a88 got=0xcf08 -->
<!-- ALLOWLIST vectorId=jovian_deposit_large_calldata field=blockGasUsed entry=FINDING-1 attribution=a status=PENDING-FIX want=0x18a88 got=0xcf08 -->
```

---

## Fixed pre-commit (never reached this ledger, recorded for the paper trail)

Two bugs were found and fixed *before* the affected vectors/code were committed, per the plan's
rule 2/3 discipline (expected values only from a correct generator run; a bug found mid-authoring
gets fixed and everything downstream regenerated/rebuilt, not filed as a "divergence"):

1. **Vector/replayer schema mismatch on deposit `value`** (Task 3, found while authoring
   `isthmus_deposit_mint_ne_value` et al.): the generator's `inputDeposit`/`outputDepositTx` Go
   structs (`generator/main.go`) read/write a deposit's `value` field *inside* `_op_deposit`
   (parallel to `mint`, matching op-geth's `types.DepositTx`), but
   `T8nVectorReplayTest.cpp`'s `applyDepositTx` read it from the outer tx object instead — a
   location the generator never populates for deposits, so it silently defaulted to `0` on both
   sides (coincidentally never producing a visible mismatch until a vector actually needed a
   nonzero deposit `value`, which is when this was caught). Fixed in `T8nVectorReplayTest.cpp`
   (`applyDepositTx`, see its inline comment) to read `value` from `_op_deposit`, matching the
   generator; all deposit vectors were authored/regenerated after this fix, not before.
2. **`isthmus_transfer_multi_nonce` vector-authoring mistake** (not a generator or opstack bug):
   its L1-attributes deposit was first authored with an arbitrary `from` address. Real op-geth
   never enforces `L1Block.sol`'s `onlyDepositor` access control in this generator (no bytecode is
   deployed at the L1Block predeploy — see `generator/README.md`'s "L1Block calldata-parsing
   ground truth is not achievable" note below), so the generator's expected postState assumed
   success regardless of sender. `bcos-evm/opstack`'s native L1Block dispatch **does** enforce
   this check (`L1BlockPredeploy.cpp:56-65`, `NotDepositor()` revert unless
   `msg.sender == OP_DEPOSITOR_ACCOUNT = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001`) — correctly
   mirroring the real `L1Block.sol` contract that a live OP-Stack chain actually deploys. This
   surfaced as a `receipts[0].status want=0x1 got=0x0` mismatch on first replay; not filed as a
   divergence because the *vector* was wrong (arbitrary sender), not either engine — fixed by
   reauthoring the case with `from = OP_DEPOSITOR_ACCOUNT` and regenerating just that vector.

## Non-divergence known limitations (documented, not attributed — no replay ever ran)

Three wave-0/wave-1 matrix cells could not be produced as executable vectors at all, so they never
reached the replayer and carry no attribution. Full detail in `generator/README.md`'s "Known
limitations" section; summarized here for the ledger's completeness:

- **`is_system_tx=true` post-Regolith deposit** (wave-1 item #7): op-geth's own
  `stateTransition.preCheck()` returns `ErrSystemTxNotSupported` once Regolith is active (Isthmus
  config has `RegolithTime=0`), which propagates as a hard Go error out of
  `core.ApplyTransactionWithEVM` and aborts `opt8n`'s *entire* vector-file generation (no output
  JSON is written at all) — captured verbatim: `opt8n: vector
  "isthmus_deposit_system_tx_rejected": tx 0: ApplyTransactionWithEVM: system tx not supported:
  address 0xdeadDEADdeAddeadDeadDeadDeaDDEADdead0011`. The attempted case file
  (`deposit_system_tx_rejected.in.json`) is kept in the vectors directory as documentation (its
  `_info.comment` says so explicitly); it intentionally has no corresponding `.json` output and
  the replayer never sees it.
- **Pre-Canyon deposit-receipt semantics** (wave-0 seed #3): `opt8n --fork isthmus|jovian` only
  offers two fixed, fully-post-Canyon chain configs (`buildChainConfig` in `generator/main.go`);
  there is no `--canyon-time` knob to produce a pre-Canyon (`DepositReceiptVersion=nil`) vector.
  Out of scope for this task per the plan's own permissive language ("若生成器暂不支持按向量调 fork
  time，就只做 post-Canyon 并在 README 记录限制").
- **L1-attributes calldata-parsing ground truth** (wave-0 seed #2, "l1_info attributes 解析场景"):
  discovered while designing this vector, not while running it. `opt8n` never deploys `L1Block.sol`
  bytecode (values are pre-seeded directly into `pre`, by design — see
  `isthmus_transfer_basic`'s own `_info.comment`); a `CALL` to a code-less account is a pure no-op
  in real EVM semantics, so real op-geth's ground truth for *any* L1-attributes deposit's own
  effect on `L1Block`'s storage is always "unchanged", regardless of the deposit's calldata
  content. `bcos-evm/opstack`, in contrast, dispatches L1Block calls natively (calldata is always
  parsed and slots always written, independent of "code" being present). This makes a genuine
  differential test of the calldata→storage parsing logic structurally unreachable by this
  generator design: any vector built this way would either (a) have its `L1Block` account excluded
  entirely from `postState` (opt8n's diff mechanism only emits accounts whose *pre-listed* fields
  actually changed relative to `pre`, and real op-geth would report no change here), or (b), if
  `pre` were deliberately seeded to already match what the calldata implies, produce a vacuously
  "passing" comparison that doesn't actually exercise the parsing path (bcos-evm re-deriving the
  same values back is not, on its own, evidence of correct parsing since the ground truth had no
  independent way to check them). Both `isthmus_seed_deposit_receipt_post_canyon` and the wave-1
  deposit vectors instead validate the parts of deposit execution the generator *can* ground-truth
  (mint/value/nonce/receipt fields); a real test of `L1BlockStorage.cpp`'s parsing correctness
  would need a different harness (e.g. a hand-computed oracle, or compiling real `L1Block.sol`
  bytecode into the generator's prestate) — out of scope for this task.

# Overlay Storage Read-Through — Design

**Date:** 2026-07-08
**Status:** Approved (design review complete; revised after a 4-lens sub-agent review:
technical fact-check, adversarial probing, test-plan adequacy, geth-parity audit)
**Scope:** Core fix + sparse-storage-view test blind-spot governance. Related cleanups
(build_diff full-account loads, clear_storage dead work, duplicate original-value cache)
are explicitly **out of scope** — tracked separately.

## Problem

`State::get_storage` (bcos-evm/eth/state/State.cpp:191) treats any account present in the
overlay (`m_accounts`) as owning its complete storage namespace: an absent key returns 0
and **never falls back to the base view**. But `State::mutable_account` (copy-on-first-write,
State.cpp:322) materializes accounts from `StateView::get_account`, and the production
`LedgerStateView::get_account` returns the account **with an empty storage map** (the ledger
reads storage per-slot; it never enumerates).

The two premises contradict. Any write that materializes an existing contract **before its
first storage access** breaks all subsequent storage reads of that contract:

| Trigger (materializes via `mutable_account`) | Exposure |
|---|---|
| Native value transfer (`transfer_balance` → `set_balance`) | ETH, OpStack |
| `set_transient_storage` (TSTORE, EIP-1153) — pure code path, no value needed | ETH, OpStack, FISCO (all ≥ Cancun) |
| EIP-7702 `applyAuthorizations` → `set_nonce(authority)` (Eip7702.cpp:204); also `set_code` on delegation install | ETH, OpStack |
| Precompile CALL target touch (`PrecompileRouter.cpp:102 touchOverlayAccount`) | all chains |
| `EthFeeSettlement::buyGas` sender debit (EthFeeSettlement.cpp:134; 7702-delegated EOA with own storage) | ETH |
| SELFDESTRUCT zero-balance beneficiary touch (`EthHost.cpp:249`) — **pre-Spurious-Dragon only** (guarded by `revision < EVMC_SPURIOUS_DRAGON`) | legacy forks only |

Consequences after materialization:
- **SLOAD** of a base-resident slot returns 0 instead of the ledger value (wrong execution).
- **SSTORE-to-zero** of a base-resident slot: `ensureStorageOriginal` caches 0 (via the same
  broken read), the prior==new short-circuit drops the write, `build_diff` never exports it —
  the ledger slot is **never cleared** (wrong persisted state). Because `applyStateDiff` runs
  per-tx, the dropped clear also corrupts **subsequent transactions in the same block** (their
  SLOADs and EIP-2200 originals see the stale value).
- **SSTORE non-zero**: the final value persists, but the mis-cached original (0 instead of the
  true base value) mis-classifies EIP-2200/3529 SSTORE gas and refunds, and exports spurious
  redundant writes.
- **OpStack L1Block predeploy readers** (`OpStackFeeParams.cpp:154`, `L1BlockStorage.cpp:287`,
  `L1BlockPredeploy.cpp`) read fee parameters through the same path: any materializing write
  to the predeploy makes subsequent fee-parameter reads return 0.

The most alarming reachable pattern is a **transient-storage reentrancy guard**
(TSTORE at function entry, then SLOAD business state) — ordinary Cancun contract code.

### Why every test passes

All EVM-executing tests use `InMemoryStateView`/`TestStateView`, whose `get_account` returns
the **full Account including its storage map** — materialization copies complete storage, so
the "overlay owns namespace" invariant holds in tests and the fallback path is never needed.
`LedgerStateViewTest` performs no EVM execution. The bug is production-only and invisible to
the entire current suite. This is the systemic blind spot this design also closes.

### Provenance

**A recent regression, two days old.** `State.cpp` gained explicit read-through at its
foundation (`8f10ea0e4`, 2026-06-18) and kept it until `8b1c2ef55` (2026-07-06) introduced the
`return evmc_bytes32{};` early-return — its direct parent `dc89d8be9` still fell through to
`m_baseStateView->get_storage`. The commit's own comment names why: "Required after
touchCreateDeploymentAccount / clear_storage — must not read base." Read-through was removed
wholesale because the two **storage-namespace-reset** cases needed suppression and no
per-account mechanism existed to scope it. This design supplies that mechanism: it restores
read-through for ordinary materialized accounts while preserving the reset semantics that
motivated the removal. (`f28d19659` later only reworded the comment.)

### geth reference model

geth's `stateObject` reads through to the trie even when dirty: `GetCommittedState` checks
`pendingStorage` → `originStorage` → falls back to the database reader and caches the result.
The read-through suppressor is **not** the `newContract` flag (that flag is EIP-6780-only:
set by `CreateContract`, journaled, reset at every tx boundary, and never consulted by
`GetState`). The actual suppressor is the **StateDB-level, block-scoped
`stateObjectsDestruct` set**, populated at `Finalise` for self-destructed /
touched-empty accounts and cleared at `Commit`; a destructed address short-circuits both
object lookup and committed-state reads for the rest of the block.

At CREATE deployment geth sets **no reset marker and clears nothing** — it relies on EIP-7610
guaranteeing the storage is already empty. Our `storageReset` at
`touchCreateDeploymentAccount` is therefore *stricter than* geth but **observationally
equivalent**: with the 7610 collision check enforced (which runs before the flag is ever
set), base storage at a successful CREATE address is empty, so "suppress" and "read empty"
coincide. For SSTORE gas on a reset account, geth's destructed branch returns 0 as the
committed original — exactly what `storageReset` produces through `ensureStorageOriginal`.

## Design

### Core change (4 sites, all in eth/state)

**1. `Account.hpp`** — one new flag, matching the existing bool style
(`selfDestructed`/`selfDestructScheduled`):

```cpp
/// Storage namespace reset this tx (CREATE deployment / clear_storage): absent keys
/// mean deleted-zero, do NOT fall back to base (geth: block-scoped stateObjectsDestruct
/// suppression, applied here per reset account).
bool storageReset{false};
```

**2. `State.cpp` — two set sites** (exactly the two scenarios the regression's comment named):
- `touchCreateDeploymentAccount` (def :771, live path): after `account.storage.clear()`
  (:783), set `account.storageReset = true`.
- `clear_storage` (def :450; its only caller `EthHost::destroyContractState` is currently
  dead code, but the semantics must stay correct for future use): set
  `account.storageReset = true`.

The EIP-6780 finalize branch (State.cpp:746-754) needs **no** flag: it repopulates the overlay
with explicit zeros for every cleared key, so reads hit the present-key branch.

**3. `State::get_storage` (:191)** — the fix:

```cpp
if (auto it = m_accounts.find(address); it != m_accounts.end())
{
    if (auto s = it->second.storage.find(key); s != it->second.storage.end())
        return s->second;
    if (it->second.storageReset)
        return evmc_bytes32{};                       // reset account: absent ⇒ deleted-zero
    return m_baseStateView->get_storage(address, key);  // materialized: read through ← FIX
}
return m_baseStateView->get_storage(address, key);
```

Caller inventory (verified complete): SLOAD (`EthHost.cpp:104`), SSTORE original/current
(`EthHost.cpp:116,120`), `ensureStorageOriginal` (State.cpp:212), `set_storage` prior
(State.cpp:439), OpStack L1Block predeploy readers. **Every caller wants the base value**;
none relies on the materialize-⇒-0 behavior. `ensureStorageOriginal` and SSTORE gas
classification are fixed automatically by routing through the corrected read.

**4. `State::hasNonEmptyStorage` (EIP-7610, :166)** — consistency: skip the base-view read
when the overlay account has `storageReset` set. **Honest scope note:** under a sparse view
this predicate is *independently* broken (it enumerates `base->get_account()->storage`,
which `LedgerStateView` leaves empty — false negatives both pre- and post-fix). Fixing that
requires a per-slot/ledger-level emptiness probe the KV store does not offer; it is a
documented residual gap, not closed by this design.

### Invariants and edge cases

| Case | Behavior | Why it holds |
|---|---|---|
| Revert across CREATE/reset | Flag restored via whole-`Account` `AccountSnapshot` journal entry (State.cpp:374 snapshots by value; :287-293 restores or erases on first-touch `nullopt`) | Verified, incl. interleavings: same-checkpoint set-after-snapshot reverts to pre-flag (correct — the reset itself is undone); flag set in checkpoint N survives revert of N+1 whether or not N+1 touches the account |
| CREATE at a fresh address | Correct even with flag=false: base has no account, read-through returns 0 | Flag only matters when base holds residual storage |
| Same-tx SELFDESTRUCT → re-CREATE | Unreachable: `failIfCreateDeploymentBlocked` runs before `touchCreateDeploymentAccount`; selfdestruct leaves nonce non-zero → nonce collision blocks the CREATE before the flag is ever set (matches geth) | EvmCallFrame.cpp:697-706, CreateDeployment.h:71-80 |
| Reset account with base-residual storage | Unreachable at persist time: the collision check calls `hasNonEmptyStorage` unconditionally, blocking CREATE at any address with detectable base storage before the flag is set | CreateDeployment.h:79 |
| EIP-6780 finalize zeroing | Explicit zeros in the overlay map → hit path, fallback never consulted | Unchanged |
| `ensureStorageOriginal` / SSTORE gas | Routes through `get_storage` → fixed automatically; original = **tx-start** base value (geth `pendingStorage` semantics — base view reflects prior same-block txs via per-tx `applyStateDiff`) | No separate change |
| Per-tx flag vs geth's per-block destruct set | Equivalent on **Cancun+**: EIP-6780 only deletes same-tx-created contracts (no base-resident storage) and EIP-7610 blocks deploying onto residual storage. **Pre-Cancun forks** additionally require the base view to reflect full account destruction across txs; the ledger's no-enumeration constraint leaves residual storage rows behind (pre-existing, documented at StateDiffApplier.h:72-74) — out of scope | geth `stateObjectsDestruct` persists to Commit; our flag dies at tx end |
| Transient storage | Per-tx, overlay-only by nature | Untouched |
| Full-storage test views | Materialization copies complete storage → key-miss ⇒ true zero → fallback branch never fires | Existing EEST parity (2181/2181 Cancun) cannot regress by construction |
| Performance | SLOAD-miss on a materialized non-reset account now costs one base read per access — identical to the pre-materialization cost of the same read, and to every read of a never-materialized account. No new caching introduced (matches current design; geth memoizes via `originStorage`, a possible later enhancement) | — |

### Test plan (blind-spot governance: dedicated regression suite)

**New helper** — `bcos-evm/test/helpers/SparseStorageStateView.h`: mirrors the
`LedgerStateView` contract with **exactly two overrides**: `get_account` returns
balance/nonce/code/codeHash with an **empty storage map**; `get_storage` serves per-slot
reads from a separate slot map. Narrow readers stay at `StateView` defaults (they route
through `get_account` and yield correct field values; the only difference vs
`LedgerStateView` is read amplification, which is not this bug). Seed API:
`insert_account`, `set_slot`.

**New suite** — `bcos-evm/test/state/SparseStorageOverlayTest.cpp`. Setup for every scenario:
base account with slot `K=5`. **[B]** = bug-exposing (fails pre-fix, passes post-fix);
**[G]** = guard (passes both; protects reset semantics).

1. **[B]** `set_balance` → SLOAD `K` returns 5 (pre-fix: 0).
2. **[B]** `set_balance` → SSTORE `K`←0 → `build_diff` **contains** `K=0`
   (`BOOST_REQUIRE(find(K) != end)` — pre-fix the prior==0 short-circuit drops the write and
   the slot is absent from the diff).
3. **[B]** `set_balance` → SSTORE `K`←5 (same value) → diff storage **empty**
   (pre-fix: prior mis-reads 0 ≠ 5 → spurious redundant export + 2200/3529 gas
   misclassification).
4. **[B]** `set_transient_storage` (TSTORE) → SLOAD `K` returns 5.
5. **[B]** `set_nonce` (7702 path) → SLOAD `K` returns 5.
6. **[B]** `set_code` (7702 delegation install) → SLOAD `K` returns 5.
7. **[B]** `mark_self_destructed` (6780: code keeps executing) → SLOAD `K` returns 5.
8. **[B]** `touchOverlayAccount` → SLOAD `K` returns 5.
9. **[B]** materialize (`set_balance`) **first**, then SSTORE `K`←9 →
   `storageOriginalAtTxStart(K) == 5` (materialize-first is load-bearing: an SSTORE-first
   flow reads base correctly even pre-fix and would not distinguish).
10. **[G]** `touchCreateDeploymentAccount` → SLOAD `K` returns 0 (reset semantics preserved).
11. **[G]** `clear_storage` → SLOAD `K` returns 0. (Do **not** assert build_diff export here:
    under a sparse view `clear_storage` enumerates an empty `base->storage` — the acknowledged
    out-of-scope enumeration limitation.)
12. **[B]** checkpoint → `touchCreateDeploymentAccount` (flag set) → revert → SLOAD `K`
    returns 5 (read-through restored; base account pre-exists so the snapshot is non-null).
13. **[G]** nested: outer checkpoint → inner checkpoint → reset in inner → inner `commit` →
    outer `revert` → SLOAD `K` returns 5 (guards journal dedup + commit-bubbling interaction).

**Existing gates (must stay green — they prove the fix is inert under full-storage views;
by construction they cannot exercise the new branches, so the new suite is the sole
load-bearing coverage):** `StateJournalRevert`, `StateBuildDiff`, `EvmCallFrameTest`,
`EthEestStateGranularSmoke`, `EthEestBlockchainSmoke` / `EthEestBlockGranularSmoke`,
`OpStackEestStateSmoke` / `OpStackEestBlockchainSmoke`,
`EthExecutionSpec1153TstoreSmoke`, `EthExecutionSpec7702CoreSmoke`,
`EthExecutionSpec6780Smoke`.

### Rejected alternatives

- **State-level side-set of reset addresses**: same semantics but requires a new journal
  entry type for revert (the Account flag gets revert correctness for free), and repeats the
  `EthHost::m_createdInTx` side-set pattern whose ancestor-revert gap is a known finding.
- **Materialize with full storage** (make `LedgerStateView::get_account` enumerate storage):
  the ledger KV has no cheap per-account storage enumeration; would reintroduce the
  read-amplification the narrow per-field readers just removed. Rejected on cost.

### Coordination constraints

Touches `Account.hpp` / `State.cpp` / `State.hpp` — the concurrent work stream's hot files —
and **directly amends its two-day-old commit `8b1c2ef55`** (which removed read-through to
gain reset semantics; this design keeps the reset semantics and restores read-through). The
diff is narrow (~25 lines). Run the EEST parity gates immediately after implementation to
demonstrate no test-visible behavior change. Out-of-scope items (build_diff full-account
loads, `clear_storage` sparse-enumeration gap, `hasNonEmptyStorage` sparse-view false
negatives, pre-Cancun cross-tx residual storage rows, duplicate tx-start original cache,
`destroyContractState` dead code) are deliberately untouched and documented above.

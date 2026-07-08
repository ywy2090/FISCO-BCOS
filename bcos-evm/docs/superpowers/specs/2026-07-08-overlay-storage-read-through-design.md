# Overlay Storage Read-Through — Design

**Date:** 2026-07-08
**Status:** Approved (design review complete)
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

| Trigger (materializes via `mutable_account`) | Chain exposure |
|---|---|
| Native value transfer (`transfer_balance` → `set_balance`) | ETH, OpStack |
| `set_transient_storage` (TSTORE, EIP-1153) — pure code path, no value needed | ETH, OpStack, FISCO (all ≥ Cancun) |
| EIP-7702 `applyAuthorizations` → `set_nonce(authority)` | ETH, OpStack |
| SELFDESTRUCT beneficiary touch (`EthHost.cpp:249 touchOverlayAccount`) | ETH, OpStack |
| `EthFeeSettlement::buyGas` sender debit (7702-delegated EOA with own storage) | ETH |

Consequences after materialization:
- **SLOAD** of a base-resident slot returns 0 instead of the ledger value (wrong execution).
- **SSTORE-to-zero** of a base-resident slot: `ensureStorageOriginal` caches 0 (via the same
  broken read), the prior==new short-circuit drops the write, `build_diff` never exports it —
  the ledger slot is **never cleared** (wrong persisted state).
- **SSTORE non-zero**: the final value persists correctly, but the cached original (0 instead
  of the true base value) mis-classifies EIP-2200/3529 SSTORE gas and refunds.

The most alarming reachable pattern is a **transient-storage reentrancy guard**
(TSTORE at function entry, then SLOAD business state) — ordinary Cancun contract code.

### Why every test passes

All EVM-executing tests use `InMemoryStateView`/`TestStateView`, whose `get_account` returns
the **full Account including its storage map** — materialization copies complete storage, so
the "overlay owns namespace" invariant holds in tests and the fallback path is never needed.
`LedgerStateViewTest` performs no EVM execution. The bug is production-only and invisible to
the entire current suite. This is the systemic blind spot this design also closes.

### Provenance

Not a recent regression: the no-fallback semantics dates to the foundational State commit
(`8b1c2ef55`); `f28d19659` only reworded the comment. The original comment names its own
intent — "Required after touchCreateDeploymentAccount / clear_storage — must not read base" —
i.e. the rule exists for the two **storage-namespace-reset** cases, but was implemented as a
blanket rule for every overlay account.

### geth reference model

geth's `stateObject` lazily loads `originStorage` from the trie even when the object is dirty;
only the `newContract` flag (created/recreated this transaction) suppresses trie fallback.
This design replicates that split exactly.

## Design

### Core change (4 sites, all in eth/state)

**1. `Account.hpp`** — one new flag, matching the existing bool style
(`selfDestructed`/`selfDestructScheduled`):

```cpp
/// Storage namespace reset this tx (CREATE deployment / clear_storage): absent keys
/// mean deleted-zero, do NOT fall back to base (geth stateObject.newContract analog).
bool storageReset{false};
```

**2. `State.cpp` — two set sites** (exactly the two scenarios the old comment named):
- `touchCreateDeploymentAccount` (~:779, live path): after `account.storage.clear()`, set
  `account.storageReset = true`.
- `clear_storage` (~:453; its only caller `EthHost::destroyContractState` is currently dead
  code, but the semantics must stay correct for future use): set `account.storageReset = true`.

**3. `State::get_storage` (~:191)** — the fix:

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

**4. `State::hasNonEmptyStorage` (EIP-7610, ~:165)** — consistency: skip the base-view read
when the overlay account has `storageReset` set (currently reads base unconditionally, which
contradicts reset semantics — a pre-existing minor inconsistency this flag lets us close).

### Invariants and edge cases

| Case | Behavior | Why it holds |
|---|---|---|
| Revert across CREATE/reset | Flag restored via whole-`Account` `AccountSnapshot` journal entry; first-touch snapshot is `nullopt` → account erased on revert | Verified: State.cpp:283-295 |
| CREATE at a fresh address | Correct even with flag=false: base has no account, read-through returns 0 | Flag only matters when base holds residual storage (recreate) |
| EIP-6780 finalize zeroing | Writes explicit zeros into the overlay map (keys present) → hit path returns 0, fallback never consulted | Unchanged |
| `ensureStorageOriginal` / SSTORE gas classification | Routes through `get_storage` → fixed automatically; `storageOriginalAtTxStart` already falls back to base when uncached | No separate change |
| Transient storage | Per-tx, overlay-only by nature | Untouched |
| Full-storage test views | Materialization copies complete storage → key-miss ⇒ true zero → fallback branch never fires | Existing EEST parity (2181/2181 Cancun) cannot regress by construction |
| FISCO | Value-transfer trigger absent (native transfer skipped), but TSTORE trigger applies; fix is chain-agnostic | — |

### Test plan (blind-spot governance: dedicated regression suite)

**New helper** — `bcos-evm/test/helpers/SparseStorageStateView.h`: mirrors the
`LedgerStateView` contract. `get_account` returns balance/nonce/code/codeHash with an
**empty storage map**; `get_storage` serves per-slot reads from a separate slot map.

**New suite** — `bcos-evm/test/state/SparseStorageOverlayTest.cpp`:

1. `set_balance` (value-transfer path) → SLOAD of base-resident slot returns the base value
   (pre-fix: 0).
2. Same materialization → SSTORE-to-zero → `build_diff` exports the clear (pre-fix: dropped).
3. `set_transient_storage` (TSTORE) → SLOAD of base-resident slot.
4. `set_nonce` (7702 path) → SLOAD.
5. `touchOverlayAccount` → SLOAD.
6. `touchCreateDeploymentAccount` → SLOAD returns 0 (reset semantics preserved).
7. `clear_storage` → SLOAD returns 0.
8. checkpoint → reset → revert → read-through restored.
9. SSTORE non-zero overwrite → `storageOriginalAtTxStart` equals the true base value
   (gas-classification correctness).

**Existing gates (must stay green, proving zero behavior change under full-storage views):**
`StateJournalRevertTest`, `EvmCallFrameTest`, EEST Cancun state/blockchain smoke,
OpStack EEST smoke.

### Rejected alternatives

- **State-level side-set of reset addresses**: same semantics but requires a new journal
  entry type for revert (the Account flag gets revert correctness for free), and repeats the
  `EthHost::m_createdInTx` side-set pattern whose ancestor-revert gap is a known finding.
- **Materialize with full storage** (make `LedgerStateView::get_account` enumerate storage):
  the ledger KV has no cheap per-account storage enumeration; would reintroduce the
  read-amplification the narrow per-field readers just removed. Rejected on cost.

### Coordination constraints

Touches `Account.hpp` / `State.cpp` / `State.hpp` — the concurrent work stream's hot files —
but the diff is narrow (~25 lines). Run the EEST parity gates immediately after
implementation to demonstrate no test-visible behavior change. Out-of-scope items
(build_diff full-account loads, clear_storage dead work, duplicate tx-start original cache,
`destroyContractState` dead code) are deliberately untouched.

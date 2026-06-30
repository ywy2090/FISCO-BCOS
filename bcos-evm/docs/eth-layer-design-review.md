# bcos-evm/eth/ — Design review

**Purpose:** A reviewer's guide to the shared EVM kernel layer. What each module does, how they compose, and what to check when reviewing a PR that touches this directory.

**Status:** Updated — 2026-06-24  
**Parent:** [architecture-overview.md](architecture-overview.md) · 外部入口 [review-pack.md](review-pack.md)

---

## 1. Layer contract

`eth/` is the **shared kernel** — the layer that `bcos/` (FISCO) and `opstack/` (OP Stack) both depend on. It must **never** include headers from `bcos/` or `opstack/`. This invariant is enforced by ADR-005 Rule 1 and audited by Gap 38.

```
eth/  ──►  evmone, bcos-framework, bcos-utilities, bcos-protocol, bcos-crypto
bcos/ ──►  eth/
opstack/ ──►  eth/
```

---

## 2. Subdirectory map (8 modules)

| # | Directory | Role | Key symbols | Test directory |
|---|-----------|------|-------------|----------------|
| 1 | `eth/state/` | Copy-on-write state engine | `State`, `StateView`, `EthHost`, `Account`, `StateDiff` | `test/state/` |
| 2 | `eth/policy/` | Extension point base class | `HostExtension`, `EthHostExtension` | `test/eth/` (indirect) |
| 3 | `eth/vm/` | EVM lifecycle (policy, factory, instance) | `EthChainPolicy`, `VMFactory`, `VMInstance` | `test/eth/` |
| 4 | `eth/precompiled/` | Precompile dispatch and gas | `PrecompileRouter`, `EthBuiltinRegistry`, `BlsGas`, `ModexpGas` | `test/eth/` |
| 5 | `eth/gas/` | Gas settlement | `computeTxIntrinsicGas`, `settleTopLevelTransactionGas`, `Eip7623`, `Eip1559` | `test/eth/` |
| 6 | `eth/pipeline/` | Hook-based pre/post kernel pipeline | `TxPipeline`, `TxPipelineContext`, `TxPipelineHooks`, `debitIntrinsicGas` | `test/eth/` |
| 7 | `eth/execution/` | Warm-up and feature preparation | `warmTransactionEntry`, `TxFeaturePrepare`, `BlockInfoBuilder`, `Eip2929PrecompileWarm` | `test/eth/` |
| 8 | `eth/` (root) | Entry points, types, cross-cutting | `executeMessage`, `executeViaEth`, `RevisionConfig`, `Eip7702`, `EVMCResult` | `test/eth/`, `test/` (root) |

---

## 3. Module-by-module design walkthrough

### 3.1 `eth/state/` — Copy-on-write state engine

**Files:** `State.hpp` / `.cpp`, `StateView.hpp`, `EthHost.hpp` / `.cpp`, `Account.hpp`, `StateDiff.hpp`, `BlockInfo.hpp`, `Transaction.hpp`, `EthPrecompiles.hpp` / `.cpp`, `CreateExecution.h`, `Transition.hpp` / `.cpp`, `BloomFilter.hpp` / `.cpp`, `HashUtils.hpp`, `Errors.hpp`

**Design:**

```
StateView  (abstract read-only interface — get_account, get_balance, get_nonce, get_code, get_storage)
    ↑
State      (copy-on-write wrapper around StateView — checkpoint/commit/revert, set_*, selfdestruct tracking)
    ↑  used by:
EthHost     (evmone Host implementation — 669 lines, implements the Host C++ interface)
```

`StateView` is the key seam. It is a pure-virtual interface with one method that must be implemented: `get_account()`. All other methods (`get_balance`, `get_nonce`, `get_code`, `get_code_hash`, `get_storage`) have default implementations derived from `get_account`. This means a new chain only needs to implement one method to provide state to the kernel.

`State` takes a `StateView&` and adds a mutable overlay — it copies accounts from the view on first write (`checkpoint()`, `commit()`, `revert()`). This is the standard copy-on-write pattern used by evmone.

`EthHost` is the largest single file in the kernel (669 lines). It implements the evmone `Host` interface (`account_exists`, `get_storage`, `set_storage`, `get_balance`, `selfdestruct`, `call`, `create`, `get_tx_context`, `get_block_hash`, `emit_log`, `access_account`, `access_storage`). Reviewers should pay closest attention to:

- **`call()`** (lines 252-375): routes through precompile check, extension hooks, CREATE binding, checkpoint, value transfer, EVM execute, and result handling. This is the single most complex function in the kernel.
- **`selfdestruct()`**: encodes EIP-6780 semantics (same-tx CREATE tracking).
- **`set_storage()`**: dispatches SSTORE refund/status through `EvmHostHooks` (`applySstoreRefund`, `classifyStorageStatus`); FISCO legacy paths override in `FiscoVmHostPolicy`.

**Points a reviewer should check:**
1. New state mutations go through `State`, not directly to `StateView`.
2. `Account` has no FISCO-specific fields in the current tree.
3. `EthHost` changes should not add new public methods; use `EvmHostHooks` overrides instead.

---

### 3.2 `eth/policy/` — Extension point base class

**Files:** `HostExtension.h`, `EthHostExtension.h`

**Design:**

```cpp
struct HostExtension {
    virtual bool allowSelfdestruct(const Account&)       { return true; }   // Eth default
    virtual bool allowDelegateCallToPrecompile()         { return true; }   // Eth default
    virtual bool skipHostValueTransfer()                 { return false; }  // Eth default
    virtual std::optional<evmc_result> tryChainPrecompile(evmc_revision, const evmc_message&);
    virtual void prepareMessage(evmc_revision, evmc_message&);
    virtual void setCallerAddress(const evmc_address&);
    virtual void bumpContractCreateNonce(const evmc_address&);
};
```

This is the **single injection point** for chain-specific behavior into the kernel. Every method has a default implementation that represents standard Ethereum semantics. Chain implementations override only what differs:

- `EthHostExtension` — no overrides (matches Ethereum defaults exactly).
- `FiscoHostExtension` — disables selfdestruct, disables delegatecall-to-precompile, skips value transfer, hooks chain precompile dispatch, bumps CREATE nonce.
- `OpHostExtension` — only overrides `tryChainPrecompile` for L1Block predeploy dispatch.

**Design principle:** The default is Ethereum. Differences are explicit overrides. This is the opposite of "every chain implements everything."

**Points a reviewer should check:**
1. New virtual methods must have a sensible Ethereum default.
2. Adding a method requires updating `FiscoHostExtension` and `OpHostExtension` (in `bcos/` and `opstack/`, not in `eth/`).
3. Virtual dispatch happens in the kernel hot path — avoid adding methods called per-opcode.

---

### 3.3 `eth/vm/` — EVM lifecycle

**Files:** `EthChainPolicy.h`, `VMFactory.h`, `VMInstance.h` / `.cpp`

**Design:**

`EthChainPolicy` is the simplest policy: it maps block numbers to `evmc_revision` using the standard Ethereum fork schedule, then calls `revisionConfigFromRevision()` to produce a `RevisionConfig`. No feature flags, no masking — this is the pure Ethereum reference.

`VMFactory` creates and caches evmone VM instances. `VMInstance` wraps a single VM with thread-local storage.

**Points a reviewer should check:**
1. `EthChainPolicy` must stay simple — no FISCO feature flags, no OP fork schedules.
2. VM creation is expensive; the factory's caching logic should not regress.

---

### 3.4 `eth/precompiled/` — Precompile dispatch and gas

**Files:** `PrecompileRouter.h` / `.cpp`, `EthBuiltinRegistry.h` / `.cpp`, `EthPrecompiles.hpp` / `.cpp`, `PrecompiledContract.h` / `.cpp`, `PrecompileTraits.h`, `PrecompileActive.h`, `PrecompiledAddress.h`, `BlsGas.h`, `ModexpGas.h` / `.cpp`

**Design:**

The precompile system has a three-level dispatch hierarchy:

```
PrecompileRouter (top-level dispatch)
    ├── 1. Chain precompile (via HostExtension::tryChainPrecompile)  ← chain-specific
    ├── 2. Kernel precompile (via EthPrecompiles)                    ← shared kernel
    └── 3. Empty-account success (EIP-161)                           ← fallback
```

`EthBuiltinRegistry` provides a compile-time table (via `PrecompileTraits`) of all Ethereum built-in precompiles (0x01-0x11) with their gas functions and execution callbacks. `ModexpGas` encodes three gas schedules (EIP-198, EIP-2565, EIP-7883) and EIP-7823 input validation. `BlsGas` handles the BLS MSM discount table.

`PrecompileActive.h` gates precompile availability on `RevisionConfig` fields — `eip2537` for BLS (0x0b-0x11) and `eip7212` for P256 (0x0100).

**Points a reviewer should check:**
1. Adding a new precompile requires updating `EthBuiltinRegistry` AND the capability matrix.
2. Precompile precedence (chain → kernel → empty-account) must be preserved.
3. Gas computation must handle overflow — `ModexpGas` uses `bigint` for intermediate values, but old paths in the legacy executor had `convert_to<int64_t>()` without overflow checks.

---

### 3.5 `eth/gas/` — Gas settlement

**Files:** `EthTxGasSettlement.h`, `Eip7623.h`, `Eip1559.h`, `Eip4844.h`

**Design:**

`EthTxGasSettlement.h` defines the core gas types and computations:

- `computeTxIntrinsicGas()` — computes intrinsic gas from message data, access list, and authorization tuples.
- `settleTopLevelTransactionGas()` — debits gas from sender, credits coinbase, handles included-tx vmerr post-settlement.
- `TxGasSettlementSnapshot` — captures pre/post settlement state for matrix tracking.

`Eip7623.h` provides the calldata floor gas computation. `Eip1559.h` handles effective gas tip/fee normalization. `Eip4844.h` provides blob gas constants.

**Points a reviewer should check:**
1. Gas settlement functions are pure — no state access, no side effects. This is deliberate: they should be testable with no setup.
2. The `calldata_floor_per_token` field in `RevisionConfig` is consumed here.
3. Gas settlement is shared between Eth and BCOS paths but NOT by OP Stack (which has its own `OpStackGasSettlement`).

---

### 3.6 `eth/pipeline/` — Shared orchestration pipeline (ADR-019)

**Files:** `TxPipeline.h` / `.cpp`, `TxPipelineContext.h`, `TxPipelineHooks.h`, `IntrinsicGasDebit.h`, `AdoptEvmcResult.h`, `BuildExecuteMessageInput.h`, `CaptureSettlementSnapshot.h`, `NormalizeIncludedTxVmerr.h`

**Design:**

All three execution paths call `runTxPipeline` (verified in source):

- `eth/ExecuteViaEth.cpp` — Eth reference path
- `bcos/ExecuteViaHost.cpp` — FISCO production path
- `opstack/OpStackExecuteViaHost.cpp` — OP Stack production path

Wrappers map input → fill `TxPipelineHooks` → call pipeline → map output. Async fee routing (`buyGas`/`refundGas`), deposit state machine, and final `stateDiff`/`logs` mapping stay in wrapper code outside the pipeline (ADR-019 Q7/Q18/Q19).

```
runTxPipeline(ctx, hooks):
    0. validate(vm, hashImpl)     — outside try/catch
    1. hooks.prepareMessage(ctx)
    2. hooks.preExecute(ctx)        ← early exit → PreExecuteRejected
    3. hooks.preDebitEntry(ctx)     ← early exit → PreDebitRejected (OpStack)
    4. debitIntrinsicGas(ctx.message, intrinsicPolicy)  ← shared; mutates ctx.message
    5. hooks.preKernel(ctx)         ← may throw; balance/21000/canTransfer
    6. buildExecuteMessageInput(ctx) + hooks.tuneKernelInput
    7. executeMessage(input)        ← input.message == ctx.message (post-debit invariant)
    8. adoptEvmcResult()            ← shared
    9. captureSettlementSnapshot()  ← Eip7623 mode only
   10. hooks.postAdopt(ctx)
   11. hooks.postSettle(ctx)
    ── exception path ──
    hooks.mapException(ctx, eptr)   ← kernel does NOT revert state
```

`TxPipelineContext` is non-copyable/non-movable, owns `state::State` and the sole mutable `evmc_message`. `extension` is a borrow pointer set by the wrapper before `runTxPipeline`. `TxPipelineHooks` is a struct of `std::function` callbacks with no-op defaults.

**Seam discipline:** `eth/pipeline/` must not `#include bcos/` or `opstack/`. Chain behaviour enters only through hooks or wrapper translation units (e.g. `OpStackPreDebitEntry` called from `preDebitEntry` lambda in `OpStackExecuteViaHost.cpp`).

**Points a reviewer should check:**
1. New shared orchestration steps belong in `runTxPipeline` or a portable header under `eth/pipeline/`, not duplicated in three wrappers.
2. New hooks must have sensible no-op defaults in `TxPipelineHooks`.
3. `earlyExit` / `TxPipelineExitKind` must be set after `preExecute`, `preDebitEntry`, intrinsic failure, and `preKernel`.
4. Intrinsic failure uses `DebitIntrinsicGasOutcome` + `mapIntrinsicFailure`; do not construct chain-final `EVMCResult` inside `debitIntrinsicGas`.
5. Exception path: each chain's `mapException` owns checkpoint revert policy (Eth/Fisco revert when checkpoint exists; OpStack maps to internal error).

---

### 3.7 `eth/execution/` — Warm-up and feature preparation

**Files:** `WarmTransactionEntry.h`, `TxFeaturePrepare.h`, `BlockInfoBuilder.h`, `Eip2929PrecompileWarm.h`

**Design:**

`warmTransactionEntry()` warms the sender, recipient (based on transaction kind), access list entries, and coinbase. This runs before `executeMessage()`.

`TxFeaturePrepare.h` defines `TransactionProperties` (warm flags for sender, recipient, coinbase, create address, and 7702 delegation target) and `setWarmDestinationFromKind()`.

`BlockInfoBuilder.h` converts `state::BlockInfo` to `evmc_tx_context`.

**Points a reviewer should check:**
1. Warm access depends on `evmc_revision >= EVMC_BERLIN` — the `warm_access` field in `RevisionConfig` is profile-only (ADR-004).
2. The "prepare phase dead warm" (Gap 36) means warm sets from `prepareTransaction` are NOT persisted to execute.

---

### 3.8 `eth/` root — Entry points and cross-cutting types

**Files:** `ExecuteMessage.h` / `.cpp`, `ExecuteViaEth.h` / `.cpp`, `ExecuteViaEthPreCheck.h` / `.cpp`, `RevisionConfig.h`, `EVMCResult.h` / `.cpp`, `Eip7702.h` / `.cpp`, `AccessList.h`, `EthExecutionArtifacts.h`, `EthTxExecutor.h`, `Transfer.h`, `Web3TypedTxKind.h`

**Design:**

**`executeMessage()`** — the kernel entry point. Takes `ExecuteMessageInput` (stateView, vm, message, revisionConfig, extension, blockInfo, blockHashes, gasPrice, accessList, authorizationList, txProps, fix flags) and returns `ExecuteMessageOutput` (result, stateDiff, logs, gasRefund). This is a pure function — no global state, no side effects outside the State copy.

**`executeViaEth()`** — Eth reference path wrapper. Fills `TxPipelineHooks` for 1559 caps, precheck, intrinsic modes, `canTransfer`, included-tx vmerr, then calls `runTxPipeline`.

**`executeViaHost()` / `opStackExecuteViaHost()`** — production wrappers in `bcos/` and `opstack/`; both call the same `runTxPipeline` with chain-specific hooks. See `ExecuteViaHost.cpp` and `OpStackExecuteViaHost.cpp`.

**`RevisionConfig.h`** — the EIP switch bitfield. Key design elements:
- Three categories: A-class (6 feature-gated fields), B-class (revision-derived), C-class (fork parameters).
- `REVISION_CONFIG_BOOL_FIELDS` X-macro with `static_assert(13)` for drift detection.
- `REVISION_CONFIG_GATED_FIELDS` X-macro with `static_assert(6)` for the A-class subset.
- `revisionConfigFromRevision()` is the **single source of truth** for canonical EIP gating (ADR-018).
- `makeIsthmusRevisionConfig()` returns `revisionConfigFromRevision(EVMC_PRAGUE)` — dense Prague profile. Used in production by `OpStackTransactionExecutorImpl` and extensively in tests.

**`Eip7702.h` / `.cpp`** — EIP-7702 delegation: `applyAuthorizations()`, `resolveDelegatedCode()`, `warmDelegationTarget()`. Separate from the state module because it's a cross-cutting EIP.

**Points a reviewer should check:**
1. `RevisionConfig.h` changes must update the capability matrix AND `RevisionConfigProfileTest`.
2. `executeMessage()` changes must not add chain-specific parameters — use `HostExtension*` instead.
3. The `Eip7702` module should not grow beyond authorization logic.

---

## 4. Key design invariants (reviewer checklist)

| # | Invariant | Enforced by |
|---|-----------|-------------|
| 1 | `eth/` never includes `bcos/` or `opstack/` | ADR-005 Rule 1, Gap 38 audit |
| 2 | Chain-specific behaviour goes through `HostExtension*`, not compile-time conditionals | Code review |
| 3 | `revisionConfigFromRevision()` is the single source for EIP gating | ADR-018, `static_assert` |
| 4 | Each A-class field in `RevisionConfig` has a corresponding FISCO feature flag | `FISCO_GATED_FLAG_MAP` x-macro in `FiscoPolicy.h` |
| 5 | Gas functions are pure (no state, no side effects) | Code review |
| 6 | `StateView::get_account()` is the only method a new chain MUST implement | Interface design |
| 7 | `TxPipelineHooks` defaults are no-ops | Default `std::function` initialization |
| 8 | Capability matrix must be updated in the same PR as kernel/hook changes | CI gate (`capability-gate.yml`) |
| 9 | `runTxPipeline` is the only fixed orchestration pipeline; wrappers supply hooks only | ADR-019; three `executeVia*` call sites |
| 10 | `eth/pipeline/` must not include `bcos/` or `opstack/` headers | ADR-005 §4, ADR-019 |

---

## 5. What belongs in eth/ vs. bcos/ vs. opstack/

```
eth/        bcos/                        opstack/
─────       ──────                       ────────
State       FiscoStateView (adapter)     OpStackBlockHeaderExtension
StateView   FiscoHostExtension           OpHostExtension
EthHost     FiscoPolicy                  OpStackFee
HostExt.    FiscoRevisionConfig          OpStackForkSchedule
RevConfig   ExecuteViaHost               OpStackExecuteViaHost
executeMsg  FiscoTxExecutor              OpStackTxExecutor
EthChainPolicy   AuthPort / ChainPrecompPort  OpStackPreCheck
Precompile  StateDiffApplier             OpStackFloorGas
gas/*       FiscoExecutionArtifacts         L1Block*
orchestr.   FiscoBlockInfo               RollupCost
execution/* FiscoTxAdapter               OpStackDepositTx
Eip7702     FiscoTransactionPrepare       OpStackReceiptMeta
```

**Rule of thumb:** If it depends on a FISCO concept (auth, chain precompile, ABI, binary address mode), it belongs in `bcos/`. If it depends on an OP Stack concept (L1 block, deposit tx, rollup cost, blob gas pool), it belongs in `opstack/`. Everything else belongs in `eth/`.

---

## 6. Known friction (from architecture review, 2026-06-23)

1. **ExecutionFrame duplication** — `executeMessage` (depth=0) and `EthHost::call` (depth>0) implement the same frame semantics on two paths. Highest-leverage next cut per [architecture-review-post-orchestration-2026-06-23.md](architecture-review-post-orchestration-2026-06-23.md).

2. **PrecompileRouter envelope order** — current order is transfer → checkpoint → dispatch; geth uses snapshot-first. Known stateRoot risk; see error-handling parity spec P0.

3. **Warm vs dispatch not single-sourced** — `PrecompileActive` reads `cfg.eip2537`, but tx-entry warm may still key off `evmc_revision`. Gap 37 / ADR-004 tension.

4. **State abstraction leaks** — `Account::abi` is a FISCO-specific field living in `eth/state/Account.hpp`.

5. **EthHost size** — 669 lines. Reviewers should flag new logic in `call()` that could move into a shared frame helper.

6. **OpStack `txData` shadow frame** — intrinsic gas unified on `ctx.message`, but `OpStackTxExecutionData` still carries 20+ settlement fields; next drift risk in refund/settlement ring (ADR-019 Q14 partial).

---

## 7. Test coverage map

| Module | Primary test files | Coverage notes |
|--------|-------------------|----------------|
| `state/` | `test/state/` (13 files), `test/StateJournalRevertTest.cpp` | Well-covered. State journal (checkpoint/commit/revert) has dedicated tests. |
| `EthHost` | `test/eth/EthHostExtensionHooksTest.cpp` | Hook coverage good. Error paths in `call()` are exercised indirectly. |
| `precompiled/` | `test/eth/PrecompileRouter*` (4 files), `test/eth/Eip2537KernelTest.cpp`, `test/eth/Eip7212KernelTest.cpp`, `test/eth/EipPrecompileRevisionGateTest.cpp` | Thorough. Precedence ordering, revision gating, and equivalence all covered. |
| `gas/` | `test/eth/Eip7623PrecheckTest.cpp` | Gas functions are pure and well-tested. |
| `orchestration/` | `test/eth/TxPipelineTest.cpp`, `test/eth/DebitIntrinsicGasTest.cpp`, `test/opstack/OpStackIntrinsicGasSyncTest.cpp` | Pipeline step order covered; OpStack message-gas sync via test spy seam |
| `executeMessage` | `test/ExecuteMessageSmokeTest.cpp` | Smoke only. Edge cases in the main flow body tested via spec tests. |
| `executeViaEth` | `test/eth/EthExecuteViaEth*.cpp` | Good coverage through spec test fixtures. |
| `RevisionConfig` | `test/eth/RevisionConfigProfileTest.cpp` | Comprehensive profile test covering all fields. |

---

## 8. Change impact map

When reviewing a PR that changes these files, check the corresponding items:

| File changed | Must also check |
|-------------|-----------------|
| `RevisionConfig.h` | `capability-matrix.md` (update), `RevisionConfigProfileTest.cpp` (extend), `FiscoPolicy.h` (`FISCO_GATED_FLAG_MAP` alignment) |
| `HostExtension.h` (new virtual) | `FiscoHostExtension.h` (implement), `OpHostExtension.h` (implement), `EthHostExtension.h` (implement), capability matrix |
| `ExecuteMessage.cpp` | `ExecuteViaHost.cpp` (does BCOS path need changes?), `OpStackExecuteViaHost.cpp` (does OP path need changes?) |
| `EthHost.cpp` | All HostExtension implementations, precompile router tests |
| `PrecompileRouter.cpp` | `PrecompileRouterPrecedenceTest`, `PrecompileRouterEquivalenceTest`, capability matrix |
| `TxPipeline.cpp` | All three wrappers (`ExecuteViaEth.cpp`, `ExecuteViaHost.cpp`, `OpStackExecuteViaHost.cpp`), `TxPipelineTest.cpp`, capability matrix |
| `EthTxGasSettlement.h` | Gas tests, capability matrix |
| `Eip7702.cpp` | BCOS and OP Stack 7702 tests, capability matrix |

---

## 9. Architectural decisions recorded

| ADR | Topic | Impact on eth/ |
|-----|-------|---------------|
| ADR-001 | TE baseline vs reference path | `executeViaEth` is reference-only; not production inheritance proof |
| ADR-004 | RevisionConfig field consumption | `warm_access`, `eip1559`, `eip3651` are profile-only (partial consumers) |
| ADR-005 | Orchestration domain boundaries | HostExtension runs in kernel; orchestrator / `runTxPipeline` runs before `executeMessage` |
| ADR-015 | ETH reference 7702 gas + included-tx vmerr | `normalizeIncludedTxVmerr` in orchestration |
| ADR-016 | ETH TE EIP-1559 settlement | `Eip1559.h`, `EthTxExecutor` |
| ADR-017 | FISCO precompile port | Port interfaces in `bcos/ports/`; kernel `PrecompileRouter` orthogonal |
| ADR-018 | Revision gating single-source | `revisionConfigFromRevision()` is the canonical source; consumers read `cfg` bools |
| ADR-019 | Orchestration pipeline | `runTxPipeline` fixed 12-step pipeline; all three `executeVia*` are thin wrappers |

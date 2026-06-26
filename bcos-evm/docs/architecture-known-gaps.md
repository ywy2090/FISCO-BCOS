# Architecture known gaps (inheritance contract)

Tracked items from Phase 1 audit and grill review. Update when closed.

## 36 — BCOS Prepare-phase dead warm

`TransactionExecutorImpl::prepareTransaction` runs `warmTransactionEntry` on a **local** `State` during Prepare. That warm set is **not** persisted into Execute; only `executeViaHost` Execute-phase wiring is normative for TE baseline inheritance.

**Status:** Documented (design doc §2.1). **Fix options:** remove Prepare warm, or persist warm markers into Execute storage (product decision).

## 37 — RevisionConfig profile-only fields

Fields such as `eip1559` are consumed via `Eip1559Access.h` (typed-tx gate, fee-cap precheck, `normalizeGasCaps`, OpStack refund). `eip3651` and `warm_access` are wired: coinbase warm via `isCoinbaseWarmEnabled`; EIP-2929 TE gate via `Eip2929Access.h` (ADR-004 Scheme A — FISCO `feature_evm_eip2929=OFF` is intentional deviation).

**Status:** Partially closed (`eip3651`, `warm_access`, `eip1559` consumed). Removed: `prague_post_execution` (dead flag).

## 38 — VmHostPolicy CMake / include audit

`bcos-evm/eth` must not include BCOS/OP headers. Chain hooks live in `FiscoVmHostPolicy` / `OpStackVmHostPolicy` implementing `state::VmHostPolicy`.

**Status:** Audit complete for current tree — no BCOS/OP includes under `bcos-evm/eth`. Re-run when adding hooks.

## Related (not blocking Phase 1)

| Gap | Notes |
| --- | --- |
| TE → `bcos-executor` Web3 decoder | **Resolved (ADR-007):** builders continue to depend on `Web3AccessListResolver`; migration deferred |
| EIP-7212 on TE path | Not in `EthPrecompiles`; matrix `unsupported` |
| L1Block via `OpHostExtension` | `L1BlockGetterTest` + `L1BlockPredeployTest` |
| `stEIP7702_delegation.json` fixture | **Closed (pipeline):** `ExecuteViaHostImportedFixtureTest` — plain CALL smoke, not 7702 delegation E2E |

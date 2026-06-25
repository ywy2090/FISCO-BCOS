# Architecture known gaps (inheritance contract)

Tracked items from Phase 1 audit and grill review. Update when closed.

## 36 — BCOS Prepare-phase dead warm

`TransactionExecutorImpl::prepareTransaction` runs `warmTransactionEntry` on a **local** `State` during Prepare. That warm set is **not** persisted into Execute; only `executeViaHost` Execute-phase wiring is normative for TE baseline inheritance.

**Status:** Documented (design doc §2.1). **Fix options:** remove Prepare warm, or persist warm markers into Execute storage (product decision).

## 37 — RevisionConfig profile-only fields

Fields such as `warm_access`, `eip1559`, and `prague_post_execution` are set in policy builders but have **no TE kernel/orchestration consumer** today (or are explicitly reserved). `eip3651` is wired: `warmTransactionEntry` reads `cfg.eip3651` for coinbase warm (ADR-004). `prague_post_execution` stays struct-default `false` — deprecated/reserved, future-removal candidate.

**Status:** Partially closed (`eip3651` consumed). Remaining: `warm_access` / `eip1559` profile-only; `prague_post_execution` reserved. **Fix options:** wire remaining consumers, delete reserved fields, or keep as profile documentation only.

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

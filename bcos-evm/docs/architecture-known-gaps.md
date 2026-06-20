# Architecture known gaps (inheritance contract)

Tracked items from Phase 1 audit and grill review. Update when closed.

## 36 — BCOS Prepare-phase dead warm

`TransactionExecutorImpl::prepareTransaction` runs `warmTransactionEntry` on a **local** `State` during Prepare. That warm set is **not** persisted into Execute; only `executeViaHost` Execute-phase wiring is normative for TE baseline inheritance.

**Status:** Documented (design doc §2.1). **Fix options:** remove Prepare warm, or persist warm markers into Execute storage (product decision).

## 37 — RevisionConfig profile-only fields

Fields such as `warm_access`, `eip3651`, `eip1559`, `prague_post_execution` are set in policy builders but have **no TE kernel/orchestration consumer** today. Runtime warm access uses `evmc_revision` and tx props (ADR-004).

**Status:** Documented in ADR-004 + matrix rows. **Fix options:** wire consumers, delete fields, or keep as profile documentation only.

## 38 — HostExtension CMake / include audit

`bcos-evm/eth` must not include BCOS/OP headers. Chain hooks live in `FiscoHostExtension` / `OpHostExtension` implementing `state::HostExtension`.

**Status:** Audit complete for current tree — no BCOS/OP includes under `bcos-evm/eth`. Re-run when adding hooks.

## Related (not blocking Phase 1)

| Gap | Notes |
| --- | --- |
| TE → `bcos-executor` Web3 decoder | **Resolved (ADR-007):** builders continue to depend on `Web3AccessListResolver`; migration deferred |
| EIP-7212 on TE path | Not in `EthPrecompiles`; matrix `unsupported` |
| L1Block via `OpHostExtension` | `L1BlockGetterTest` + `L1BlockPredeployTest` |
| `stEIP7702_delegation.json` fixture | **Closed (pipeline):** `ExecuteViaHostImportedFixtureTest` — plain CALL smoke, not 7702 delegation E2E |

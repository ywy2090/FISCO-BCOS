# Architecture known gaps (inheritance contract)

Tracked items from Phase 1 audit and grill review. Update when closed.

## 36 — BCOS Prepare-phase dead warm

**Status:** **Closed (2026-07-02).** TE Prepare no longer calls `prepareTransaction` / `prepareState` on a local `State`. Tx-entry warm is normative in kernel Execute (`InnerExecute::prepareTxEntry` → `prepareState`). `FiscoPrepareTransaction.h` remains for test harnesses (`ExecuteViaHostEip2929Harness`).

**Was:** `TransactionExecutorImpl` / `EthTransactionExecutorImpl` ran warm on ephemeral Prepare `State` that did not persist into Execute.

## 37 — RevisionConfig profile-only fields

Fields such as `eip1559` are consumed via `Eip1559Gate.h` (typed-tx gate, fee-cap precheck, `normalizeGasCaps`, OpStack refund). `eip3651` and `eip2929` are wired: coinbase warm via `isCoinbaseWarmEnabled`; EIP-2929 TE gate via `Eip2929Gate.h` (ADR-004 Scheme A — FISCO `feature_evm_eip2929=OFF` is intentional deviation).

**Status:** Partially closed (`eip3651`, `eip2929`, `eip1559` consumed). Removed: `prague_post_execution` (dead flag). Isthmus criteria 14 (no 6110/7002/7251 block postExecution) guarded by `IsthmusPostExecutionPolicyTest` + `check-opstack-no-prague-post-execution.sh`.

## 38 — EvmHostHooks CMake / include audit

`bcos-evm/eth` must not include BCOS/OP headers. Chain hooks live in `FiscoEvmHostHooks` / `OpStack chain call-target adapter` implementing `state::EvmHostHooks`.

**Status:** Audit complete for current tree — no BCOS/OP includes under `bcos-evm/eth`. Re-run when adding hooks.

## Related (not blocking Phase 1)

| Gap | Notes |
| --- | --- |
| TE → `bcos-executor` Web3 decoder | **Resolved (ADR-007):** builders continue to depend on `Web3AccessListResolver`; migration deferred |
| EIP-7212 on TE path | Not in `EthPrecompiles`; matrix `unsupported` |
| L1Block via `OpHostExtension` | `L1BlockGetterTest` + `L1BlockPredeployTest` |
| `stEIP7702_delegation.json` fixture | **Closed (pipeline):** `ExecuteViaHostImportedFixtureTest` — plain CALL smoke, not 7702 delegation E2E |

## 39 — ADR-028 reject vs included (entry-failure inclusion)

Pre-execution failures (intrinsic gas, buyGas afford, transfer afford, rules preCheck) are **consensus-rejected** in geth/op-geth: no receipt, tx not in block, sender balance unchanged. Post ADR-025, bcos orchestration state/fee on abort is aligned, but **TE still always `makeReceipt`** and the block scheduler unconditionally appends receipts.

**Status:** **Tracked — deferred.** Verified by [`2026-07-01-eth-vs-geth-parity.md`](audits/2026-07-01-eth-vs-geth-parity.md) Round 2 (N2 / reject cluster). **Not a Prague+ state-root blocker.** Fix in one batch with TE + consensus per [ADR-028](adr/028-consensus-reject-entry-failure-inclusion.md) Phases C–D.

**Impact:** receiptsRoot, inclusion count, optional sender gas debit on intrinsic fail (N2); not state root on happy-path storage.

**Do not:** patch only `eth/` ErrorPolicy or kernel early-exit without TE Finalize gate and scheduler null-receipt handling.

## 40 — ADR-015 included-vmerr receipt status (geth receiptsRoot / RPC)

Included top-level vmerr settlement/state matches geth (ADR-015 Accepted), but `normalizeIncludedTxVmerr` sets `TransactionStatus::None` → TE receipt + `ReceiptResponse` report **RPC success** where geth reports **failed** (INVALID / OOG / …).

**Status:** **Closed (2026-07-02).** `IncludedTxVmerrNormalize.h` normalizes `status_code` only; receipt `TransactionStatus` preserved (`BadInstruction` / `OutOfGas` / `RevertInstruction` for 7702). Tests: `EthIncludedTxVmerrTest`, `EthStateTransitionErrorPolicyTest`.

**Do not:** remove `topLevelIncludedTxVmError` or peak-gas settlement; receipt fix is orthogonal to ADR-015 state semantics.

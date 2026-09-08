# Known items at c83c15381 — do NOT re-derive

Pinned head: `c83c153813ab5a62a9d585dbb4f411d504aa172e`
Stack base: `f22bb8b968ab703b0abd456bb32cde7bbcc21a03` (#5550 at stack time)
This is a **behaviour-change** Karst landing (Osaka EVM + timestamp OpForkSchedule + Engine getPayload V4→V5), not a refactor.

## Closed findings (do not re-report)

F1–F8, P-HIGH-F1, F9–F13: all `fixed` at this head (see prior ledger). Mechanisms:
- Q5 scans every envelope/tx (`hasNonDepositEnvelope` / `hasNonDepositTx`); DA 176B still last-tx.
- `eth_call` dry-run skips EIP-7825 (`enforce_max_tx_gas = !call`); block path still enforces.
- OP caps advertise getPayload V4+V5 only, not V3.
- `preBlockOpSteps`/`processOpBlock` require `schedule` + `parentTsSec`; `OpScheduler` ctor throws on null schedule.
- `envelopeIsDeposit` uses `kDepositTxType`; `OpForkId.h`/`OpTime.h` have full Apache grant.
- V5 JSON pin calls getPayload 5; template is `[op_fork_schedule] canonical=`; genesis resolve canonicalizes.

## Open (already filed — do not re-report)

F14 LOW: `.agents/reviews/pr-karst-on-5550-46f9bc9a9/` committed in `30296bb3b`.

## Carry-forward LOWs (do not re-file unless the mechanism changed)

hash hex length; trailing comma `"0:isthmus,"`; release gate tracked-only; unconditional parent-header read; stored-vs-genesis ignore; unused Osaka `expected.bin`; self-oracle schedule hash; dual `"0:isthmus"`/`"0:jovian"`; op-geth file-header overclaim; dead newPayload version-mismatch branch.

## Deliberate behaviours (not findings)

- Engine profile keys on **payload/attrs timestamp**, never head. Timestamps are internal ms; schedule takes Unix seconds via `unixSecondsFromInternalMillis`.
- Q5 deposits-only on all Jovian+ activations in `preBlockOpSteps`.
- Executor accepts deposit-after-user off the activation window (warn only). Do not enforce global deposit-first.
- `TestBypass` must not enter Initializer.
- FCU V3 / newPayload V4 / getPayload V5 only at Karst.
- estimateGas shares `call=true` with eth_call so both skip 7825 (TransactionExecutor has no isEstimate).

## Oracle

op-reth ≥ v2.3.3, op-node ≥ v1.19.1 — not op-geth as the primary oracle.

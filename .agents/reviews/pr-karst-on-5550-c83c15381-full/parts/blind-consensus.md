# Blind consensus review (PIN c83c153813ab5a62a9d585dbb4f411d504aa172e)

Outline: Q5 deposits-only (execute vs FCU build) → Engine profile/caps/getPayload version → timestamp units → profile key (head vs payload/attrs) → 7825 paths → schedule/hash fail-open.

## Findings

### F1 — HIGH — INTRODUCED
**Scope:** FCU payload build vs Q5 execute gate  
**Location:** `engine/bcos-engine/OpEngineService.inl:340-345,494-505` (build) vs `opstack-executor/OpBlockExecute.h:377-386` (Q5)

```
if (!payloadAttributes.noTxPool.value_or(false)) {
    ... m_memPool.seal(..., sealedTxs);
}
...
for (auto const& [hash, env] : sealedEnvelopes) {
    if (evicted.count(hash) != 0) continue;
    ...
    candidateEnvelopes.push_back(env);
}
```

**Problem:** `buildOpPayload` never consults `isNoUserTxActivationBlock`. Jovian/Karst activation still seals the mempool (unless `noTxPool`) and appends those envelopes after forced deposits. Live execute (`OpScheduler::execute` → `preBlockOpSteps`) then rejects the candidate:

```
if (isNoUserTxActivationBlock(*schedule, parentTsSec, blockTsSec) &&
    hasNonDepositEnvelope(rawTxBytes))
    throw OpConsensusError("... fork activation block");
```

That `OpConsensusError` has no `txHash`. The retry loop therefore cannot drop pool txs; it throws `OpExecutionInternalError` (`inl:569-570`). Forced attrs that already contain a non-deposit fail the same way.

**Why (reachable):** Sequencer `forkchoiceUpdatedV3` at the Karst (or Jovian) activation timestamp, `noTxPool` unset/false, mempool non-empty — the default Engine miner shape. Parent header is already loaded for `calcOpBaseFee`, so parent/attrs Unix seconds are available. A spec-following CL that only lists deposits in `attrs.transactions` still gets pool user txs mixed in. Result: no `payloadId`, activation block cannot be produced (`-32603` loop) until the pool is empty.

newPayload of a user-tx activation block is correctly INVALID (Q5). This is EL-builder liveness vs the new consensus gate, not an accept-too-loose execute bug.

**Fix:** If `isNoUserTxActivationBlock(schedule, parentTsSec, attrsTsSec)`, skip `seal` (treat as `noTxPool`) and/or FCU-INVALID any non-deposit in `attrs.transactions`. Do not retry-evict via culprit hashes.

## Clean on this slice
- Execute Q5 full-scans envelopes; 176B DA last-tx is a separate op-geth CalcDAFootprint path.
- `engineApiFor`: FCU V3 / newPayload V4 / getPayload V4→V5 at Karst; caps match; getPayload/FCU/newPayload key on payload/attrs seconds, not head.
- Live timestamps go through `unixSecondsFromInternalMillis` (header/payload/attrs/parent).
- `runDeposit` gates 7825 with `!cfg.deposit_exempt_from_max_tx_gas`; user txs use `opValidate` policy (deposits never enter `opValidate`).
- `OpScheduler` ctor rejects a null schedule; `hashErr` is fail-closed in `finalizeOpBlockResult`.

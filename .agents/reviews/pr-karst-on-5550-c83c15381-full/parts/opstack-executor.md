# opstack-executor — full module (c83c15381)

Reviewed with fisco-review v1.20.1. **Zero new findings.** Do not re-file F1–F14 / carry-forward LOWs.

## Re-audit (Q5 / schedule / 7825)

Live execute cannot skip Q5. `OpScheduler` ctor throws on null schedule and `execute()` always passes `m_schedule.get()` plus parent Unix seconds. `preBlockOpSteps` / `processOpBlock` still no-op Q5 when `schedule == nullptr`; every such call in this module is a test helper (`RunSharedPath`, `runOpBlock`, `runExecutionProbe`). **Accepted risk (tests):** those helpers are not a live caller. Do not treat as a production hole.

`hasNonDepositEnvelope` / `hasNonDepositTx` still scan every item; DA 176B still last-tx only.

Block `m_prepare` sets `enforce_max_tx_gas = !call`. Live `call()` uses the 10-arg form with `call=true` (eth_call/estimateGas skip 7825 — disclosed). Serial `executeBlock` must be `call=false` or `m_finish` would drop the diff; dual-path goldens imply that. `processOpBlock` calls `opValidate` without an explicit policy; `TxValidationPolicy::enforce_max_tx_gas` defaults `true`.

## Findings

None.

## Claim-audit (this module)

Karst Q5 on all Jovian+ activations, warn-only deposit-after-user off the window, Engine Karst getPayload V5 / FCU V3 / newPayload V4, and estimateGas sharing `call=true` match known deliberate behaviours. No extra undisclosed live behaviour in these files.

## Checked, and not a problem

1. **Live Q5 always armed.** Ctor + execute:
```
if (!m_schedule) { throw std::invalid_argument("OpScheduler: null fork schedule"); }
...
preBlockOpSteps(..., m_schedule.get(), parentTsSec);
```
(`OpScheduler.h:405-407`, `1044-1045`)

2. **Q5 all-scan vs DA last-tx** (known split, still true):
```
if (schedule != nullptr) {
    ... isNoUserTxActivationBlock(...) && hasNonDepositEnvelope(rawTxBytes)
...
bool const lastTxIsDeposit = envelopeIsDeposit(rawTxBytes.back());
```
(`OpBlockExecute.h:377-394`)

3. **7825 only skipped on dry-run:**
```
evmone::state::TxValidationPolicy const policy{.enforce_max_tx_gas = !call};
```
(`OpstackExecutor.h:1605`; `coCallOnView` passes `call=true` at `1485-1487`)

4. **6-arg `executeTransaction` refuses block execution** (`if (!call) throw` at `1255-1259`).

5. **Karst Engine profile** is FCU V3 / getPayload V5 / newPayload V4 (`OpSchedulerSeam.h:97-104`); Osaka mismatch → `InconsistentExecutionConfig` (`129-131`).

## Conventions

Module headers are SPDX-short (same as `OpScheduler.h`). New `KarstNutHelpers.{h,cpp}` have no Apache grant; not scored (hygiene, matches several existing test helpers). Ctor still throws `std::invalid_argument` (pre-existing idiom, not Karst-new).

## Verification boundary

Read: listed module files, outline/grep of other tests only where Q5/`nullptr`/`call` fired. Did not execute tests. `SchedulerSerialImpl.h` is not in this tree — call-flag inferred from writeback contract + goldens (see above).

Could not determine: none that is tree-resolvable inside this module.

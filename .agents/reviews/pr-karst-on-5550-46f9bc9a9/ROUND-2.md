# Round 2 at head 46f9bc9a933824fb41405bd5f85f599edab44a1c

**Target:** Karst-on-#5550 (local branch `feat/karst-on-release-3.18`, no GitHub PR)
**base → head:** `f22bb8b96` → `46f9bc9a9` (+3916/−832, 66 files, 22 commits)
**Last full:** Round 1 at `aa0cdbeac`
**This round delta:** `aa0cdbeac..46f9bc9a9` — 2 commits, +274/−100, 15 files
  - `4e95c1094` fix(opstack): close Karst review findings on schedule, tests, and Q5
  - `46f9bc9a9` fix(opstack): scan all txs on Q5 activation deposits-only
**Mode:** Delta (fisco-review v1.20.1). fisco-review-pr v2.13.0 cannot bind a GitHub PR or run 5+1 blind → formal artifact verdict `INCOMPLETE` (see §6).
**Conflict of interest:** this reviewer session authored `46f9bc9` and directed `4e95c1094`. Findings on the Q5 fix are self-review; treat as non-independent.
**Prior findings:** Round 1 F1–F8 (all claimed fixed). Parallel second-reviewer list confirmed at `4e95c1094` (not in Round 1 ledger).
**Convergence rule:** every prior finding fixed or rejected-with-verified-premise at an unchanged head, no new findings, every disclosed behaviour change verified against the code. A new HIGH/BLOCKER or a mis-disclosed behaviour change resets convergence.
**Round ceiling:** 3
**Head re-checked immediately before this report:** still `46f9bc9a933824fb41405bd5f85f599edab44a1c`. Zero unaudited commits.

## Prior-status table

| ID | Round 1 title | Status at 46f9bc9 | Evidence |
|---|---|---|---|
| R1-F1 | V5 JSON pin never calls getPayload V5 | **fixed** in `4e95c1094` | `OpEngineServiceParityTest.cpp:1067-1070` — `KarstProfilePair` + `getPayload(payloadId, 5)` |
| R1-F2 | Template `[opstack].fork_schedule` | **fixed** in `4e95c1094` | `chain-config.template.yaml:16` — `[op_fork_schedule] canonical=` |
| R1-F3 | Genesis branch returns raw text | **fixed** in `4e95c1094` | `ChainMetadata.h:142-144` — `canonicalOpForkSchedule(parse…)` |
| R1-F4 | License / `kMax*` / bare throw | **fixed** (partial residual) | Codec + ChainMetadata full Apache; `c_maxOpFork*`. Residual: `OpForkId.h:1-4` SPDX-only (now R2-F13) |
| R1-F5 | FCU text names V4 | **fixed** in `4e95c1094` | `OpEngineService.inl:151` — `engine_forkchoiceUpdatedV3` only |
| R1-F6 | Karst profile test hygiene | **fixed** in `4e95c1094` | license / `c_*` timestamps (not re-quoted this delta) |
| R1-F7 | Karst-unreachable test name | **fixed** in `4e95c1094` | `OpForkScheduleTest.cpp:71` — `FlagsWrapperDoesNotSelectKarst` |
| R1-F8 | `processOpBlock` cannot run Q5 | **partial** | now takes `schedule` and runs `hasNonDepositTx` (`OpBlockExecute.cpp:108-109`); defaults still `nullptr` (R2-F12) |
| P-HIGH-F1 | Q5 last-tx only | **fixed** in `46f9bc9` | `hasNonDepositEnvelope` / `hasNonDepositTx`; tests `:217-244`. DA last-tx kept (`:396`) |

## 1. Summary

Delta closes the live Q5 hole (`[deposit, user, deposit]` on a Jovian+/Karst activation block) and the Round 1 MEDIUMs. The stack is still a **behaviour-change** Karst landing, not a refactor. Three MEDIUM gaps remain on the live path: EIP-7825 applies to `eth_call`, caps still advertise unserviceable `engine_getPayloadV3`, and omitted `schedule` silently disables Q5. No new HIGH/BLOCKER in this delta. Convergence **not** met.

## 2. Findings (open at this head)

### [F9] Q5 all-scan promotes a `k` constant
**Severity:** LOW
**Origin:** INTRODUCED (`46f9bc9`)
**Scope:** new API
**Location:** opstack-executor/OpBlockExecute.h:104
**Evidence:**
```
inline constexpr uint8_t kDepositTypeByte = 0x7e;
```
**Problem:** Checklist requires `c_` for new constants. This was a local `constexpr` and is now a namespace-scope name.
**Why it matters:** Same file already mixed `k` locally; promoting it makes the violation public.
**Recommended fix:** `c_depositTypeByte` (or reuse `kDepositTxType` from `OpTransition.h`).

### [F10] EIP-7825 cap applies to `eth_call`
**Severity:** MEDIUM
**Origin:** INTRODUCED (Karst/Osaka path; not introduced by this delta)
**Scope:** live production path
**Location:** bcos-evm/bcos-evm/opstack/OpTransition.cpp:396 · state.cpp:385
**Evidence:**
```
auto base = evmone::state::validate_transaction(view, block, tx, cfg.rev, blockGasLeft, 0);
if (policy.enforce_max_tx_gas && rev >= EVMC_OSAKA && tx.gas_limit > MAX_TX_GAS_LIMIT)
    return make_error_code(MAX_GAS_LIMIT_EXCEEDED);
```
**Problem:** Default `TxValidationPolicy.enforce_max_tx_gas = true`. `eth_call` and `estimateGas` share `executeTransaction(..., call=true)` → `opValidate` with no policy. Upstream skips the cap for `eth_call` (geth #32641); `estimateGas` stays capped.
**Why it matters:** Karst `eth_call` with explicit `gas > 2^24` returns `MAX_GAS_LIMIT_EXCEEDED`.
**Recommended fix:** Distinguish call vs estimate; pass `{.enforce_max_tx_gas = false}` only for `eth_call`.

### [F11] caps advertise `engine_getPayloadV3` but profile never serves V3
**Severity:** MEDIUM
**Origin:** INTRODUCED
**Scope:** live production path
**Location:** engine/bcos-engine/OpEngineService.cpp:91-93 · OpSchedulerSeam.h:98-110
**Evidence:**
```
"engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5"
// engineApiFor: Karst getPayload V5, else V4
```
**Problem:** Comment admits the live method is V4/V5. A V3 retrieve hits `version != ctx.api.getPayload` → `UnsupportedFork` (`-38005`). Undisclosed narrowing vs #5550 `EngineTracker` V1–V5.
**Why it matters:** CL capability intersection can pick V3 and then fail every retrieve.
**Recommended fix:** Drop V3 from caps, or map a V3-shaped profile and disclose the window.

### [F12] Default `schedule=nullptr` still silently skips Q5
**Severity:** MEDIUM
**Origin:** INTRODUCED
**Scope:** live production path (tests omit; production passes)
**Location:** opstack-executor/OpBlockExecute.h:84, :333
**Evidence:**
```
OpForkSchedule const* schedule = nullptr, uint64_t parentTsSec = 0)
if (schedule != nullptr) { /* Q5 */ }
```
**Problem:** Q5 is opt-in by argument. Six test/helper call sites omit both args (`OpL1BlockDepositTest.cpp:350`, `OpBlockInjectorTest.cpp:246`, `RunSharedPath.h:89`, `OpSchedulerTest.cpp:628`, `PreBlockOpStepsTest.cpp:184,331`). No compile error.
**Why it matters:** A future production caller that forgets the last two args ships blocks with user txs on an activation height.
**Recommended fix:** Make both arguments required; update the six sites.

### [F13] `OpForkId.h` / `OpTime.h` still lack the full Apache grant
**Severity:** LOW
**Origin:** INTRODUCED
**Scope:** new API
**Location:** bcos-framework/bcos-framework/engine/OpForkId.h:1-4
**Evidence:**
```
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
```
**Problem:** Checklist hard violation vs `LedgerTypeDef.h` full grant + `@file`/`@brief`.
**Recommended fix:** Same header as `ChainMetadata.h`.

Carry-forward LOWs confirmed still present, not re-derived as new blocks: hash hex length (`ChainMetadata.h:73-82`), trailing comma (`OpForkScheduleCodec.h:190-208`), release gate tracked-only, unconditional parent-header read, stored-vs-genesis ignore, unused Osaka `expected.bin`, self-oracle schedule hash, dual `"0:isthmus"`/`"0:jovian"` strings, op-geth file-header overclaim, dead newPayload version-mismatch branch.

## 3. Scorecard

| Dimension | Score /10 | Reason |
|---|---|---|
| Correctness | 7 | Q5 hole closed on the live path; 7825/`eth_call` and getPayload V3 mismatch remain |
| Tests | 6 | Trailing-deposit cases added; suite not built; `processOpBlock` hole untested |
| Comments/readability | 7 | DA last-tx comment corrected; Q5 vs DA split is now explicit |
| Scope discipline | 7 | Delta stays on review-fix + Q5 |
| Description honesty | 7 | No GitHub PR body. Handoff claims still match the live Engine/Q5 wiring |
| **Overall** | **6.5 / 10** | **CHANGES_REQUESTED** (F10, F11, F12) |

## 4. What this PR is doing (delta only)

| At `aa0cdbeac` | At `46f9bc9` |
|---|---|
| Q5 probes `rawTxBytes.back()` / `txs.back()` | Q5 scans every envelope / every `OpBlockTx` |
| V5 JSON test called getPayload 4 | `KarstProfilePair` + getPayload 5 |
| Template `[opstack].fork_schedule` | `[op_fork_schedule] canonical=` |
| Genesis resolve returned raw text | Returns `canonicalOpForkSchedule(parse…)` |
| DA comment “deposits always precede” | Last-tx only, matching CalcDAFootprint |

Intentional: activation deposits-only is now all-tx, not last-tx. DA 176B last-tx **unchanged** (still last-tx by design).

## 5. Checked, and not a problem

1. **Q5 all-scan does not tighten DA.** `:390-396` still uses `envelopeIsDeposit(rawTxBytes.back())` and cites CalcDAFootprint. A non-activation Jovian block with a trailing deposit still uses last-tx only.
2. **Empty block still fail-closed before Q5.** `:366-372` rejects empty / non-deposit first envelope; `hasNonDepositEnvelope` is not consulted on an empty range.
3. **Live path still keys Engine profile on payload/attrs timestamp, not head** (unchanged from Round 1; not re-broken by this delta).
4. **Executor still accepts deposit-after-user off the activation window** (`OpstackExecutor.h` warn-only). Q5 does not impose global deposit-first.

## 6. Verification boundary

**Read:** `git show 46f9bc9a9:<path>` only (worktree clean except untracked `.agents/`). Line numbers are the pinned head.

**Built/run:** none this round. No CMake build on the Karst worktree. Affected targets still unverified: `opstack-executor-block-tests` (owns the new Q5 cases), `bcos-evm-opstack-tests`, `test-bcos-engine`, `test-bcos-ledger`, `test-bcos-tool`, `test-bcos-rpc`.

**fisco-review-pr:** no GitHub PR number; cannot fetch live `head.sha`/`base.sha`, cannot write `$ARTIFACT_DIR/review_$PR.json` under a PR lock, cannot run isolated blind agents. Routing = local branch review. Verdict for that skill: `INCOMPLETE`.

**Outside this review:** rebase onto current #5550 tip; t8n / op-reth replay; GitHub description (none).

**Aggregation:** Round 1 F1–F8 re-quoted at this head. Q5 fix re-checked on both `preBlockOpSteps` and `processOpBlock`. Parallel-review MEDIUMs re-quoted (F10–F12).

Reviewed with fisco-review v1.20.1 + fisco-review-pr v2.13.0 (local-only)
subagents: 0 (delta, main-thread)

review Karst-on-#5550 结束

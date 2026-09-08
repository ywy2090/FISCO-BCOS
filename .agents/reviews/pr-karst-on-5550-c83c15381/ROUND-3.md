# Round 3 at head c83c153813ab5a62a9d585dbb4f411d504aa172e

**Target:** Karst-on-#5550 (local branch `feat/karst-on-release-3.18`, no GitHub PR)
**base → head:** `f22bb8b96` → `c83c15381` (stack base unchanged)
**Last full:** Round 2 at `46f9bc9a9`
**This round delta:** `46f9bc9a9..c83c15381` — 2 commits, 19 files (+572/−74)
  - `30296bb3b` checkpoint before checking out feat/engine-split-D-cutover (**review artifacts only**)
  - `c83c15381` fix(opstack): close remaining Karst review findings (15 source files, +146/−74 after clang-format)
**Mode:** Delta (fisco-review v1.20.1). fisco-review-pr v2.13.0 cannot bind a GitHub PR → formal artifact verdict `INCOMPLETE` (see §6).
**Conflict of interest:** this reviewer session authored `c83c15381` and directed the F10–F13 fixes. Findings on this delta are self-review; treat as non-independent.
**Prior findings:** Round 1 F1–F8 + P-HIGH-F1 (closed or partial at 46f9bc9). Round 2 F9–F13 open at 46f9bc9.
**Convergence rule:** every prior finding fixed or rejected-with-verified-premise at an unchanged head, no new findings, every disclosed behaviour change verified against the code. A new HIGH/BLOCKER or a mis-disclosed behaviour change resets convergence. Terminal APPROVE also requires a full-surface re-hunt (fast-path (b)) — this round is delta-only.
**Round ceiling:** 3 (this is the last scheduled round).
**Head re-checked immediately before this report:** still `c83c153813ab5a62a9d585dbb4f411d504aa172e`. Working tree dirty only for this review directory.

## Prior-status table

| ID | Round 2 title | Status at c83c15381 | Evidence |
|---|---|---|---|
| R1-F1…F7 | (closed at 4e95c1094) | **fixed** (unchanged) | not re-quoted; files untouched this delta |
| R1-F8 | leftover `processOpBlock` cannot run Q5 | **fixed** | args required (`OpBlockExecute.h:84`); Q5 runs when `schedule != nullptr` (`OpBlockExecute.cpp:108-109`); leftover has no production caller |
| P-HIGH-F1 | Q5 last-tx only | **fixed** (unchanged) | all-scan still in place; this delta only drops `kDepositTypeByte` |
| R2-F9 | public `kDepositTypeByte` | **fixed** in `c83c15381` | `envelopeIsDeposit` uses `kDepositTxType` (`OpBlockExecute.h:106`). Local `k` in `OpCommon.h:107` is function-scope, not the finding |
| R2-F10 | EIP-7825 on `eth_call` | **fixed** in `c83c15381` | `opValidate(..., policy)` (`OpTransition.cpp:377,397-398`); `call=true` → `{.enforce_max_tx_gas=false}` (`OpstackExecutor.h:1602-1608`). Test `KarstEthCallSkipsEip7825MaxGasLimit` |
| R2-F11 | caps advertise getPayloadV3 | **fixed** in `c83c15381` | caps are V4+V5 only (`OpEngineService.cpp:93-94`); tests pin V3 absent |
| R2-F12 | default `schedule=nullptr` | **fixed** in `c83c15381` | defaults removed (`OpBlockExecute.h:84,331`). Production `OpScheduler` ctor throws on null schedule (`OpScheduler.h:407-410`) and passes `m_schedule.get()` |
| R2-F13 | SPDX-only headers | **fixed** in `c83c15381` | full Apache + `@file`/`@brief` on `OpForkId.h` / `OpTime.h` |

## 1. Summary

Delta closes the three Round-2 MEDIUMs (F10–F12) and the two LOWs (F9, F13). Live Engine/Q5 wiring is unchanged except: `eth_call` dry-run skips EIP-7825, OP caps no longer advertise `engine_getPayloadV3`, and Q5 schedule arguments are required at compile time. One new LOW: commit `30296bb3b` landed review intermediates on the product branch. Convergence **not** met (new finding + delta-only + COI + no GitHub PR). Round ceiling reached.

## 2. Findings (open at this head)

### [F14] Review artifacts committed onto the product branch
**Severity:** LOW
**Origin:** INTRODUCED (`30296bb3b`)
**Scope:** build graph / repo hygiene
**Location:** `.agents/reviews/pr-karst-on-5550-46f9bc9a9/` (4 files)
**Evidence:**
```
30296bb3b checkpoint before checking out feat/engine-split-D-cutover
 .agents/reviews/pr-karst-on-5550-46f9bc9a9/ROUND-2.md
 .agents/reviews/pr-karst-on-5550-46f9bc9a9/findings-round1.json
 .agents/reviews/pr-karst-on-5550-46f9bc9a9/findings.json
 .agents/reviews/pr-karst-on-5550-46f9bc9a9/review_karst-on-5550.json
```
**Problem:** fisco-review keeps intermediates untracked. A checkout checkpoint committed them. They ride every future PR diff (+426).
**Why it matters:** A GitHub PR would present review notes as product; `.gitignore` only ignores `.claude/agents`.
**Recommended fix:** Drop `30296bb3b` from the branch (or delete the four files) before opening a PR.

Carry-forward LOWs confirmed still present, not re-derived as new blocks: hash hex length, trailing comma `"0:isthmus,"`, release gate tracked-only, unconditional parent-header read, stored-vs-genesis ignore, unused Osaka `expected.bin`, self-oracle schedule hash, dual `"0:isthmus"`/`"0:jovian"` strings, op-geth file-header overclaim, dead newPayload version-mismatch branch.

## 3. Scorecard

| Dimension | Score /10 | Reason |
|---|---|---|
| Correctness | 8 | F10–F12 live holes closed; leftover `processOpBlock` Q5 skip is test-only |
| Tests | 7 | eth_call 7825 unit + caps V3-absent pins; estimateGas not separately tested |
| Comments/readability | 8 | 7825 shared-path and V3-drop comments match the code |
| Scope discipline | 6 | Fix commit stays on review items; checkpoint pollutes the branch |
| Description honesty | 8 | No GitHub PR. Comments disclose estimateGas sharing `call=true` |
| **Overall** | **7.5 / 10** | **COMMENT** (F14 + carry-forward LOWs; no open MEDIUM/HIGH) |

## 4. What this PR is doing (delta only)

| At `46f9bc9` | At `c83c15381` |
|---|---|
| `opValidate` always default-enforces EIP-7825 | `call=true` passes `{.enforce_max_tx_gas=false}` |
| caps list `engine_getPayloadV3/V4/V5` | V4+V5 only |
| `preBlockOpSteps`/`processOpBlock` default `schedule=nullptr` | both args required |
| public `kDepositTypeByte` | `kDepositTxType` |
| `OpForkId.h`/`OpTime.h` SPDX-only | full Apache grant |
| `.agents/reviews` untracked | committed in `30296bb3b` |

Intentional: Karst `eth_call` with `gas > 2^24` now validates. OP `exchangeCapabilities` no longer offers getPayloadV3.

## 5. Checked, and not a problem

1. **Block path still enforces EIP-7825.** `OpstackExecutor.h:1602` sets `enforce_max_tx_gas = !call`. `call=false` keeps the default cap. `KarstOrdinaryTxRejectsGasOverEip7825Cap` still pins reject.
2. **estimateGas also skips the cap — accepted residual.** `call=true` is the shared TransactionExecutor dry-run; there is no `isEstimate`. Disclosed at `OpTransition.h:71-75` and `OpstackExecutor.h:1600-1601`. Omitted-gas estimate still returns used gas (cannot exceed 2^24 for a mineable tx). Explicit `estimateGas(gas>2^24)` diverges from geth; not a consensus path.
3. **Production Q5 cannot omit the schedule.** `OpScheduler` ctor (`:407-410`) throws `invalid_argument` on null `m_schedule`; execute passes `m_schedule.get()`. Test helpers that pass `nullptr` skip Q5 **explicitly**.
4. **`engineApiFor` still never returns getPayload V3** (`OpSchedulerSeam.h:101-114`: Karst V5, else V4). Dropping V3 from caps matches the live window.
5. **Q5 all-scan / DA last-tx split unchanged.** `hasNonDepositEnvelope` vs `envelopeIsDeposit(rawTxBytes.back())` for 176B (`OpBlockExecute.h:387-396`).
6. **OpEngineReviewFixTest +45/−38 is clang-format**, except the V3-dead-list / V5-present assertions.

## 6. Verification boundary

**Read:** `git show c83c15381:<path>` only. Line numbers are this pinned head.

**Built/run (this conversation, pre-hook tree then format-only commit):**
- Built: `bcos-evm-opstack-tests`, `test-bcos-engine`, `opstack-executor-block-tests`, `opstack-executor-tests` (exit 0).
- Ran: `OpOsaka*` 12/12 (includes new eth_call case), `OpKarstActivationSuite` 8/8, `*PreBlock*` 15/15, `OpEngineReviewFixTest` 18/18, `OpEngineServiceParityTest` 37/37, `OpEngineKarstProfileSuite` 6/6, `OpKarstReleaseGateSuite` 3/3.
- Not re-run after clang-format on `c83c15381` (hook-only wrap).
- Not run: `OpT8nReplay`, full binaries, t8n / op-reth replay.

**fisco-review-pr:** no GitHub PR number; cannot fetch live `head.sha`/`base.sha` or write `$ARTIFACT_DIR/review_$PR.json` under a PR lock. Verdict for that skill: `INCOMPLETE`.

**Outside this review:** rebase onto current #5550 tip; drop `30296bb3b` before opening a PR; full-surface hunt required before a terminal APPROVE.

**Aggregation:** Round 2 F9–F13 re-quoted at this head and marked fixed. Adversarial pass on the F10/F11/F12 mechanisms (shared dry-run, caps vs `engineApiFor`, ctor null-schedule). One new LOW (F14).

Reviewed with fisco-review v1.20.1 + fisco-review-pr v2.13.0 (local-only)
subagents: 0 (delta, main-thread)

review Karst-on-#5550 结束

# EEST State-Full Parity — Loop Agent Prompt

**Date:** 2026-07-06  
**Purpose:** Copy-paste prompt for Cursor `/loop` or long-running agent sessions  
**Spec:** `bcos-evm/docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md`  
**Baseline:** verified 2026-07-06 on `build-bcos-evm-check`, EEST v5.4.0

---

## How to use

```text
/loop
<paste "Master prompt" section below>
```

Or fixed interval (re-check baseline every 30m while a WP is in progress):

```text
/loop 30m
<paste "Master prompt" section below>
```

Dispatch **one WP per implementer subagent**; main agent orchestrates baseline → WP → verify → next WP.

---

## Verified baseline (2026-07-06)

**Runner:** `EthExecutionSpecStateTests`  
**Manifest:** `eth-eest-state-full.json` (16 entries)

| Metric | Value |
|--------|-------|
| Executed | 4140 |
| Pass | 3075 (74.3%) |
| Fail | 1065 |

**Failure buckets:**

| Type | Count |
|------|-------|
| stateRoot mismatch | 902 |
| expectException but execution succeeded | 163 |
| logsHash / wrong-status | 0 |

**By directory (fail):**

| Directory | Fail | Dominant type |
|-----------|------|---------------|
| `cancun/eip4844_blobs` | 890 | precheck 152 + stateRoot 738 |
| `prague/eip7702_set_code_tx` | 68 | stateRoot |
| `prague/eip7623_increase_calldata_cost` | 59 | stateRoot |
| `cancun/eip6780_selfdestruct` | 34 | stateRoot |
| `osaka/eip7825_transaction_gas_limit_cap` | 12 | precheck 11 + stateRoot 1 |
| `shanghai/eip3860_initcode` | 2 | stateRoot |

**Already 100% in manifest:** 2537, 7823, 7883, 7951, 1153, 5656, 7516, 3651, 3855.

**Reproduce:**

```bash
BIN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests
EEST=build-bcos-evm-check/_deps/evm_ref_eest_root
MANIFEST=bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json
EXPECT=bcos-evm/test/eth-eest-test/manifests/expectations.json

$BIN --manifest $MANIFEST --eest-root $EEST --expectations $EXPECT \
  > /tmp/eest-pass.txt 2> /tmp/eest-fail.txt

echo -n "PASS: "; rg -c '^PASS ' /tmp/eest-pass.txt
echo -n "FAIL: "; rg -c '^FAIL ' /tmp/eest-fail.txt
```

---

## Work packages (strict order)

| WP | Scope | Exit criteria | Slice command |
|----|-------|---------------|---------------|
| **WP-4844-P0** | Blob tx pool reject | insufficient-full **0 fail** | `--manifest eth-eest-4844-insufficient-full.json` |
| **WP-4844-P1** | Blob execution stateRoot | blobs-all **>95%** (1037+/1092) | `--manifest eth-eest-4844-blobs-all.json` |
| **WP-7623** | Calldata floor residual | **>95%** (459+/483) | `--manifest slices/eip7623-state-full.json` |
| **WP-7702** | 7702 state residual | **>95%** (525+/552) | `--manifest slices/eip7702-state-full.json` |
| **WP-7825** | MaxTxGas reject | smoke + full dir 0 precheck fail | `--manifest eth-eest-7825-gas-limit-cap-smoke.json` + osaka dir |
| **WP-6780** | Same-tx SELFDESTRUCT | **>95%** (109+/115) | `--manifest eth-eest-6780-all.json` |
| **WP-MISC** | 3860 etc. | state-full 0 fail | state-full |

**Do not skip WP-4844-P0** — precheck fixes change whether stateRoot is evaluated.

### WP-4844-P0 detail (163 fail, 152 in 4844)

| Expected `TransactionException` | Count |
|----------------------------------|-------|
| `INSUFFICIENT_ACCOUNT_FUNDS` | 144 |
| `TYPE_3_TX_INVALID_BLOB_VERSIONED_HASH` | 4 |
| `INSUFFICIENT_MAX_FEE_PER_BLOB_GAS` | 2 |
| `TYPE_3_TX_ZERO_BLOBS` | 1 |
| `TYPE_3_TX_MAX_BLOB_GAS_ALLOWANCE_EXCEEDED\|TYPE_3_TX_BLOB_COUNT_EXCEEDED` | 1 |

**Audit refs:** `2026-07-01-eth-vs-geth-parity.md` §2.4, §2.6  
**Semantics:** EEST expects **pool reject** (tx not executed); assert fails when `actual.status == EVMC_SUCCESS`.

**Code paths:** `EthMessageAdapter` → `applyEthMessage` → `EthStateTransitionHooks` (blob checks) → `EthFeeSettlement::buyGas` (blob balance debit).

**Regression guard:** `eth-eest-4844-blob-smoke.json` must stay **14/14**.

---

## Master prompt (paste into /loop)

```markdown
# Task: EEST state-full parity closure (feat-evm-refactor worktree)

## STOP when (all true)
- `EthExecutionSpecStateTests --manifest eth-eest-state-full.json` → **0 fail** (4140 pass)
- `ctest -L specs-tests-smoke` on `build-bcos-evm-check` → all pass
- No regression on already-green dirs (2537, 7823, 7883, 7951, 1153, 5656, …)

## Read first (round 1, no code changes)
- bcos-evm/docs/superpowers/plans/2026-07-06-eest-parity-loop-prompt.md (this file)
- bcos-evm/docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md
- bcos-evm/docs/audits/2026-07-01-eth-vs-geth-parity.md §2.4, §2.6, 7825

## WP order (one WP per implementer subagent)
WP-4844-P0 → WP-4844-P1 → WP-7623 → WP-7702 → WP-7825 → WP-6780 → WP-MISC

## Every loop iteration
1. Run state-full baseline; count PASS/FAIL (stdout vs stderr); bucket stateRoot vs expectException
2. Pick first WP not meeting exit criteria (see plan doc table)
3. Dispatch implementer subagent for that WP only:
   - codegraph_explore / read audit slice first
   - Minimal diff on product path (`applyEthMessage`, hooks, settlement); do NOT swap to evmone test harness
   - Add targeted unit test if it locks behavior
4. Verify WP slice manifest + `ctest -L specs-tests-smoke`
5. Report: `{loop#, WP, slice before→after, state-full fail count, next action}`
6. If same WP fail count unchanged for **2 consecutive loops** → BLOCKED report and STOP

## Build
- Dir: `build-bcos-evm-check`
- Configure: `-DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON`
- Runner: `build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests`
- EEST: `build-bcos-evm-check/_deps/evm_ref_eest_root`

## Constraints
- No force push; no amend pushed commits; no unrelated files
- No blockchaintest / statetest harness work unless required for current WP
- One git commit per completed WP: `fix(eth): eest <wp-id> parity`
- 4844 precheck = pool reject, not included INSUFFICIENT_BALANCE with SUCCESS

## Optional triage
- `EEST_PROBE=1` on single slice case
- `EthEestStateGranular <json-file> --fork-profiles eth-cancun`

Start: baseline → WP-4844-P0 implementer subagent.
```

---

## Subagent prompts (dispatch templates)

### WP-4844-P0 implementer

```markdown
Fix EEST blob tx pool-level reject (WP-4844-P0).

Symptom: `Expected exception 'TransactionException.*' but execution succeeded`
Target: `eth-eest-4844-insufficient-full.json` 144/144 pass (currently 0/144)

Investigate: EthMessageAdapter → applyEthMessage → EthStateTransitionHooks blob block,
EthFeeSettlement::buyGas blob debit, balance check vs geth reject semantics.

Must handle: INSUFFICIENT_ACCOUNT_FUNDS (144), INVALID_BLOB_VERSIONED_HASH (4),
INSUFFICIENT_MAX_FEE_PER_BLOB_GAS (2), ZERO_BLOBS (1), BLOB_COUNT (1).

Accept: insufficient-full 0 fail; blob-smoke 14/14; specs-tests-smoke green.
Do NOT fix stateRoot in this task (WP-4844-P1).
Audit: 2026-07-01-eth-vs-geth-parity.md §2.4, §2.6
```

### WP-4844-P1 implementer

```markdown
Fix EEST 4844 blob execution stateRoot (WP-4844-P1).

Prerequisite: WP-4844-P0 done (insufficient-full green).
Target: `eth-eest-4844-blobs-all.json` >95% pass (currently 202/1092).

Focus: blob gas settlement, buyGas/refund blob branch, execution state after type-3 tx.
Use EEST_PROBE=1 on failing cases; compare post-state vs expected.

Accept: blobs-all >1037/1092; no regression on insufficient-full or blob-smoke.
```

### WP-7623 / WP-7702 implementer

```markdown
Fix EEST stateRoot residual for {7623|7702} (WP-{7623|7702}).

Slice: `slices/eip{7623|7702}-state-full.json`
Current: {424/483|484/552} — target >95%
All failures are stateRoot only (no precheck).

Audit: 2026-07-01-eth-vs-geth-parity.md Phase 3 / 7702 / 7623 sections.
Minimal diff on TE path; verify with slice + specs-tests-smoke.
```

### WP-7825 implementer

```markdown
Fix Osaka EIP-7825 MaxTxGas pool reject (WP-7825).

Symptom: `GAS_LIMIT_EXCEEDS_MAXIMUM` expected but execution succeeded (11 cases).
Audit notes geth Osaka+ MaxTxGas not wired.

Accept: 7825 smoke green + full osaka/eip7825 dir 0 precheck fail.
```

### WP-6780 implementer

```markdown
Fix EIP-6780 same-tx SELFDESTRUCT stateRoot (WP-6780).

Target: `eth-eest-6780-all.json` >95% (currently 81/115, 34 stateRoot fail).
6780 smoke passes transitional only — full manifest gates stateRoot.

Focus: EthHost selfdestruct journal, same-tx CREATE+clear semantics.
```

---

## Loop output template

```markdown
### Loop {N} — {WP-id}

| Metric | Before | After |
|--------|--------|-------|
| state-full pass | | |
| state-full fail | | |
| WP slice pass | | |

**Fail buckets:** stateRoot {n} | expectException {n}

**Files changed:** …

**Commits:** …

**Next:** {WP-id or STOP}
```

---

## Realistic timeline (expectation setting)

| Phase | Loops (estimate) | Outcome |
|-------|------------------|---------|
| WP-4844-P0 | 1–3 | insufficient-full green |
| WP-4844-P1 | 3–6 | blobs-all >90% |
| WP-7623 + WP-7702 | 2–4 each | slices >95% |
| WP-7825 + WP-6780 | 1–3 each | slices >95% |
| WP-MISC + full green | 1–2 | 0 fail state-full |

**Do not expect one loop iteration to fix all 1065 failures.**

---

## Related files

| File | Role |
|------|------|
| `test/eth-eest-test/runners/eth/EthExecutionSpecStateTests.cpp` | Manifest runner |
| `test/eth-eest-test/src/EthMessageAdapter.cpp` | EEST → applyEthMessage |
| `test/eth-eest-test/src/StateTestAssert.cpp` | assertLevels / fail messages |
| `eth/apply/EthStateTransitionHooks.cpp` | Blob precheck, CanTransfer |
| `eth/settlement/EthFeeSettlement.cpp` | buyGas blob debit |
| `test/eth-eest-test/manifests/eth/eth-eest-4844-insufficient-full.json` | P0 acceptance |

**Last updated:** 2026-07-06

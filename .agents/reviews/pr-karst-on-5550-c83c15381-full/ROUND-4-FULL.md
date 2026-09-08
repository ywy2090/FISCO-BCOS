# Round 4 FULL at head c83c153813ab5a62a9d585dbb4f411d504aa172e

**Target:** Karst-on-#5550 (local branch `feat/karst-on-release-3.18`, no GitHub PR)
**base → head:** `f22bb8b96` → `c83c15381` (stack base = #5550 at stack time)
**diffstat:** 70 files, +4051/−895 (excluding `.agents`)
**Mode:** Full (fisco-review v1.20.1). Six module agents + aggregator post-pass. fisco-review-pr cannot bind a GitHub PR → formal artifact verdict `INCOMPLETE`.
**Conflict of interest:** this reviewer session authored `46f9bc9` / `c83c15381` and directed earlier Karst fixes. The HIGH below was found by a **blind** agent that did not receive known-items. Treat F15–F16/F18 as self-review; treat F17 as independently sourced then aggregator-verified.
**Prior findings:** F1–F13 + P-HIGH-F1 `fixed`; F14 `open` (review artifacts on the product branch).
**Convergence rule:** every prior finding fixed or rejected-with-verified-premise at an unchanged head, no new findings, every disclosed behaviour change verified against the code. Terminal APPROVE also requires this full-surface hunt to close with no new HIGH/BLOCKER. A new HIGH resets convergence.
**Round ceiling:** 4 (explicit 全量 after the scheduled ceiling of 3).
**Head re-checked immediately before this report:** still `c83c153813ab5a62a9d585dbb4f411d504aa172e`. Working tree dirty only for review directories.

## Prior-status table

| ID | Title | Status at c83c15381 |
|---|---|---|
| F1–F13, P-HIGH-F1 | (Rounds 1–3) | **fixed** — not re-derived; mechanisms re-quoted by module agents |
| F14 | Review artifacts committed (`30296bb3b`) | **open** |

## 1. Summary

This is a **behaviour-change** Karst landing (Osaka EVM + timestamp `OpForkSchedule` + Engine getPayload V4→V5), stacked on #5550. Execute-side Q5 (Jovian+ activation deposits-only) and the Engine profile gate are in place and match the disclosed triple (FCU V3 / newPayload V4 / getPayload V5 at Karst, keyed on payload/attrs timestamp). The full hunt found a **builder/executor half-fix**: FCU `buildOpPayload` still seals the mempool on an activation timestamp, then Q5 rejects the candidate with a hash-less `OpConsensusError` that the retry loop flattens to `-32603`. Karst Engine tests set `noTxPool=true` and cannot see it. Convergence **not** met.

## 2. Findings (open at this head)

### [F14] Review artifacts committed onto the product branch
**Severity:** LOW
**Origin:** INTRODUCED (`30296bb3b`)
**Scope:** build graph / repo hygiene
**Location:** `.agents/reviews/pr-karst-on-5550-46f9bc9a9/` (4 files)
**Evidence:** commit `30296bb3b` adds `ROUND-2.md`, `findings-round1.json`, `findings.json`, `review_karst-on-5550.json`.
**Problem / Why / Fix:** unchanged from Round 3. Drop `30296bb3b` or delete the four files before opening a PR.

### [F15] New metadata test truncates Apache grant
**Severity:** LOW
**Origin:** INTRODUCED
**Scope:** tests
**Location:** `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp:1`
**Evidence:**
```
/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "L2GenesisTestStorage.h"
```
**Problem:** Sibling 2026 ledger tests (`test_GenesisEthHeader.cpp`) carry the full Apache grant + `@file`/`@brief`. This file stops after SPDX. Also uses `kIsthmusJovianSchedule`.
**Why it matters:** License/Doxygen scanners that pass sibling tests fail this TU.
**Recommended fix:** Copy the sibling Apache block; rename the constant to `c_isthmusJovianSchedule`.

### [F16] Karst V5 tests never call newPayload
**Severity:** LOW
**Origin:** INTRODUCED
**Scope:** tests
**Location:** `engine/test/unittests/engine/OpEngineServiceParityTest.cpp:1062`
**Evidence:**
```
/// The full getPayload-response JSON shape as the CL sees it (Karst pairing
/// FCU V3 -> getPayload V5 -> newPayload V4): combineGetPayloadResponse must
...
auto parsed = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
```
**Problem:** The comment claims the Karst pairing includes `newPayload V4`. The test only JSON-parses as V4; it never calls `OpEngineService::newPayload`. `OpEngineKarstProfileTest.cpp` has zero `newPayload` sites. Caps pin `engine_forkchoiceUpdatedV4` absent, not `engine_newPayloadV5`.
**Why it matters:** If Karst `engineApiFor.newPayload` were bumped to V5 while the V4-only static gate stayed, every Karst `newPayload V4` would `-38005` and every engine Karst test would still pass.
**Recommended fix:** After the V5 JSON pin, `newPayload(parsed, 4)` and expect VALID; assert caps omit `engine_newPayloadV5`.

### [F17] FCU build still seals mempool user txs on Jovian/Karst activation
**Severity:** HIGH
**Origin:** INTRODUCED (Q5 is this stack; builder was not updated)
**Scope:** live production path
**Location:** `engine/bcos-engine/OpEngineService.inl:340-345` (build) vs `opstack-executor/OpBlockExecute.h:377-386` (Q5)
**Evidence:**
```
if (!payloadAttributes.noTxPool.value_or(false))
{
    sealView.newMutable();
    m_memPool.remove(sealView);
    m_memPool.seal(m_blockTxCountLimit, sealView, std::back_inserter(sealedTxs));
}
```
```
if (isNoUserTxActivationBlock(*schedule, parentTsSec, blockTsSec) &&
    hasNonDepositEnvelope(rawTxBytes))
{
    throw OpConsensusError(
        "op block: unexpected non-deposit transactions in fork activation block");
}
```
Retry flatten (`OpEngineService.inl:557-570`): `if (culprit.has_value() && …) continue;` else `OpExecutionInternalError` ("OP payload build execution failed"). Q5 uses the one-arg `OpConsensusError` ctor (`OpCommon.h:45`); `attachOpRejectInfo` only tags `OpCulpritTxHash` when `txHash.has_value()` (`OpScheduler.h:1335-1338`).
**Problem:** `buildOpPayload` never consults `isNoUserTxActivationBlock`. On an activation timestamp it still seals the mempool (unless `noTxPool==true`) and appends those envelopes after forced deposits (`inl:494-505`). Live execute then Q5-rejects the candidate. The reject has no culprit hash, so the eviction loop cannot drop pool txs.
**Why it matters:** `forkchoiceUpdatedV3` at the Jovian/Karst activation timestamp, `noTxPool` omitted or false, mempool non-empty — the default `value_or(false)` miner shape, and a first-class path (`OpEngineServiceParityTest` sets `noTxPool=false`). A CL that lists only deposits in `attrs.transactions` still gets pool user txs mixed in. Result: no `payloadId`, `-32603` loop, activation block cannot be produced until the pool is empty. Karst Engine tests set `noTxPool=true` (`OpEngineKarstTestHarness.h:590`) and cannot see this. newPayload of a user-tx activation block is correctly INVALID — this is EL-builder liveness vs the new consensus gate, not accept-too-loose execute.
**Recommended fix:** If `isNoUserTxActivationBlock(schedule, parentTsSec, attrsTsSec)`, skip `seal` (treat as `noTxPool`) and FCU-INVALID any non-deposit already in `attrs.transactions`. Do not try to retry-evict via culprit hashes. Add an Engine test with `noTxPool=false` + a sealed user tx at the Karst activation timestamp that expects a clean FCU answer (VALID + deposits-only `payloadId`, or INVALID), never `-32603`.

### [F18] Shared caps comment still claims Eth and Op advertise the same list
**Severity:** LOW
**Origin:** INTRODUCED (this stack made the sibling comment false)
**Scope:** comments (`EngineServiceCommon.cpp` is not in the Karst file list)
**Location:** `engine/bcos-engine/EngineServiceCommon.cpp:80`
**Evidence:**
```
// Eth and Op advertise the same list. FCU V4 is unimplemented (Endpoint -38005)
```
**Problem:** `supportedOpCapabilities()` now lists getPayload V4+V5 and newPayload V4 only (`OpEngineService.cpp:92-94`). The shared-comment claim is false.
**Why it matters:** The next reader of the shared helper will assume Op still advertises V1–V3 getPayload/newPayload.
**Recommended fix:** Narrow the comment to Eth `supportedCapabilities()` only.

Carry-forward LOWs confirmed still present, not re-derived: hash hex length, trailing comma `"0:isthmus,"`, release gate tracked-only, unconditional parent-header read, stored-vs-genesis ignore, unused Osaka `expected.bin`, self-oracle schedule hash, dual `"0:isthmus"`/`"0:jovian"`, op-geth file-header overclaim, dead newPayload version-mismatch branch.

## 3. Scorecard

| Dimension | Score /10 | Reason |
|---|---|---|
| Correctness | 6 | Q5/7825/profile live gates hold; F17 strands activation-block FCU on the seal path |
| Tests | 6 | Karst Engine suite cannot see F17 (`noTxPool=true`); F16 pairing comment is vacuous |
| Comments/readability | 7 | 7825/V3-drop comments match; F18 sibling comment is stale |
| Scope discipline | 6 | Product stack is coherent; checkpoint commit (F14) + builder half-fix (F17) |
| Description honesty | 8 | No GitHub PR. In-code comments disclose estimateGas sharing `call=true` and the Engine triple |
| **Overall** | **6.5 / 10** | **CHANGES_REQUESTED** (F17 HIGH) |

## 4. What this PR is doing

| Before (#5550 / Isthmus–Jovian) | After (this stack @ c83c15381) |
|---|---|
| Feature-flag / height fork hints | Timestamp `OpForkSchedule` persist + fail-closed hash |
| Jovian EVM | Karst = Osaka + 7825 deposit exemption |
| getPayload V4 | Profile: Karst getPayload V5 (`-38005` on mismatch); FCU stays V3; newPayload stays V4 |
| No activation deposits-only | Q5 on all Jovian+ activations in `preBlockOpSteps` / leftover `processOpBlock` |
| — | `eth_call`/`estimateGas` skip EIP-7825 (`call=true`) |
| — | OP caps drop getPayload V3 |

Intentional and verified: profile keys on payload/attrs timestamp (never head); internal ms → Unix seconds via `unixSecondsFromInternalMillis`; `TestBypass` does not enter Initializer; deposit-after-user off the activation window is warn-only.

## 5. Checked, and not a problem

1. **Execute Q5 scans every envelope.** `hasNonDepositEnvelope` / `hasNonDepositTx` (`OpBlockExecute.h:113-130`). DA 176B is still last-tx (`:394`), matching op-geth `CalcDAFootprint`.
2. **Live execute cannot skip Q5.** `OpScheduler` ctor throws on null schedule (`OpScheduler.h:405-408`); execute passes `m_schedule.get()` (`:1044-1045`).
3. **getPayload keys on payload timestamp, not head** (`OpEngineService.inl:117-120`). FCU attrs (`:154-158`). newPayload payload ts (`:677`).
4. **Caps vs live methods.** OP list is V4+V5 getPayload + V4 newPayload (`OpEngineService.cpp:92-94`). `engineApiFor` never returns getPayload V3 (`OpSchedulerSeam.h:95-110`).
5. **`maxEngineVersion=V4` does not cap V5.** `EngineServiceInitializer.h:122` `(void)maxEngineVersion`.
6. **EIP-7825.** Block path default-on (`state.hpp:172`); `m_prepare` sets `{.enforce_max_tx_gas = !call}` (`OpstackExecutor.h:1605`); deposits invert `deposit_exempt_from_max_tx_gas` (`OpTransition.cpp:531`). estimateGas sharing `call=true` is the accepted residual.
7. **Schedule hash fail-closed** (`ChainMetadata.h:135-137`). Karst without Jovian rejected (`OpForkScheduleCodec.h:158`).
8. **TestBypass stays out of boot.** Initializer uses `OpForkSchedule::parse` only (`Initializer.cpp:540-541`).
9. **Include-case / column-limit.** New public headers match their includes byte-for-byte. Hot C++ files are ≤100 columns; only the release-gate shell has three 106–108-char lines.
10. **`processOpBlock` has no production caller** (tests only). Q5 there uses `hasNonDepositTx` + already-converted Unix seconds.

## 6. Verification boundary

**Read:** materialized tree at `.agents/reviews/pr-karst-on-5550-c83c15381-full/tree` (verified against `c83c15381`). Line numbers are that tree.

**Agents (6):** [framework-ledger](ffa2d7a2-affd-4984-88d7-470e91af3934) → F15; [bcos-evm](2e27e9d2-e21e-428e-bca9-04c75a194ba4) zero; [opstack-executor](7b47a593-1018-4bfd-b00a-4db31e8856d2) zero; [engine](3d7c131b-0844-4aa6-88f7-04ef811e753a) → F16; [boot-rpc-tool](74dae833-01c1-4f68-bf17-0cec4ea64e3e) zero; [blind-consensus](82c9ef9f-4c44-4902-be2d-31c91da81eed) → F17. Every admitted finding re-quoted at tree `file:line`. Agent "could not determine" items resolved in-tree (genesis header time is not bound to activation stamps; hash hex length remains carry-forward).

**Aggregator post-pass:** sibling Q5 helpers, `engineApiFor` vs caps, `MAX_TX_GAS_LIMIT` single definition (`0x1000000`), `unixSecondsFromInternalMillis` at Engine/scheduler boundaries, `0x7e` / `kDepositTxType`.

**Built/run:** not re-executed this full round (Round 3 already ran the Karst suites at this same head). Those suites cannot catch F17.

**fisco-review-pr:** no GitHub PR → `INCOMPLETE`.

**Outside this review:** rebase onto current #5550 tip; drop `30296bb3b` before a PR; F17 must be fixed (with an Engine test that uses `noTxPool=false` on the activation timestamp) before a terminal APPROVE.

Reviewed with fisco-review v1.20.1
subagents: 6 agents (full)

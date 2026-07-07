# EEST Integration Matrix

> **Pin:** EEST `v5.4.0` (`assets/upstream-pins.json`)  
> **Baseline date:** 2026-07-07 (`build-bcos-evm-check`, manifest + granular harness)  
> **Reference:** [evmone](file:///Users/octopus/octo/code/blockchain-impl/evmone) `evmone-statetest` / `evmone-blockchaintest`  
> **Statetest spec:** `bcos-evm/docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md` (Approved)  
> **Harness plan:** `bcos-evm/docs/superpowers/plans/2026-07-07-eest-statetest-harness-h2-h7.md`  
> **Scope:** Eth reference path (`bcos-evm/test/eth-eest-test/`)

---

## 1. Executive Summary

| Corpus | EEST v5.4.0 | bcos-evm integration | evmone integration | Primary gap |
|--------|-------------|----------------------|--------------------|-------------|
| **State (native EIP dirs)** | 28 dirs across 10 forks | **15/28** manifest (**4140/4140**); granular **2722/2722** (H1–H7 + WP-HIST harness) | **Full tree** recursive scan | Manifest + default-profile granular **closed**; WP-HIST phase 2 = expand historical post coverage |
| **State (static GST)** | 58 suites under `static/state_tests/` | **19 suites** in `eth-eest-static-regression-full.json` | Included in statetest scan | Partial static coverage; no nightly full static |
| **Transaction tests** | `transaction_tests/prague/eip7702_set_code_tx` | **106/106 pass** (`eth-eest-tx-full.json`) | **No dedicated runner** | bcos ahead |
| **Blockchain tests** | 12 fork dirs + `static/` | M1 Cancun **2181/2181**; M4 **Berlin 282/282 · London 2/2 · Paris 46/46 · Shanghai 128/128** | **Full tree** | M2–M3 Prague/Osaka; engine format |
| **Blockchain engine/sync** | `blockchain_tests_engine*`, `blockchain_tests_sync` | **Not integrated** | Partial (engine variants) | Format + CL payload decoding |

**Bottom line:** Manifest state-full **4140/4140** and granular full-tree **2722/2722** (2026-07-07). Blockchain **M1 ACHIEVED** — Cancun **2181/2181** @ `f28d19659`; **M4 ACHIEVED** — Shanghai **128/128** + Berlin **282/282** + London **2/2** + Paris **46/46** (PoW `miningReward` + withdrawal post-diff). M2 Prague / M3 Osaka / M5 historical remain. Remaining: **WP-HIST phase 2**, optional H8 trace.

---

## 2. Runners vs evmone

| bcos-evm runner | Binary / CTest | Execution path | evmone equivalent | Integration mode |
|-----------------|----------------|----------------|-------------------|------------------|
| `EthExecutionSpecStateTests` | manifest-driven CTests | `EthMessageAdapter` → `applyEthMessage` | — | Curated manifest entries + `assertLevels` |
| `EthEestStateGranular` | `EthEestStateGranularSmoke` / `EthEestStateGranularFull` | Same (file-per-GTest) | `evmone-statetest` | H1 full tree CTest; H2–H7 harness (see §2.1) |
| `EthExecutionSpecTransactionTests` | `EthExecutionSpecTransactionTestsFull` | Tx decode / precheck (no EVM exec) | — | Full `transaction_tests/` dir |
| `EthEestBlockchainRunner` | `EthEestBlockchainSmoke` / `EthEestBlockchainFull` | `validateBlock` + `applyEthBlock` + MPT roots + invalid-block | `evmone-blockchaintest` | Smoke manifest (8 files); nightly full sweep |
| `EthEestBlockGranular` | `EthEestBlockGranularSmoke` / `EthEestBlockGranularFull` | Same via `BlockchainRunCore` (per-file GTest) | `evmone-blockchaintest` | Cancun filter smoke; nightly full tree |
| `EthGSTSmoke` / `EthGSTFull` | GST legacy | GST adapter | evmone also runs legacy GST | Separate from EEST native dirs |

### CTest labels

| Label | Purpose |
|-------|---------|
| `specs-tests-smoke` | PR gate (`capability-gate` CI) |
| `specs-tests-full` | Nightly / local full manifest sweeps |
| `eest-statetest` | Granular statetest harness (`EthEestStateGranular*`) |
| `nightly` | Full-tree granular (`EthEestStateGranularFull`) |
| `probe` | Diagnostic manifests (7702 slices, precompile probes) |
| `eest` | Any EEST-backed test |

### 2.1 Statetest harness capabilities (H1–H7, 2026-07-07)

| ID | Capability | bcos-evm | evmone equivalent | Status |
|----|------------|----------|-------------------|--------|
| H1 | Full-tree CTest | `EthEestStateGranularFull` | directory scan | ✅ |
| H2 | Slow-test default filter | `EestGranularSlowFilter.h` | `FLAGS_gtest_filter` in statetest.cpp | ✅ |
| H3 | Multi-path + `-k` | `EestGranularCli` | CLI paths + `-k` | ✅ |
| H4 | Per-case fork inference | `EestForkInference` + manifest profile map | post keys → `to_rev()` (no profile filter) | ✅ (diff: profile filter → SKIP) |
| H5 | Historical fork profiles | `ForkProfileRegistry` (`eth-homestead`, `eth-berlin`, `eth-london`, `eth-paris`) | `to_rev()` full history | ✅ default granular appends Homestead/Berlin; targeted `shouldRunWpHistPost` |
| H6 | Unsupported → SKIP | `tryLoadGeneralStateTestFile` | throw on bad JSON | ✅ |
| H7 | Failure bucket reports | `scan-eest-failures.py` + `bucket-failures.py` | — | ✅ |
| H8 | `--trace` | deferred | `--trace` VM option | ⏸ Phase 0 |

---

## 3. Native State Tests — Fork × EIP Directory Matrix

Legend:

| Symbol | Meaning |
|--------|---------|
| ✅ | In `eth-eest-state-full.json` + dedicated smoke manifest |
| 🟡 | Smoke/probe manifest only (not in state-full) |
| 🔵 | Granular-runnable (`EthEestStateGranular <dir>`); manifest-16 scan **0 fail** @ 2026-07-07 |
| ❌ | Not integrated; needs fork profile and/or parity work |
| — | Empty / no EIP dirs in pin |

### 3.1 Per-directory matrix

| Fork | EIP directory | Capability row | Manifest | Smoke CTest | Full baseline | Granular | evmone statetest |
|------|---------------|----------------|----------|-------------|---------------|----------|------------------|
| **shanghai** | `eip3651_warm_coinbase` | `eip3651-warm-coinbase` | ✅ state-full | `EthExecutionSpec3651WarmCoinbaseSmoke` | 16/16 | 🔵 | ✅ auto |
| | `eip3855_push0` | `eip3855-push0` | ✅ | `EthExecutionSpec3855Push0Smoke` | 10/10 | 🔵 | ✅ |
| | `eip3860_initcode` | `eip3860-initcode` | ✅ | `EthExecutionSpec3860InitcodeSmoke` | 60/62 | 🔵 | ✅ |
| **cancun** | `eip1153_tstore` | `eip1153-tstore` | ✅ | `EthExecutionSpec1153TstoreSmoke` | 123/123 | 🔵 | ✅ |
| | `eip4844_blobs` | `eip4844-blobs` | ✅ | `EthExecutionSpec4844BlobSmoke` + probe manifests | **1092/1092** | 🔵 | ✅ |
| | `eip5656_mcopy` | `eip5656-mcopy` | ✅ | `EthExecutionSpec5656McopySmoke` | 93/93 | 🔵 | ✅ |
| | `eip6780_selfdestruct` | `eip6780-selfdestruct-kernel` | ✅ | `EthExecutionSpec6780Smoke` | **115/115** | 🔵 | ✅ |
| | `eip7516_blobgasfee` | `eip4844-blobs` (shared) | ✅ | — | 4/4 | 🔵 | ✅ |
| **prague** | `eip2537_bls_12_381_precompiles` | `eip2537-bls-precompiles` | ✅ | `EthExecutionSpec2537BlsSmoke` | 975/975 | 🔵 | ✅ |
| | `eip7623_increase_calldata_cost` | `eip7623-calldata-cost`¹ | ✅ | state-smoke (1 vector) | **483/483** | 🔵 | ✅ |
| | `eip7702_set_code_tx` | `eip7702-set-code-tx` | ✅ | state-smoke + many probes | **552/552** | 🔵 | ✅ |
| **osaka** | `eip7823_modexp_upper_bounds` | `eip7823-modexp` | ✅ | state-smoke (1 vector) | **23/23** | 🔵 | ✅ |
| | `eip7825_transaction_gas_limit_cap` | `eip7825-gas-limit-cap` | ✅ | `EthExecutionSpec7825GasLimitCapSmoke` | **35/35** | 🔵 | ✅ |
| | `eip7883_modexp_gas_increase` | `eip7823-modexp` (related) | ✅ | `EthExecutionSpec7883ModexpGasSmoke` | 160/160 | 🔵 | ✅ |
| | `eip7951_p256verify_precompiles` | `eip7212-p256-precompile` | ✅ | `EthExecutionSpec7951P256Smoke` | 397/397 | 🔵 | ✅ |
| | `eip7594_peerdas` | — | ❌ | — | — | 🔵 | ✅ auto |
| | `eip7939_count_leading_zeros` | — | ❌ | — | — | 🔵 | ✅ auto |
| **berlin** | `eip2929_gas_cost_increases` | `eip2929-runtime-warm`² | ❌ | — | — | 🔵 H5 | ✅ auto |
| | `eip2930_access_list` | `eip2929-runtime-warm`² | ❌ | — | — | 🔵 H5 | ✅ auto |
| **london** | `eip1559_fee_market_change` | `eip1559-settlement`² | 🟡 | `EthExecutionSpec1559GaspriceProbe` (static `stExample/eip1559`) | 1/1 probe | 🔵 | ✅ auto |
| **istanbul** | `eip1344_chainid` | — | ❌ | — | — | 🔵 | ✅ auto |
| | `eip152_blake2` | `builtin-precompiles-kernel`² | ❌ | precompile-probe (7702 slice) | — | 🔵 | ✅ auto |
| **paris** | `eip7610_create_collision` | — | ❌ | — | — | 🔵 H5 | ✅ auto |
| **byzantium** | `eip196_ec_add_mul` | — | ❌ | — | — | 🔵 | ✅ auto |
| | `eip197_ec_pairing` | — | ❌ | — | — | 🔵 | ✅ auto |
| | `eip198_modexp_precompile` | — | ❌ | — | — | 🔵 | ✅ auto |
| **constantinople** | `eip1014_create2` | — | ❌ | — | — | 🔵 | ✅ auto |
| | `eip145_bitwise_shift` | — | ❌ | — | — | 🔵 | ✅ auto |
| **frontier** | `create`, `opcodes`, `precompiles`, … | — | ❌ | — | — | 🔵 granular | ✅ auto |
| **homestead** | `coverage`, `identity_precompile` | — | ❌ | — | — | 🔵 granular | ✅ auto |

¹ Capability row `eip7623-calldata-cost` exists; state-full entry currently tags `eip2929-runtime-warm` (manifest metadata drift).  
² Covered indirectly via static regression or probe manifests, not native EIP directory.

**Counts:** 28 native EIP dirs in pin → **15 integrated in state-full** (manifest JSON entries) → **13 not in state-full** (46%).

### 3.2 Aggregate full-run baseline (`eth-eest-state-full.json`)

Verified **2026-07-07** on `build-bcos-evm-check` (EEST v5.4.0). Runner: `EthExecutionSpecStateTests`.

| Slice | Executed | Pass | Fail | Pass rate | Notes |
|-------|----------|------|------|-----------|-------|
| **Total (15 entries)** | **4140** | **4140** | **0** | **100%** | Parity loop closed 2026-07-07 |
| All manifest dirs | — | — | 0 | 100% | incl. 4844, 7623, 7702, 6780, 7825 |

**Granular manifest-16 scan** (`scan-eest-failures.py --manifest-16`, 2026-07-07): **0 subtest failures**, 210 JSON files.

**Granular full-tree scan** (`scan-eest-failures.py --granular-full`, 2026-07-07): **2722/2722 JSON files clean**, 0 subtest failures. Default profiles: manifest (Shanghai/Cancun/Prague/Osaka) + `eth-homestead` + `eth-berlin`; historical posts filtered via `shouldRunWpHistPost`. Nightly CI: `workflow-specs-tests-nightly.yml` (hard gate).

Historical (2026-07-06): **3075/4140 (74.3%)** — superseded.

**Core slice** (7623 + 7823 + 7702 — historical README口径): **931/1058 (88.0%)**.  
Historical (2026-06-21): 519/1056 (49%) on same three dirs — superseded.

---

## 4. Static State Tests (`fixtures/state_tests/static/`)

EEST ships **58** legacy GST suites under `static/state_tests/`.

| Integration | Manifest | CTest | Suites covered |
|-------------|----------|-------|----------------|
| Smoke (7 files) | `eth-eest-static-regression-smoke.json` | `EthExecutionSpecStaticRegressionSmoke` | Cancun×2, Shanghai×2, stChainId, stSelfBalance, stBugs |
| Full (19 dirs) | `eth-eest-static-regression-full.json` | `EthExecutionSpecStaticRegressionFull` | Cancun, Shanghai, stEIP1559, stEIP2930, stCreate2, stRevertTest, stRefundTest, stSStoreTest, stCreateTest, stInitCodeTest, stExtCodeHash, stDelegatecallTestHomestead, stCallCodes, stStaticCall, stSelfBalance, stChainId, stBadOpcode, stReturnDataTest, stLogTests, stBugs |
| Nonce smoke (4) | `eth-eest-nonce-smoke.json` | `EthExecutionSpecNonceTests` | stRevertTest×2, stCreateTest×2, 7702 nonce |
| 1559 probe (1) | `eth-eest-1559-gasprice-probe.json` | `EthExecutionSpec1559GaspriceProbe` | stExample/eip1559 |

**Not integrated:** ~39 static suites (VMTests, stAttackTest, stMemoryStressTest, stEIP150*, homestead-specific, etc.)

| evmone | bcos-evm |
|--------|----------|
| All static JSON picked up by recursive statetest scan | Manifest-only subset; no nightly full static sweep |

---

## 5. Transaction Tests

| Path | bcos-evm | evmone |
|------|----------|--------|
| `fixtures/transaction_tests/prague/eip7702_set_code_tx/` | ✅ `eth-eest-tx-full.json` — **106/106 pass** | ❌ no runner |

| Manifest | CTest | Label |
|----------|-------|-------|
| `eth-eest-tx-smoke.json` (5 vectors) | `EthExecutionSpecTransactionTests` | `specs-tests-smoke` |
| `eth-eest-tx-full.json` (full dir) | `EthExecutionSpecTransactionTestsFull` | `specs-tests-full` |

---

## 6. Blockchain Tests

| Corpus dir | evmone | bcos-evm runner | CI today | Blockers |
|------------|--------|-----------------|----------|----------|
| `blockchain_tests/{fork}/` | Full scan | `EthEestBlockchainRunner` | `EthEestBlockchainSmoke` manifest (PR); `EthEestBlockchainFull` nightly (`continue-on-error`) | M2–M5 forks; engine format |
| `blockchain_tests/static/` | Full scan | same | nightly full sweep (non-blocking) | static blockchain parity |
| `blockchain_tests_engine/` | partial | ❌ | — | `engineNewPayloads` decode |
| `blockchain_tests_engine_x/` | partial | ❌ | — | extended engine format |
| `blockchain_tests_sync/` | partial | ❌ | — | CL sync vectors |

`EthEestBlockGranular` runs per-file execution via shared `BlockchainRunCore` (loader + `validateBlock` + `applyEthBlock` + MPT checks). `EthEestBlockGranularFull` mirrors CLI nightly sweep.

### M1 baseline — Cancun (`blockchain_tests/cancun/`, 2026-07-07)

| Metric | bcos-evm | evmone target | M1 gate |
|--------|----------|---------------|---------|
| Corpus size | 105 files / 2181 subtests | full tree | — |
| CLI case pass rate | **2181 / 2181 = 100%** | ~100% | ≥90% — **ACHIEVED** |
| Granular file pass rate | **105 / 105 = 100%** | ~100% | ≥90% — **ACHIEVED** |
| Crash-free nightly | `EthEestBlockchainFull` + `EthEestBlockGranularFull` CTests wired | — | ✅ (failures allowed) |

Verified **2026-07-07** on `build-bcos-evm-check` (EEST v5.4.0). Commit `f28d19659`.

**Parity fixes (M1 closure):**

| Cluster | Root cause | Fix |
|---------|------------|-----|
| EIP-4788 beacon (~61) | System call hit EIP-1559 precheck; calldata used `prevRandao` not `parentBeaconBlockRoot` | `isCall` fee bypass; `BlockInfo.parentBeaconBlockRoot` + loader wiring |
| receiptsRoot (~1300+) | Typed receipt missing EIP-2718 prefix; included vmerr encoded as success | `ReceiptForRoot.txType`; `encodeReceipt` prefix; `receiptsForRoot` failure bit |
| blob gas (~17) | `blobBaseFee` unset; valid path skipped `blockInfoForExecution` | `calcBlobBaseFee(excessBlobGas)`; unified exec block info on both paths |
| RLP-only invalid (4) | No structured header → false `INVALID_BLOCK_PARENT` | `hasStructuredHeader` + Level-1 `RLP_STRUCTURES_ENCODING` pass |
| UINT64_MAX timestamp (16) | int64 compare on `timestamp` | Rule #7 uint64 comparison |
| EIP-6780 cross-tx (6) | Empty accounts lingered in `preStatePairs` after SELFDESTRUCT | `mergeStateDiffAccount` + EIP-158 prune between txs |

Historical baseline (pre-M1): **302 / 2181 = 13.8%** case rate — superseded.

### M4 baseline — Shanghai (`blockchain_tests/shanghai/`, 2026-07-07)

| Metric | bcos-evm | M4 gate (Shanghai subset) |
|--------|----------|---------------------------|
| Corpus size | 20 files / 128 subtests | — |
| CLI case pass rate | **128 / 128 = 100%** | ≥80% — **ACHIEVED** |
| Granular file pass rate | **20 / 20 = 100%** | ≥80% — **ACHIEVED** |

**Fix:** withdrawal balance credit moved to **after** `buildPostStateView` (geth: post-tx, not overwritten by `accumulatedDiff`). Affected `eip4895_withdrawals` CREATE/SELFDESTRUCT + withdrawal combos (3 cases).

### M4 baseline — Berlin / London / Paris (`blockchain_tests/{berlin,london,paris}/`, 2026-07-07)

| Fork | Files | Cases | Pass rate |
|------|-------|-------|-----------|
| Berlin | 7 | 282 | **100%** |
| London | 2 | 2 | **100%** |
| Paris | 3 | 46 | **100%** |

**Fix:** PoW block reward via `miningReward()` — evmone schedule: &lt;Byzantium 5 ETH, &lt;Constantinople 3 ETH, &lt;Paris 2 ETH, Paris+ none. Credited to coinbase after `buildPostStateView` (pre-merge fixtures expect reward + tx fees in stateRoot). Root cause of 144 Berlin + 1 London `stateRoot` mismatches.

### Milestones (blockchain parity loop)

| Milestone | Fork / scope | Gate | Status |
|-----------|--------------|------|--------|
| **M1** | Cancun | ≥90% standard-format files; PR smoke manifest | **ACHIEVED** — 105/105 file / 2181/2181 case (2026-07-07) |
| **M2** | Prague+ | `requestsHash` (EIP-7685) + system-contract collection | XFAIL-guarded; `computeRequestsHash` implemented |
| **M3** | Osaka+ | EIP-7918 blob-base-fee refinement | deferred |
| **M4** | Shanghai+ dirs | ≥80% withdrawal + multi-fork | **ACHIEVED** — Shanghai 128/128; Berlin 282/282; London 2/2; Paris 46/46 (2026-07-07) |
| **M5** | Reorg / PoW | canonical tip + TD semantics | deferred |

M2–M5 are Phase 4.x parity loop items; nightly blockchain sweep records baseline without blocking CI.

### Known blockchain runner gaps (vs evmone-blockchaintest)

1. Skips fixtures without parsed `pre` + `genesisBlockHeader` (engine-only format).
2. Blocks with RLP hex but no parsed txs/headers → skip.
3. `postState` full account diff marked "Phase 2".
4. No Amsterdam / BPO transition networks (`OsakaToBPO1AtTime15k`, etc.).
5. **M2 / B2 — Prague+ `requestsHash`:** `computeRequestsHash` (EIP-7685 SHA-256) implemented; runner validates only when `BlockApplyResult.requests` is non-empty. System-contract request collection not wired → XFAIL skip (does not block M1 Cancun).

---

## 7. Capability Rows ↔ Manifest Mapping

16 capability rows in `manifests/capability-rows.json`:

| Capability row | Primary native dir | Manifest entry | Smoke CTest | Notes |
|----------------|-------------------|----------------|-------------|-------|
| `eip2929-runtime-warm` | berlin/eip2929 (native ❌) | state-full tags 7623/7823/7702 | state-smoke | Indirect / metadata drift on 7623 |
| `eip6780-selfdestruct-kernel` | cancun/eip6780 | state-full | `6780` smoke | smoke = transitional only |
| `builtin-precompiles-kernel` | istanbul/eip152 (native ❌) | precompile-probe | probe | 7702→precompile slice |
| `eip1559-settlement` | london/eip1559 (native ❌) | 1559-gasprice-probe | `1559-gasprice` | static stExample, not native dir |
| `eip7702-set-code-tx` | prague/eip7702 | state-full + tx-full | many | best-covered EIP |
| `eip2537-bls-precompiles` | prague/eip2537 | state-full | `2537` smoke | |
| `eip1153-tstore` | cancun/eip1153 | state-full | `1153` smoke | stateRoot probe manifests exist |
| `eip5656-mcopy` | cancun/eip5656 | state-full | `5656` smoke | |
| `eip3855-push0` | shanghai/eip3855 | state-full | `3855` smoke | |
| `eip3860-initcode` | shanghai/eip3860 | state-full | `3860` smoke | |
| `eip3651-warm-coinbase` | shanghai/eip3651 | state-full | `3651` smoke | |
| `eip4844-blobs` | cancun/eip4844 + eip7516 | state-full + 4844 probes | `4844` smoke | multiple probe manifests |
| `eip7212-p256-precompile` | osaka/eip7951 | state-full | `7951` smoke | row id 7212, dir 7951 |
| `eip7623-calldata-cost` | prague/eip7623 | slice manifest | state-smoke (1 vec) | **424/483** full |
| `eip7823-modexp` | osaka/eip7823 + eip7883 | state-full | smoke + `7883` smoke | **23/23** on 7823 |
| `eip7825-gas-limit-cap` | osaka/eip7825 | state-full | `7825` smoke | |

---

## 8. Manifest Catalog (Eth)

| Manifest | Type | CTest target | Label |
|----------|------|--------------|-------|
| `eth-eest-state-smoke.json` | state | `EthExecutionSpecStateTests` | smoke |
| `eth-eest-state-full.json` | state | `EthExecutionSpecStateTestsFull` | full |
| `eth-eest-tx-smoke.json` | tx | `EthExecutionSpecTransactionTests` | smoke |
| `eth-eest-tx-full.json` | tx | `EthExecutionSpecTransactionTestsFull` | full |
| `eth-eest-7702-core-smoke.json` | state | `EthExecutionSpec7702CoreSmoke` | smoke |
| `eth-eest-6780-smoke.json` | state | `EthExecutionSpec6780Smoke` | smoke |
| `eth-eest-6780-all.json` | state | (manual / nightly) | full |
| `eth-eest-6780-callcode-probe.json` | probe | (manual) | probe |
| `eth-eest-2537-bls-smoke.json` | state | `EthExecutionSpec2537BlsSmoke` | smoke |
| `eth-eest-1153-tstore-smoke.json` | state | `EthExecutionSpec1153TstoreSmoke` | smoke |
| `eth-eest-1153-stateRoot-probe.json` | probe | manual | probe |
| `eth-eest-1153-stateRoot-single-probe.json` | probe | manual | probe |
| `eth-eest-5656-mcopy-smoke.json` | state | `EthExecutionSpec5656McopySmoke` | smoke |
| `eth-eest-3855-push0-smoke.json` | state | `EthExecutionSpec3855Push0Smoke` | smoke |
| `eth-eest-3860-initcode-smoke.json` | state | `EthExecutionSpec3860InitcodeSmoke` | smoke |
| `eth-eest-3651-warm-coinbase-smoke.json` | state | `EthExecutionSpec3651WarmCoinbaseSmoke` | smoke |
| `eth-eest-4844-blob-smoke.json` | state | `EthExecutionSpec4844BlobSmoke` | smoke |
| `eth-eest-4844-blobs-all.json` | state | manual | full |
| `eth-eest-4844-*-probe.json` (×5) | probe | probe CTests | probe |
| `eth-eest-7951-p256-smoke.json` | state | `EthExecutionSpec7951P256Smoke` | smoke |
| `eth-eest-7883-modexp-gas-smoke.json` | state | `EthExecutionSpec7883ModexpGasSmoke` | smoke |
| `eth-eest-7825-gas-limit-cap-smoke.json` | state | `EthExecutionSpec7825GasLimitCapSmoke` | smoke |
| `eth-eest-1559-gasprice-probe.json` | static | `EthExecutionSpec1559GaspriceProbe` | smoke |
| `eth-eest-nonce-smoke.json` | static+7702 | `EthExecutionSpecNonceTests` | smoke |
| `eth-eest-static-regression-smoke.json` | static | `EthExecutionSpecStaticRegressionSmoke` | smoke |
| `eth-eest-static-regression-full.json` | static | `EthExecutionSpecStaticRegressionFull` | full |
| `eth-eest-probe-{revert,return,oog,invalid}.json` | probe | probe CTests | probe |
| `eth-eest-precompile-probe*.json` | probe | probe CTests | probe |
| `eth-eest-opcode-probe.json` | probe | probe CTest | probe |
| `probe-gas-cost-one.json` | probe | probe CTest | probe |
| `slices/eip7623-state-full.json` | slice | `EthExecutionSpecSliceEip7623` | full |
| `slices/eip7702-state-full.json` | slice | `EthExecutionSpecSliceEip7702` | full |
| `slices/eip7823-state-full.json` | slice | `EthExecutionSpecSliceEip7823` | full |

---

## 9. Fork Profile Coverage

`ForkProfileRegistry` profiles vs EEST `network` field:

| EEST fork / network | Profile | State manifest | Blockchain | Notes |
|---------------------|---------|----------------|------------|-------|
| Shanghai | `eth-shanghai` | ✅ | ✅ blockchain **128/128** | M4 Shanghai closed 2026-07-07 |
| Cancun | `eth-cancun` | ✅ | ✅ blockchain **2181/2181** | M1 closed 2026-07-07 |
| Prague | `eth-prague` | ✅ (2537) | 🔵 | 7623/7702 run at `eth-osaka` profile |
| Osaka | `eth-osaka` | ✅ | 🔵 | |
| Homestead | `eth-homestead` (H5) | ❌ manifest | 🔵 granular | Default granular profile; targeted post filter |
| Berlin | `eth-berlin` (H5) | ❌ manifest | ✅ blockchain **282/282** | M4 closed 2026-07-07 |
| London | `eth-london` (H5) | static/probe only | ✅ blockchain **2/2** | M4 closed 2026-07-07 |
| Paris / Merge | `eth-paris` (H5) | ❌ | ✅ blockchain **46/46** | M4 closed 2026-07-07 |
| Istanbul / Byzantium / Constantinople | ❌ missing | ❌ | 🔵 | No dedicated profile; granular via manifest-fork posts |
| Amsterdam | ❌ missing | ❌ | ❌ | evmone supports; bcos does not |
| OsakaToBPO1AtTime15k, BPO*, BPO2ToAmsterdamAtTime15k | ❌ missing | ❌ | ❌ | evmone `blob_schedule.cpp` |

---

## 10. evmone vs bcos-evm — Integration Model

```
evmone                          bcos-evm
─────────────────────────────────────────────────────────
fixtures/state_tests/           fixtures/state_tests/
  └─ recursive scan               ├─ manifest (15 dirs, 4140/4140 PR/nightly)
     (all JSON)                   └─ EthEestStateGranularFull (H1–H7; nightly)
                                   └─ EthEestStateGranularSmoke (cancun PR)

fixtures/blockchain_tests/      fixtures/blockchain_tests/
  └─ full runner                  └─ EthEestBlockchainRunner (Cancun 2181/2181; M4 Berlin/London/Paris/Shanghai)
                                   └─ EthEestBlockGranularFull (nightly)

(no tx runner)                  fixtures/transaction_tests/
                                  └─ 106/106 pass

test/state::transition          EthMessageAdapter → applyEthMessage
(non-product path)              (product TE path — higher fidelity, harder parity)
```

| Dimension | evmone | bcos-evm |
|-----------|--------|----------|
| Fixture pin | v5.4.0 CI; bal@v5.6.1 on some jobs | v5.4.0 |
| Discovery | Zero-config directory scan | Manifest + granular (H3 multi-path, H4 fork inference) |
| PR gate | gtest_filter exclusions | `specs-tests-smoke` (39 CTests @ 2026-07-07) |
| Nightly | Full corpus | `specs-tests-full` + `--granular-full` gate; manifest **4140/4140**, granular **2722/2722** |
| Failure taxonomy | GTest pass/fail | H7 bucket reports; manifest `assertLevels` (no logsHash in granular default) |
| OPStack | — | Parallel `opstack/` manifests + skip-list |

---

## 11. Work Backlog to Reach "Full EEST" (evmone parity + product path)

### P0 — Wiring (days)

- [x] CTest: `EthEestStateGranularFull` (H1, nightly)
- [x] Slow-test filter aligned with evmone (H2)
- [x] CLI multi-path + `-k` (H3)
- [x] Failure bucket reports (H7)
- [x] CTest: `EthEestBlockchainRunner --fixtures .../blockchain_tests` (no limit, nightly; `workflow-specs-tests-nightly.yml` `continue-on-error`)
- [x] CI job artifact upload for `--granular-full` reports (`workflow-specs-tests-nightly.yml`)
- [x] Nightly `--granular-full` hard gate (`workflow-specs-tests-nightly.yml`, 2026-07-07)
- [ ] Pin bump policy (track evmone v5.6.x bal)

### P0 — Parity (manifest — closed 2026-07-07)

- [x] `eip4844_blobs` — **1092/1092**
- [x] `eip7623_increase_calldata_cost` — **483/483**
- [x] `eip7702_set_code_tx` — **552/552**
- [x] `eip6780_selfdestruct` — **115/115**
- [x] `eip7825_transaction_gas_limit_cap` — **35/35**
- [x] `eip7823_modexp_upper_bounds` — **23/23**
- [x] state-full aggregate — **4140/4140**
- [x] Full-tree granular nightly gate — **2722/2722** @ default profiles (2026-07-07)
- [x] WP-HIST harness phase 1 — `eth-homestead` profile, default append Homestead/Berlin, `shouldRunWpHistPost`, revision-aware GST state root
- [ ] WP-HIST phase 2 — append London/Paris to defaults; relax post filters; fix parity as new posts run
- [ ] Execution trace Phase 0 / H8 `--trace`

### P1 — Blockchain harness

- [x] Cancun M1 parity — **2181/2181** (`f28d19659`, 2026-07-07)
- [x] EIP-4788 block system calls (`parentBeaconBlockRoot`, `isCall` bypass)
- [x] Typed receipt MPT roots (EIP-2718 prefix + included-vmerr status)
- [x] Blob tx execution context (`blobBaseFee` from `excessBlobGas`)
- [x] RLP-only invalid block Level-1 handling
- [x] Cross-tx EIP-158 state merge (EIP-6780 selfdestruct recreate)
- [x] Shanghai M4 parity — **128/128** (withdrawal after diff merge)
- [x] Berlin/London/Paris M4 parity — **282/282 + 2/2 + 46/46** (PoW `miningReward`)
- [ ] RLP block decoding path in `EthEestBlockchainRunner` (beyond rlp_decoded fixtures)
- [ ] `engineNewPayloads` / engine sync formats
- [ ] Full `postState` account diff
- [ ] Prague+ blockchain dirs (M2–M3)

### P2 — Fork / corpus expansion

- [ ] Add manifest rows or rely on granular for: `eip7594`, `eip7939`, byzantium dirs
- [ ] WP-HIST phase 2: London/Paris default profiles + broader historical post coverage
- [ ] `Amsterdam` + BPO transition profiles
- [ ] Static suite full sweep (58 suites) or document intentional subset
- [ ] `blockchain_tests_engine*` integration

### P3 — Hygiene

- [ ] Fix capability row tagging on 7623 entries (`eip7623-calldata-cost` vs `eip2929-runtime-warm`)
- [x] Align slow-test exclusion list with evmone `gtest_filter` (H2)
- [ ] Auto-generate manifest entries from granular run metadata (optional)

---

## 12. Quick Commands

```bash
# Configure
cmake -S . -B build-ref -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
cmake --build build-ref --target EthEestStateGranular EthExecutionSpecStateTestsFull

# PR smoke
ctest -L 'specs-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Nightly manifest full (current partial corpus)
ctest -L 'specs-tests-full' --test-dir build-ref -C Debug --output-on-failure

# Nightly granular full tree
ctest -R EthEestStateGranularFull --test-dir build-ref -C Debug --output-on-failure

# Shanghai blockchain sweep (128/128)
./build-ref/bcos-evm/test/eth-eest-test/EthEestBlockchainRunner \
  --fixtures $EEST_ROOT/fixtures/blockchain_tests/shanghai

# Nightly blockchain full sweep (Cancun 2181/2181; failures non-blocking in CI)
./build-ref/bcos-evm/test/eth-eest-test/EthEestBlockchainRunner \
  --fixtures $EEST_ROOT/fixtures/blockchain_tests/cancun

ctest -L 'specs-tests-full' -R 'BlockchainFull|BlockGranularFull' --test-dir build-ref -C Debug --output-on-failure

# Failure bucket scan (manifest-16 dirs)
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16

# Full-tree granular scan (nightly gate)
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --granular-full --build-dir build-ref

# evmone-style directory scan (default manifest + Homestead/Berlin profiles)
./build-ref/bcos-evm/test/eth-eest-test/EthEestStateGranular \
  $EEST_ROOT/fixtures/state_tests

# Per-slice debugging
ctest -R EthExecutionSpecSliceEip7823 -V --test-dir build-ref -C Debug
```

---

## 13. Document Maintenance

| Field | Update when |
|-------|-------------|
| Baseline pass/fail tables | After each parity sprint; rerun `EthExecutionSpecStateTestsFull` |
| Statetest integration spec | `docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md` |
| Native dir inventory | EEST pin bump (`upstream-pins.json`) |
| Manifest catalog | New manifest added to `CMakeLists.txt` |
| Fork profiles | `ForkProfileRegistry.cpp` changes |

| Harness plan | `docs/superpowers/plans/2026-07-07-eest-statetest-harness-h2-h7.md` |
| Parity loop reports | `docs/superpowers/plans/2026-07-06-eest-parity-loop-*-report.md` |

**Last updated:** 2026-07-07 (M4 **ACHIEVED**: Berlin 282/282 + London 2/2 + Paris 46/46 + Shanghai 128/128; M1 Cancun 2181/2181)

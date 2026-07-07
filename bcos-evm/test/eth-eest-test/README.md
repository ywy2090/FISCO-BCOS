# specs-tests

EVM reference vector runners for geth / op-geth parity evidence.

## Layout

| Directory | Role |
|-----------|------|
| `src/` | Library code (loaders, matchers, adapters) |
| `include/bcos-evm/specs-tests/` | Public headers |
| `runners/` | CTest entrypoints only (GST/EEST smoke & full runners) |
| `test/` | Unit tests for loaders, matchers, adapters |
| `manifests/` | JSON manifest fixtures |
| `assets/` | Upstream pin metadata |


Reference tests are opt-in and do not affect default builds:

```bash
cmake -S . -B build-ref -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
cmake --build build-ref --target bcos-evm-specs-tests-core
```

Pinned fixtures (`ethereum/tests` GST + EEST) are downloaded at **configure time** via CMake `FetchContent`, driven by `assets/upstream-pins.json`. No git submodule and no manual fetch script required.

To disable configure-time download (use pre-populated trees or env overrides):

```bash
cmake -S . -B build-ref -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON \
  -DBCOS_EVM_SPECS_TESTS_FETCH_ASSETS=OFF
export ETHEREUM_TESTS_ROOT=/path/to/parent-of-GeneralStateTests
export EEST_ROOT=/path/to/eest-root-containing-fixtures
```

Optional runtime overrides (take precedence over compile-time defaults):

```bash
export ETHEREUM_TESTS_ROOT=/path/to/parent-of-GeneralStateTests
export EEST_ROOT=/path/to/eest-root-containing-fixtures
```

Pin versions and upstream metadata in `assets/upstream-pins.json`.

## Fork extension

Add fork profiles in `ForkProfileRegistry` (see implementation plan Part A Task 5).

Osaka smoke (`eth-gst-osaka-smoke.json`) executes at `EVMC_OSAKA` while using `postFork: Prague` expectations until the pinned `ethereum/tests` commit includes Osaka post entries. EEST fixtures provide Osaka-native vectors for nightly runs (Task 15).

### Manifests

| Manifest | Scope | CTest label |
|----------|-------|-------------|
| `eth-eest-state-smoke.json` | Curated EEST state vectors (7623/7823/7702 warming + full self-sponsored 10 variants + invalid auth) | `specs-tests-smoke` |
| `eth-eest-tx-smoke.json` | Curated 7702 tx validation (RLP + empty auth + invalid chain id/nonce/signature) | `specs-tests-smoke` |
| `eth-eest-7702-core-smoke.json` | 7702 core state (eip7702, empty auth, create, delegation, pointer, nonce overflow) | `specs-tests-smoke`, `7702` |
| `eth-eest-6780-smoke.json` | Cancun EIP-6780 same-tx SELFDESTRUCT (3 vectors; transitional only — stateRoot parity pending) | `specs-tests-smoke`, `6780` |
| `eth-eest-1559-gasprice-probe.json` | EIP-1559 `GASPRICE`/`BASEFEE` state probe (`stExample/eip1559`) | `specs-tests-smoke`, `1559-gasprice` |
| `eth-eest-nonce-smoke.json` | Nonce semantics: REVERT precompile touch, CREATE high nonce, 7702 nonce validity | `specs-tests-smoke`, `nonce` |
| `eth-eest-state-full.json` | Full native EIP dirs in manifest (15 entries: Shanghai→Osaka) | `specs-tests-full` |
| `eth-eest-tx-full.json` | Full Prague 7702 transaction_tests dir | `specs-tests-full` |

### Granular runners (evmone-statetest parity)

Harness spec: `bcos-evm/docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md` (Approved 2026-07-07).  
Implementation plan: `bcos-evm/docs/superpowers/plans/2026-07-07-eest-statetest-harness-h2-h7.md`.

| CTest | Scope | Labels |
|-------|-------|--------|
| `EthEestStateGranularSmoke` | `state_tests/cancun` + gtest filter | `specs-tests-smoke` |
| `EthEestStateGranularFull` | `${EEST}/fixtures/state_tests` full tree (recursive) | `specs-tests-full;eest-statetest;nightly` |

| Harness ID | Feature | CLI / behavior |
|------------|---------|----------------|
| H1 | Full-tree CTest | `EthEestStateGranularFull` |
| H2 | Slow-test default filter | evmone + EEST `*run_until_out_of_gas*` excluded; override with `--gtest_filter=*` |
| H3 | Multi-path + `-k` | `EthEestStateGranular dir/a dir/b -k 4844 --fork-profiles eth-cancun` |
| H4 | Per-case fork inference | `postByFork` + manifest profile map; default profiles: shanghai,cancun,prague,osaka |
| H5 | Historical profiles | Berlin, London, Paris (Merge alias) in `ForkProfileRegistry` |
| H6 | Unsupported JSON | Non-GST / engine JSON → `GTEST_SKIP` (not FAIL) |
| H7 | Failure buckets | `tools/scan-eest-failures.py --manifest-16` → JSON/MD reports |

```bash
# List registered cases (file-level GTests; directory mode = one GTest per JSON file)
./build-ref/bcos-evm/test/eth-eest-test/EthEestStateGranular \
  $EEST_ROOT/fixtures/state_tests --gtest_list_tests | head

# Multi-path + name filter (evmone-statetest style)
./build-ref/bcos-evm/test/eth-eest-test/EthEestStateGranular \
  $EEST_ROOT/fixtures/state_tests/cancun/eip4844_blobs \
  $EEST_ROOT/fixtures/state_tests/cancun/eip6780_selfdestruct \
  -k insufficient --fork-profiles eth-cancun

# Nightly full granular sweep
ctest -R EthEestStateGranularFull --test-dir build-ref -C Debug --output-on-failure

# Manifest-16 failure scan + bucket report
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16
```

**Profile / SKIP note:** `--fork-profiles` intersects with JSON `post` fork keys. Pre-fork vectors (e.g. `post: Shanghai` under `cancun/eip4844_blobs`) require `eth-shanghai` in the profile list, or use the default manifest-16 four profiles. Narrow single-profile runs may `GTEST_SKIP("no supported forks")` — this is expected, not an execution failure.

**Probe manifests (nightly):** `eth-eest-probe-*.json`, `eth-eest-precompile-probe*.json`, `probe-gas-cost-one.json` — 7702 behavior/gas slices with `stateRoot` assertions; label `specs-tests-full`, `probe`.

Smoke manifests use curated fixture paths so PR CI stays green; nightly runs the full manifests.

```bash
# Smoke (assets fetched on first cmake configure)
ctest -L 'specs-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Full EEST sweep (local / nightly; may fail until parity gaps close)
ctest -L 'specs-tests-full' --test-dir build-ref -C Debug --output-on-failure
```

### Full-run baseline (2026-07-07, `build-bcos-evm-check`, EEST pin v5.4.0)

Re-run locally:

```bash
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests \
  --manifest bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json \
  --eest-root build-bcos-evm-check/_deps/evm_ref_eest_root \
  --expectations bcos-evm/test/eth-eest-test/manifests/expectations.json
# All 4140 EEST state subtest(s) passed
```

| Manifest | Executed | Pass | Fail | Pass rate | Notes |
|----------|----------|------|------|-----------|-------|
| `eth-eest-tx-full.json` | 106 | 106 | 0 | 100% | 7702 `transaction_tests` |
| `eth-eest-state-full.json` | **4140** | **4140** | **0** | **100%** | 15 manifest entries (Shanghai→Osaka); regression guard |

**Granular manifest-16 scan** (2026-07-07, `--manifest-16`): **0 subtest failures** across 15 directories / 210 JSON files.  
Parity loop closure: `bcos-evm/docs/superpowers/plans/2026-07-06-eest-parity-loop-1-report.md`, loop-2-report.

Historical baseline (2026-07-06): state-full **3075/4140 (74%)** — superseded after parity loop (4844/7623/7702/6780/7825 closed).

Smoke + manifest full remain the PR / nightly gate. Granular full tree (`EthEestStateGranularFull`) is informational for historical dirs and WP-HIST.

PR CI (`capability-gate`) configures with `BCOS_EVM_SPECS_TESTS=ON` (FetchContent downloads assets) and runs smoke via `ctest -L specs-tests-smoke`.

### EIP-1559 GASPRICE probe baseline (2026-06-22, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-1559-gasprice-probe.json` | 1 | 1 | 0 | `stExample/eip1559` Cancun — transitional pass; `stateRoot` not gated (access-list intrinsic gap on non-7623 GST path, spec §1.1) |

Per spec v1.2 §1.1, **zero delta vs pre-1559-fix baseline is acceptable** for EEST stateRoot on this probe: adapter settlement already used `min(tip+base,feeCap)`; the PR's primary delta is TE `buyGas`/`refundGas` and reference `GASPRICE` normalization (ADR-016). Run locally:

```bash
ctest -R 1559-gasprice -V --test-dir build-ref -C Debug --output-on-failure
```

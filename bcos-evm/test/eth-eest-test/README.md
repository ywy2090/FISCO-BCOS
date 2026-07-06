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
| `eth-eest-state-full.json` | Full native EIP dirs in manifest (16 entries: Shanghai→Osaka) | `specs-tests-full` |
| `eth-eest-tx-full.json` | Full Prague 7702 transaction_tests dir | `specs-tests-full` |

**Probe manifests (nightly):** `eth-eest-probe-*.json`, `eth-eest-precompile-probe*.json`, `probe-gas-cost-one.json` — 7702 behavior/gas slices with `stateRoot` assertions; label `specs-tests-full`, `probe`.

Smoke manifests use curated fixture paths so PR CI stays green; nightly runs the full manifests.

```bash
# Smoke (assets fetched on first cmake configure)
ctest -L 'specs-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Full EEST sweep (local / nightly; may fail until parity gaps close)
ctest -L 'specs-tests-full' --test-dir build-ref -C Debug --output-on-failure
```

### Full-run baseline (2026-07-06, `build-bcos-evm-check`, EEST pin v5.4.0)

Re-run locally:

```bash
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests \
  --manifest bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json \
  --eest-root build-bcos-evm-check/_deps/evm_ref_eest_root \
  --expectations bcos-evm/test/eth-eest-test/manifests/expectations.json \
  > /tmp/eest-pass.txt 2> /tmp/eest-fail.txt
# PASS lines → stdout; FAIL lines → stderr
```

| Manifest | Executed | Pass | Fail | Pass rate | Notes |
|----------|----------|------|------|-----------|-------|
| `eth-eest-tx-full.json` | 106 | 106 | 0 | 100% | 7702 `transaction_tests` (not re-run this sweep) |
| `eth-eest-state-full.json` | **4140** | **3075** | **1065** | **74.3%** | 16 manifest entries; see slice table below |

**Core slice** (same dirs as 2026-06-21 README breakdown; subtest counts drift +2 on 7823):

| Directory | Pass | Fail | Total | Pass rate |
|-----------|------|------|-------|-----------|
| `prague/eip7623_increase_calldata_cost` | 424 | 59 | 483 | 87.8% |
| `osaka/eip7823_modexp_upper_bounds` | 23 | 0 | 23 | 100% |
| `prague/eip7702_set_code_tx` | 484 | 68 | 552 | 87.7% |
| **Core total** | **931** | **127** | **1058** | **88.0%** |

**Dominant remaining gaps** (state-full, 2026-07-06): `eip4844_blobs` **890 fail** (202 pass); then `eip7702` (68), `eip7623` (59), `eip6780` (34), `eip7825` (12). Directories at 100% in manifest: 2537, 7823, 7883, 7951, 1153, 5656, 7516, shanghai trio (3651/3855 mostly pass).

Historical baseline (2026-06-21): core slice **519/1056 (49%)** — superseded; 7623/7702/7823 parity improved significantly since W1–W4.

Smoke + self_sponsored `stateRoot` remain the PR gate; full state sweep stays nightly-only until parity closes (4844 is current blocker for aggregate >95%).

PR CI (`capability-gate`) configures with `BCOS_EVM_SPECS_TESTS=ON` (FetchContent downloads assets) and runs smoke via `ctest -L specs-tests-smoke`.

### EIP-1559 GASPRICE probe baseline (2026-06-22, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-1559-gasprice-probe.json` | 1 | 1 | 0 | `stExample/eip1559` Cancun — transitional pass; `stateRoot` not gated (access-list intrinsic gap on non-7623 GST path, spec §1.1) |

Per spec v1.2 §1.1, **zero delta vs pre-1559-fix baseline is acceptable** for EEST stateRoot on this probe: adapter settlement already used `min(tip+base,feeCap)`; the PR's primary delta is TE `buyGas`/`refundGas` and reference `GASPRICE` normalization (ADR-016). Run locally:

```bash
ctest -R 1559-gasprice -V --test-dir build-ref -C Debug --output-on-failure
```

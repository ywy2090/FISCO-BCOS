# specs-tests

EVM reference vector runners for geth / op-geth parity evidence.

## Build

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
| `eth-eest-1559-gasprice-probe.json` | EIP-1559 `GASPRICE`/`BASEFEE` state probe (`stExample/eip1559`) | `specs-tests-smoke`, `1559-gasprice` |
| `eth-eest-state-full.json` | Full Prague 7623 + Osaka 7823 + Prague 7702 state dirs | `specs-tests-full` |
| `eth-eest-tx-full.json` | Full Prague 7702 transaction_tests dir | `specs-tests-full` |

Smoke manifests use curated fixture paths so PR CI stays green; nightly runs the full manifests.

```bash
# Smoke (assets fetched on first cmake configure)
ctest -L 'specs-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Full EEST sweep (local / nightly; may fail until parity gaps close)
ctest -L 'specs-tests-full' --test-dir build-ref -C Debug --output-on-failure
```

### Full-run baseline (2026-06-21, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-tx-full.json` | 106 | 106 | 0 | 7702 `transaction_tests` — strict auth scalar encoding fix |
| `eth-eest-state-full.json` | 1056 | 519 | 537 | 7623 230/483; 7823 0/21; 7702 289/552 (+12 precheck) |

State full dominant gaps: `stateRoot` mismatch (~458), included success paths with wrong status (~67). Precheck gaps (`SENDER_NOT_EOA`, empty auth list, type-4 CREATE, fee cap) closed in W1–W4. Smoke + self_sponsored `stateRoot` remain the PR gate; full state sweep stays nightly-only until parity closes.

PR CI (`capability-gate`) configures with `BCOS_EVM_SPECS_TESTS=ON` (FetchContent downloads assets) and runs smoke via `ctest -L specs-tests-smoke`.

### EIP-1559 GASPRICE probe baseline (2026-06-22, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-1559-gasprice-probe.json` | 1 | 1 | 0 | `stExample/eip1559` Cancun — transitional pass; `stateRoot` not gated (access-list intrinsic gap on non-7623 GST path, spec §1.1) |

Per spec v1.2 §1.1, **zero delta vs pre-1559-fix baseline is acceptable** for EEST stateRoot on this probe: adapter settlement already used `min(tip+base,feeCap)`; the PR's primary delta is TE `buyGas`/`refundGas` and reference `GASPRICE` normalization (ADR-016). Run locally:

```bash
ctest -R 1559-gasprice -V --test-dir build-ref -C Debug --output-on-failure
```

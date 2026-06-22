# evm-reference-tests

EVM reference vector runners for geth / op-geth parity evidence.

## Build

Reference tests are opt-in and do not affect default builds:

```bash
cmake -S . -B build-ref -DTESTS=ON -DBCOS_EVM_REFERENCE_TESTS=ON
cmake --build build-ref --target bcos-evm-reference-tests-core
```

## Assets

Official General State Tests are vendored as a git submodule. Initialize before running loaders or runners:

```bash
git submodule update --init --depth 1 bcos-evm/evm-reference-tests/assets/ethereum-tests
tar -xzf bcos-evm/evm-reference-tests/assets/ethereum-tests/fixtures_general_state_tests.tgz \
  -C bcos-evm/evm-reference-tests/assets/ethereum-tests GeneralStateTests
export ETHEREUM_TESTS_ROOT=/path/to/GeneralStateTests/parent  # optional override
```

Loaders call `ensureGeneralStateTestsExtracted()` when `GeneralStateTests/` is missing.

When unset, runners resolve the default tree at `assets/ethereum-tests` (see implementation plan). Pin versions and upstream metadata in `assets/upstream-pins.json`.

## Fork extension

Add fork profiles in `ForkProfileRegistry` (see implementation plan Part A Task 5).

Osaka smoke (`eth-gst-osaka-smoke.json`) executes at `EVMC_OSAKA` while using `postFork: Prague` expectations until `ethereum/tests` submodule pins include Osaka post entries. EEST fixtures (`assets/eest`) provide Osaka-native vectors for nightly runs (Task 15).

## EEST assets (Task 15+)

```bash
bash bcos-evm/evm-reference-tests/tools/fetch_eest_assets.sh
export EEST_ROOT=/path/to/assets/eest  # optional override
```

Pin release URL and sha256 in `assets/upstream-pins.json`. Nightly CI uses `.github/workflows/evm-reference-tests-nightly.yml`.

### Manifests

| Manifest | Scope | CTest label |
|----------|-------|-------------|
| `eth-eest-state-smoke.json` | Curated EEST state vectors (7623/7823/7702 warming + full self-sponsored 10 variants + invalid auth) | `evm-reference-tests-smoke` |
| `eth-eest-tx-smoke.json` | Curated 7702 tx RLP validation | `evm-reference-tests-smoke` |
| `eth-eest-1559-gasprice-probe.json` | EIP-1559 `GASPRICE`/`BASEFEE` state probe (`stExample/eip1559`) | `evm-reference-tests-smoke`, `1559-gasprice` |
| `eth-eest-state-full.json` | Full Prague 7623 + Osaka 7823 + Prague 7702 state dirs | `evm-reference-tests-full` |
| `eth-eest-tx-full.json` | Full Prague 7702 transaction_tests dir | `evm-reference-tests-full` |

Smoke manifests use curated fixture paths so PR CI stays green; nightly runs the full manifests.

```bash
# Smoke (requires fetch_eest_assets.sh once)
ctest -L 'evm-reference-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Full EEST sweep (local / nightly; may fail until parity gaps close)
ctest -L 'evm-reference-tests-full' --test-dir build-ref -C Debug --output-on-failure
```

### Full-run baseline (2026-06-21, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-tx-full.json` | 106 | 106 | 0 | 7702 `transaction_tests` — strict auth scalar encoding fix |
| `eth-eest-state-full.json` | 1056 | 519 | 537 | 7623 230/483; 7823 0/21; 7702 289/552 (+12 precheck) |

State full dominant gaps: `stateRoot` mismatch (~458), included success paths with wrong status (~67). Precheck gaps (`SENDER_NOT_EOA`, empty auth list, type-4 CREATE, fee cap) closed in W1–W4. Smoke + self_sponsored `stateRoot` remain the PR gate; full state sweep stays nightly-only until parity closes.

PR CI (`capability-gate`) fetches EEST and runs smoke via `ctest -L evm-reference-tests-smoke`.

### EIP-1559 GASPRICE probe baseline (2026-06-22, `build-ref`, EEST pin)

| Manifest | Executed | Pass | Fail | Notes |
|----------|----------|------|------|-------|
| `eth-eest-1559-gasprice-probe.json` | 1 | 1 | 0 | `stExample/eip1559` Cancun — transitional pass; `stateRoot` not gated (access-list intrinsic gap on non-7623 GST path, spec §1.1) |

Per spec v1.2 §1.1, **zero delta vs pre-1559-fix baseline is acceptable** for EEST stateRoot on this probe: adapter settlement already used `min(tip+base,feeCap)`; the PR's primary delta is TE `buyGas`/`refundGas` and reference `GASPRICE` normalization (ADR-016). Run locally:

```bash
ctest -R 1559-gasprice -V --test-dir build-ref -C Debug --output-on-failure
```

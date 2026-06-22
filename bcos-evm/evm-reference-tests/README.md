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

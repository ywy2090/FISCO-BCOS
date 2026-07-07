# Task 6 Report: H7 — Failure bucket reports (JSON + MD)

## Status

**COMPLETE**

## Deliverables

| File | Action |
|------|--------|
| `bcos-evm/test/eth-eest-test/tools/bucket-failures.py` | Created — ordered taxonomy, `classify_failure`, `write_bucket_reports` |
| `bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py` | Modified — argparse, manifest loader, `--granular-full`, bucket integration |
| `bcos-evm/test/eth-eest-test/reports/README.md` | Created — artifact docs + bucket taxonomy |
| `bcos-evm/test/eth-eest-test/CMakeLists.txt` | Modified — `eest-granular-failure-report` custom target |

## Commit

```
test(eest): H7 failure bucket reports for granular statetest runs

Add JSON/MD taxonomy so nightly full-tree failures group by assertion
kind (stateRoot, logs, gas, exception) for parity loop prioritization.
```

## Tests

| Command | Result |
|---------|--------|
| `scan-eest-failures.py --dir cancun/eip4844_blobs --profile eth-cancun` | **0 failures** (22/22 files clean) |
| `scan-eest-failures.py --manifest-16` | **0 failures** (15 manifest dirs, 210 JSON files clean) |
| `ctest -R EthExecutionSpecStateTestsFull` | **PASS** (manifest regression unchanged) |

### Verification commands

```bash
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py \
  --dir cancun/eip4844_blobs --profile eth-cancun

python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16

ctest -R EthExecutionSpecStateTestsFull --test-dir build-bcos-evm-check
```

## Implementation Summary

- **`bucket-failures.py`**: first-match bucket taxonomy (`state_root` → `unknown`); emits timestamped `eest-granular-failures-<ts>.{json,md}` with `by_bucket`, per-bucket inventory, and `bucket` field on each failure.
- **`scan-eest-failures.py`**: loads dirs from `eth-eest-state-full.json`; CLI `--build-dir`, `--dir`, `--profile`, `--manifest-16` (default), `--granular-full`; writes inventory (`eest-state-full-failures` / `eest-dir-*` / `eest-granular-full-failures`) plus bucket reports.
- **CMake**: maintainer-only `eest-granular-failure-report` target (not PR gate).

## Concerns

1. **Manifest count** — `eth-eest-state-full.json` has **15** entries; docs/README still say "16"; scope string preserved for parity with brief.
2. **Generated reports** — timestamped bucket JSON/MD under `reports/` are local artifacts; not committed (see `reports/README.md`).
3. **`--granular-full`** — file-level GTest granularity (directory input); not exercised in this verification run (nightly-only; 4h timeout).

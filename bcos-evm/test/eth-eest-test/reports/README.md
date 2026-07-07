# EEST granular failure reports

Generated artifacts from `tools/scan-eest-failures.py` and `tools/bucket-failures.py`.
Not checked into CI by default; safe to delete and regenerate.

## Generate

From repository root (requires `build-bcos-evm-check` with `EthEestStateGranular`):

```bash
# All 16 manifest dirs (eth-eest-state-full.json)
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16

# Single EIP directory
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py \
  --dir cancun/eip4844_blobs --profile eth-cancun

# Nightly full-tree sweep (file-level granularity)
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --granular-full

# CMake maintainer target (same as --manifest-16)
cmake --build build-bcos-evm-check --target eest-granular-failure-report
```

## Outputs

| File pattern | Producer | Purpose |
|--------------|----------|---------|
| `eest-state-full-failures.{json,md}` | `--manifest-16` | Per-directory inventory for manifest-full dirs |
| `eest-dir-<slug>-failures.{json,md}` | `--dir` | Single-directory inventory |
| `eest-granular-full-failures.{json,md}` | `--granular-full` | Full-tree inventory |
| `eest-granular-failures-<timestamp>.{json,md}` | bucket pass | Failures grouped by assertion kind |
| `gtest-xml/` | scan runs | Per-file GTest XML (debug / diff) |

## Bucket taxonomy

Ordered first-match classification on failure message text:

| Bucket | Patterns |
|--------|----------|
| `state_root` | `stateRoot mismatch`, `state root` |
| `logs_hash` | `logsHash`, `logs hash` |
| `gas_used` | `gasUsed`, `gas used`, `intrinsic gas` |
| `expect_exception` | `expectException`, `expected reverted` |
| `balance_nonce` | `balance`, `nonce`, `coinbase` |
| `code_storage` | `code mismatch`, `storage` |
| `runner_error` | `no gtest xml`, `timeout`, `SEGV` |
| `unknown` | everything else |

Re-bucket an existing inventory JSON:

```bash
python3 bcos-evm/test/eth-eest-test/tools/bucket-failures.py \
  bcos-evm/test/eth-eest-test/reports/eest-state-full-failures.json
```

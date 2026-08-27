#!/usr/bin/env bash
# FISCO BCOS — Karst atomic release gate: legacy fork API identifiers must not remain
# in production OP surfaces after Task 7.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v rg >/dev/null 2>&1; then
  echo "check-op-karst-release-gate: rg (ripgrep) is required" >&2
  exit 1
fi

FORBIDDEN='isJovianActive|OpForkFlags|configAt\(OpForkFlags\)'
SCAN_ROOTS=(
  engine
  libinitializer
  opstack-executor
  bcos-rpc
  bcos-evm/bcos-evm
  bcos-evm/bcos-evm/opstack
  bcos-framework/bcos-framework
  bcos-ledger/bcos-ledger
)

fail=0
for dir in "${SCAN_ROOTS[@]}"; do
  if [[ ! -d "$dir" ]]; then
    echo "check-op-karst-release-gate: missing scan root: $dir" >&2
    fail=1
    continue
  fi
  # Capture status explicitly: `if rg` would treat missing/error exits as "no match".
  set +e
  rg -n "$FORBIDDEN" "$dir" --glob '*.h' --glob '*.hpp' --glob '*.cpp' --glob '*.cc'
  status=$?
  set -e
  case "$status" in
    0)
      echo "check-op-karst-release-gate: forbidden identifier in $dir" >&2
      fail=1
      ;;
    1)
      ;; # no match
    *)
      echo "check-op-karst-release-gate: rg failed in $dir (exit $status)" >&2
      exit 2
      ;;
  esac
done

if [[ "$fail" -ne 0 ]]; then
  echo "check-op-karst-release-gate: FAILED" >&2
  exit 1
fi

echo "check-op-karst-release-gate: OK"

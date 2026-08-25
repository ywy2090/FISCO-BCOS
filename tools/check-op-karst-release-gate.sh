#!/usr/bin/env bash
# FISCO BCOS — Karst atomic release gate: legacy fork API identifiers must not remain
# in production OP surfaces after Task 7.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

FORBIDDEN='isJovianActive|OpForkFlags|configAt\(OpForkFlags\)'
SCAN_ROOTS=(
  engine
  libinitializer
  opstack-executor
  bcos-rpc
  bcos-evm/bcos-evm
  bcos-evm/bcos-evm/opstack
)

fail=0
for dir in "${SCAN_ROOTS[@]}"; do
  if [[ ! -d "$dir" ]]; then
    continue
  fi
  if rg -n "$FORBIDDEN" "$dir" --glob '*.h' --glob '*.cpp' 2>/dev/null; then
    echo "check-op-karst-release-gate: forbidden identifier in $dir" >&2
    fail=1
  fi
done

if [[ "$fail" -ne 0 ]]; then
  echo "check-op-karst-release-gate: FAILED" >&2
  exit 1
fi

echo "check-op-karst-release-gate: OK"

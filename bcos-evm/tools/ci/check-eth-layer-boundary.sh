#!/usr/bin/env bash
# ADR-005 Rule 1: eth/ must not include bcos/ or opstack/ chain shells.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

status=0

if matches=$(grep -rE '#include[[:space:]]*"bcos-evm/(bcos|opstack)/' eth/ \
    --include='*.cpp' --include='*.h' --include='*.hpp' 2>/dev/null || true); then
  if [[ -n "$matches" ]]; then
    echo "ERROR: eth/ must not include bcos/ or opstack/ headers:" >&2
    echo "$matches" >&2
    status=1
  fi
fi

if [[ $status -eq 0 ]]; then
  echo "eth-layer-boundary gate: OK"
fi
exit $status

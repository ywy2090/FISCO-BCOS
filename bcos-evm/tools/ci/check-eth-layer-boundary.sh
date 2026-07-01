#!/usr/bin/env bash
# ADR-005 / ADR-019 layer include gates for bcos-evm/eth.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

status=0

check_pattern() {
  local label="$1"
  local path="$2"
  local pattern="$3"
  shift 3
  local includes=("$@")
  if [[ ${#includes[@]} -eq 0 ]]; then
    includes=(--include='*.cpp' --include='*.h' --include='*.hpp')
  fi

  local matches=""
  # shellcheck disable=SC2086
  if matches=$(grep -rE "$pattern" $path "${includes[@]}" 2>/dev/null || true); then
    if [[ -n "$matches" ]]; then
      echo "ERROR: $label" >&2
      echo "$matches" >&2
      status=1
    fi
  fi
}

# Gap 38 — eth/ must not pull in chain shells.
check_pattern \
  'eth/ must not include bcos/ or opstack/ headers' \
  'eth/' \
  '#include[[:space:]]*"bcos-evm/(bcos|opstack)/'

# apply orchestration must not reach into kernel execution internals.
check_pattern \
  'eth/apply/ must not include eth/kernel/execution/ headers' \
  'eth/apply/' \
  '#include[[:space:]]*"bcos-evm/eth/kernel/execution/'

# kernel stays chain-agnostic at compile time.
check_pattern \
  'eth/kernel/ must not include eth/apply/ headers' \
  'eth/kernel/' \
  '#include[[:space:]]*"bcos-evm/eth/apply/'

# core seam headers must not depend on kernel execution implementation headers.
check_pattern \
  'eth/core/ headers must not include eth/kernel/execution/ (use eth/core/*.h seam types)' \
  'eth/core/' \
  '#include[[:space:]]*"bcos-evm/eth/kernel/execution/' \
  --include='*.h' --include='*.hpp'

# state-transition context headers stay above kernel/execution implementation.
check_pattern \
  'eth/kernel/state-transition/ headers must not include eth/kernel/execution/' \
  'eth/kernel/state-transition/' \
  '#include[[:space:]]*"bcos-evm/eth/kernel/execution/' \
  --include='*.h' --include='*.hpp'

if [[ $status -eq 0 ]]; then
  echo "eth-layer-boundary gate: OK"
fi
exit $status

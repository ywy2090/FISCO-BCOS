#!/usr/bin/env bash
# capability-matrix gate: fail if capability surfaces change without matrix update.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MATRIX="$ROOT/capability-matrix.md"

if [[ ! -f "$MATRIX" ]]; then
  echo "error: missing $MATRIX" >&2
  exit 1
fi

# Matrix body: capability rows only (exclude column semantics and status token glossary).
body=$(awk '/^\| Capability \| Layer \|/,/^---$/' "$MATRIX" | tail -n +3 | sed '$d' || true)

# Forbidden legacy tokens in cells (ADR-002). Match whole tokens only.
invalid=$(echo "$body" | rg -n '\b(supported|wired|partial|TBD)\b' || true)
if [[ -n "$invalid" ]]; then
  echo "capability-matrix: forbidden status token:" >&2
  echo "$invalid" >&2
  exit 1
fi

while IFS= read -r line; do
  [[ -z "$line" ]] && continue
  if ! echo "$line" | rg -q 'inherited|explicit|feature-gated|unsupported|deviation'; then
    echo "capability-matrix: row missing status token: $line" >&2
    exit 1
  fi
done <<< "$body"

echo "capability-matrix: OK"

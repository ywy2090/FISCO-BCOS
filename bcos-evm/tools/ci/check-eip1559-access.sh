#!/usr/bin/env bash
# Enforce single consumer gate for cfg.eip1559 (Eip1559Gate.h).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

ALLOWLIST=(
    'eth/core/RevisionConfig.h'
    'eth/RevisionConfig.h'
    'eth/eip/Eip1559Gate.h'
)

status=0
matches="$(grep -rnE '\.eip1559\b' \
    --include='*.h' --include='*.hpp' --include='*.cpp' \
    eth/ bcos/ opstack/ || true)"

if [[ -n "$matches" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        file="${line%%:*}"
        allowed=0
        for entry in "${ALLOWLIST[@]}"; do
            if [[ "$file" == "$entry" ]]; then
                allowed=1
                break
            fi
        done
        if [[ $allowed -eq 0 ]]; then
            if [[ -z "${VIOLATIONS:-}" ]]; then
                VIOLATIONS="$line"
            else
                VIOLATIONS+=$'\n'"$line"
            fi
            status=1
        fi
    done <<< "$matches"
fi

if [[ $status -ne 0 ]]; then
    echo "ERROR: cfg.eip1559 read outside Eip1559Gate.h / RevisionConfig.h:" >&2
    echo "$VIOLATIONS" >&2
    echo "  -> Route through gas::isEip1559* helpers in eth/eip/Eip1559Gate.h." >&2
else
    echo "eip1559-access gate: OK (no direct cfg.eip1559 reads outside allowlist)"
fi

exit $status

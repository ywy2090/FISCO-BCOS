#!/usr/bin/env bash
# Isthmus criteria 14 (op-geth state_processor.go:141): no EIP-6110/7002/7251 block
# postExecution hooks in the bcos-evm OpStack tx execution path.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

SCAN_DIRS=(
    opstack/
    eth/pipeline/
    eth/reference/
)

FORBIDDEN=(
    'ParseDepositLogs'
    'ProcessWithdrawalQueue'
    'ProcessConsolidationQueue'
    'prague_post_execution'
    'praguePostExecution'
)

status=0
VIOLATIONS=""

for pattern in "${FORBIDDEN[@]}"; do
    matches="$(grep -rn "$pattern" \
        --include='*.h' --include='*.hpp' --include='*.cpp' \
        "${SCAN_DIRS[@]}" || true)"
    if [[ -n "$matches" ]]; then
        if [[ -z "$VIOLATIONS" ]]; then
            VIOLATIONS="$matches"
        else
            VIOLATIONS+=$'\n'"$matches"
        fi
        status=1
    fi
done

if [[ $status -ne 0 ]]; then
    echo "ERROR: Prague block postExecution hooks must not appear in OpStack tx path:" >&2
    echo "$VIOLATIONS" >&2
    echo "  -> Isthmus aligns with op-geth: postExecution runs only when IsPrague && !IsIsthmus." >&2
    echo "  -> bcos-evm OpStack is tx-level; criteria 14 is enforced by absence + IsthmusPostExecutionPolicyTest." >&2
else
    echo "opstack-no-prague-post-execution gate: OK"
fi

exit $status

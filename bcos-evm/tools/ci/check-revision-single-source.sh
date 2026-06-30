#!/usr/bin/env bash
# Enforce single source of truth for A-class EIP revision gating.
# Fails if any A-class field is assigned directly from `revision >= EVMC_xxx`
# outside the canonical kernel header (RevisionConfig.h).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# A-class fields (keep in sync with REVISION_CONFIG_GATED_FIELDS in eth/RevisionConfig.h).
FIELDS=(eip2929 eip2537 eip7212 eip7623 eip7823 eip7702)

status=0
for field in "${FIELDS[@]}"; do
    # Match e.g. `cfg.eip2537 = ... revision >= EVMC_PRAGUE` on one line.
    matches="$(grep -rnE "\.${field}[[:space:]]*=.*revision[[:space:]]*>=[[:space:]]*EVMC_" \
        --include='*.h' --include='*.hpp' --include='*.cpp' \
        --exclude='RevisionConfig.h' . || true)"
    if [[ -n "$matches" ]]; then
        echo "ERROR: A-class field '${field}' derived from 'revision >= EVMC_' outside RevisionConfig.h:" >&2
        echo "$matches" >&2
        echo "  -> Use revisionConfigFromRevision() (+ applyFiscoFeatureGates for FISCO) instead." >&2
        status=1
    fi
done

if [[ $status -eq 0 ]]; then
    echo "revision-single-source gate: OK (no A-class field derived outside RevisionConfig.h)"
fi
exit $status

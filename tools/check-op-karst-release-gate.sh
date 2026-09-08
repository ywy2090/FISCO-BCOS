#!/usr/bin/env bash
# FISCO BCOS — Karst atomic release gate: production OP surfaces must not keep
# OpForkFlags / isJovianActive, and karstConfig must be Osaka + EIP-7825.
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
  bcos-evm
  bcos-framework
  bcos-ledger
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
  rg -n "$FORBIDDEN" "$dir" \
    --glob '*.h' --glob '*.hpp' --glob '*.cpp' --glob '*.cc' --glob '*.inl' \
    --glob '!**/test/**' --glob '!**/tests/**' --glob '!**/unittests/**'
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

# Production karstConfig must ship Osaka + deposit exemption (not a Jovian alias).
KARST_CFG='bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp'
if [[ ! -f "$KARST_CFG" ]]; then
  echo "check-op-karst-release-gate: missing $KARST_CFG" >&2
  fail=1
else
  set +e
  karst_body="$(rg -n -A 20 'const OpForkConfig& karstConfig\(\) noexcept' "$KARST_CFG")"
  karst_status=$?
  set -e
  if [[ "$karst_status" -ne 0 ]]; then
    echo "check-op-karst-release-gate: karstConfig() not found in $KARST_CFG" >&2
    fail=1
  else
    if ! printf '%s\n' "$karst_body" | rg -q 'EVMC_OSAKA'; then
      echo "check-op-karst-release-gate: karstConfig() must set EVMC_OSAKA" >&2
      fail=1
    fi
    if ! printf '%s\n' "$karst_body" | rg -q 'deposit_exempt_from_max_tx_gas = true'; then
      echo "check-op-karst-release-gate: karstConfig() must set deposit_exempt_from_max_tx_gas = true" >&2
      fail=1
    fi
  fi
fi

# Production OP getPayload must not serve the Eth V1-V5 window via EngineTracker.
OP_GETPAYLOAD_FILES=(
  engine/bcos-engine/OpEngineService.inl
  engine/bcos-engine/OpEngineService.h
  engine/bcos-engine/OpEngineService.cpp
)
for f in "${OP_GETPAYLOAD_FILES[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "check-op-karst-release-gate: missing $f" >&2
    fail=1
    continue
  fi
  set +e
  rg -n 'm_tracker\.getPayload' "$f"
  tracker_status=$?
  rg -n 'isGetPayloadVersionSupported' "$f"
  window_status=$?
  set -e
  case "$tracker_status" in
    0)
      echo "check-op-karst-release-gate: OP getPayload must not call m_tracker.getPayload ($f)" >&2
      fail=1
      ;;
    1) ;; # no match
    *)
      echo "check-op-karst-release-gate: rg failed in $f (exit $tracker_status)" >&2
      exit 2
      ;;
  esac
  case "$window_status" in
    0)
      echo "check-op-karst-release-gate: OP getPayload must not use isGetPayloadVersionSupported ($f)" >&2
      fail=1
      ;;
    1) ;; # no match
    *)
      echo "check-op-karst-release-gate: rg failed in $f (exit $window_status)" >&2
      exit 2
      ;;
  esac
done

if [[ "$fail" -ne 0 ]]; then
  echo "check-op-karst-release-gate: FAILED" >&2
  exit 1
fi

echo "check-op-karst-release-gate: OK"

#!/usr/bin/env bash
# Enforce PascalCase basenames under bcos-evm scoped dirs.
# .hpp extension allowed only in eth/state/ and include/bcos-evm/ (Phase 3/4 legacy).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

HPP_ALLOWLIST=(eth/state include/bcos-evm)
status=0

is_hpp_allowed() {
  local relpath="$1"
  for prefix in "${HPP_ALLOWLIST[@]}"; do
    [[ "$relpath" == "$prefix/"* ]] && return 0
  done
  return 1
}

while IFS= read -r -d '' file; do
  relpath="${file#./}"
  base="$(basename "$file")"
  stem="${base%.*}"
  ext="${base##*.}"

  if [[ ! "$stem" =~ ^[A-Z][A-Za-z0-9]*$ ]]; then
    echo "ERROR: non-PascalCase basename: $relpath" >&2
    status=1
    continue
  fi

  case "$ext" in
    cpp) ;;
    h) ;;
    hpp)
      if ! is_hpp_allowed "$relpath"; then
        echo "ERROR: .hpp not allowed outside eth/state/ or include/bcos-evm/: $relpath" >&2
        status=1
      fi
      ;;
    *)
      continue
      ;;
  esac
done < <(find eth bcos opstack test specs-tests \
  \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print0 2>/dev/null)

if [[ $status -eq 0 ]]; then
  echo "filename-convention gate: OK"
fi
exit $status

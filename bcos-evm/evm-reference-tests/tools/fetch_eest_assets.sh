#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PINS="${ROOT}/assets/upstream-pins.json"
DEST="${ROOT}/assets/eest"
URL="$(python3 -c "import json; print(json.load(open('${PINS}'))['eest']['url'])")"
EXPECTED="$(python3 -c "import json; print(json.load(open('${PINS}'))['eest']['sha256'])")"

mkdir -p "${DEST}"
TARBALL="${DEST}/fixtures_develop.tar.gz"

if [[ ! -f "${TARBALL}" ]]; then
  echo "Downloading EEST fixtures from ${URL}"
  curl -fsSL "${URL}" -o "${TARBALL}"
fi

ACTUAL="$(shasum -a 256 "${TARBALL}" | awk '{print $1}')"
if [[ "${EXPECTED}" != "<fill-in-part-b-task-15>" && "${ACTUAL}" != "${EXPECTED}" ]]; then
  echo "error: EEST tarball sha256 mismatch" >&2
  echo "expected ${EXPECTED}" >&2
  echo "actual   ${ACTUAL}" >&2
  exit 1
fi

if [[ ! -d "${DEST}/fixtures/state_tests" ]]; then
  echo "Extracting EEST fixtures"
  tar -xzf "${TARBALL}" -C "${DEST}"
fi

echo "EEST assets ready at ${DEST}"

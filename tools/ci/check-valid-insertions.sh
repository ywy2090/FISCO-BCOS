#!/bin/bash
# Local pre-commit helper mirroring tools/.ci/check-commit.sh check_PR_limit math.
set -euo pipefail
INSERT_LIMIT=300
LICENSE_LINE=20
BASE="${1:-HEAD^}"
HEAD="${2:-HEAD}"
need_check_files=$(git diff --numstat "$BASE" "$HEAD" \
  | awk '{if ($1!=0) print $0;}' \
  | sed 's/{.*> //g;s/}//g' \
  | awk '{print $3}' \
  | grep -vE 'sample/|benchmark/|test|tools/|fisco-bcos/|\.github/' \
  | grep -E '\.(h|hpp|c|cpp)$' || true)
if [ -z "$need_check_files" ]; then
  echo "OK: no production .h/.cpp insertions (valid=0)"
  exit 0
fi
new_files=$(git diff "$BASE" "$HEAD" $need_check_files | grep -c 'new file mode' || true)
empty_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*$' || true)
block_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*[\{\}]\s*$' || true)
include_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\#include' || true)
comment_lines=$(git diff "$BASE" "$HEAD" $need_check_files | grep -cE '^\+\s*//' || true)
insertions=$(git diff --ignore-space-change --shortstat "$BASE" "$HEAD" $need_check_files | awk '{print $4}')
insertions=${insertions:-0}
git_ins=$(git diff --shortstat "$BASE" "$HEAD" $need_check_files | awk '{print $4}')
git_ins=${git_ins:-0}
valid=$((insertions - new_files * LICENSE_LINE - comment_lines - empty_lines - block_lines - include_lines))
echo "valid_insertions=$valid (limit=$INSERT_LIMIT) raw=$insertions git=$git_ins new_files=$new_files"
if [ "$valid" -gt "$INSERT_LIMIT" ] && [ "$git_ins" -gt "$INSERT_LIMIT" ]; then
  echo "FAIL: valid_insertions $valid > $INSERT_LIMIT"
  exit 1
fi
echo "OK"

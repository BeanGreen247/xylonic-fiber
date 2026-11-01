#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 reference.png actual.png [diff.png]" >&2
  exit 2
fi

ref=$1
actual=$2
diff=${3:-}

read -r rw rh <<< "$(identify -format '%w %h' "$ref")"
read -r aw ah <<< "$(identify -format '%w %h' "$actual")"
if [[ "$rw $rh" != "$aw $ah" ]]; then
  echo "FAIL: dimensions differ: reference ${rw}x${rh}, actual ${aw}x${ah}" >&2
  exit 1
fi

metric=$(compare -metric AE "$ref" "$actual" null: 2>&1 || true)
rmse=$(compare -metric RMSE "$ref" "$actual" null: 2>&1 || true)
echo "pixels different: $metric / $((rw * rh))"
echo "RMSE: $rmse"
if [[ -n "$diff" ]]; then
  compare "$ref" "$actual" "$diff"
  echo "diff: $diff"
fi
[[ "$metric" == 0* ]] || exit 1

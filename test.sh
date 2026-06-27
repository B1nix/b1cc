#!/bin/sh
set -eu

tmp="${TMPDIR:-/tmp}/b1cc-test"
rm -rf "$tmp"
mkdir -p "$tmp"

./b1cc.py tests/return_42.c -o "$tmp/return_42"
set +e
"$tmp/return_42"
rc=$?
set -e

test "$rc" = 42
echo "ok return_42"

./b1cc.py tests/precedence.c -o "$tmp/precedence"
set +e
"$tmp/precedence"
rc=$?
set -e

test "$rc" = 14
echo "ok precedence"

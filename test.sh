#!/bin/sh
set -eu

tmp="${TMPDIR:-/tmp}/b1cc-test"
rm -rf "$tmp"
mkdir -p "$tmp"

make build/b1cc >/dev/null

./build/b1cc tests/return_42.c -o "$tmp/return_42"
set +e
"$tmp/return_42"
rc=$?
set -e

test "$rc" = 42
echo "ok return_42"

./build/b1cc tests/precedence.c -o "$tmp/precedence"
set +e
"$tmp/precedence"
rc=$?
set -e

test "$rc" = 14
echo "ok precedence"

./build/b1cc tests/local.c -o "$tmp/local"
set +e
"$tmp/local"
rc=$?
set -e

test "$rc" = 18
echo "ok local"

./build/b1cc tests/if_else.c -o "$tmp/if_else"
set +e
"$tmp/if_else"
rc=$?
set -e

test "$rc" = 7
echo "ok if_else"

./build/b1cc tests/while.c -o "$tmp/while"
set +e
"$tmp/while"
rc=$?
set -e

test "$rc" = 10
echo "ok while"

./build/b1cc tests/for.c -o "$tmp/for"
set +e
"$tmp/for"
rc=$?
set -e

test "$rc" = 15
echo "ok for"

./build/b1cc tests/function.c -o "$tmp/function"
set +e
"$tmp/function"
rc=$?
set -e

test "$rc" = 42
echo "ok function"

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

./build/b1cc tests/puts.c -o "$tmp/puts"
set +e
"$tmp/puts" > "$tmp/puts.out"
set -e
grep -qx "hello from b1cc" "$tmp/puts.out"
echo "ok puts"

./build/b1cc tests/string_pointer.c -o "$tmp/string_pointer"
set +e
"$tmp/string_pointer" > "$tmp/string_pointer.out"
set -e
grep -qx "pointer string" "$tmp/string_pointer.out"
echo "ok string_pointer"

./build/b1cc tests/function.c -o "$tmp/function"
set +e
"$tmp/function"
rc=$?
set -e

test "$rc" = 42
echo "ok function"

./build/b1cc tests/argc.c -o "$tmp/argc"
set +e
"$tmp/argc" one two
rc=$?
set -e

test "$rc" = 3
echo "ok argc"

./build/b1cc tests/argv.c -o "$tmp/argv"
set +e
"$tmp/argv" alpha > "$tmp/argv.out"
set -e
grep -qx "alpha" "$tmp/argv.out"
echo "ok argv"

rm -f /tmp/b1cc-file-write.out
./build/b1cc tests/file_write.c -o "$tmp/file_write"
"$tmp/file_write"
grep -qx "file smoke" /tmp/b1cc-file-write.out
echo "ok file_write"

./build/b1cc tests/stderr_exit.c -o "$tmp/stderr_exit"
set +e
"$tmp/stderr_exit" 2> "$tmp/stderr_exit.err"
rc=$?
set -e

test "$rc" = 37
grep -qx "stderr smoke" "$tmp/stderr_exit.err"
echo "ok stderr_exit"

./build/b1cc tests/include_initializer.c -o "$tmp/include_initializer"
set +e
"$tmp/include_initializer"
rc=$?
set -e

test "$rc" = 11
echo "ok include_initializer"

./build/b1cc --target=x86_64-b1nix tests/return_42.c -S -o "$tmp/return_42_x86_64.s"
grep -q '^main:' "$tmp/return_42_x86_64.s"
grep -q 'ret' "$tmp/return_42_x86_64.s"
echo "ok x86_64_b1nix_asm"

if [ -x ../b1nix/tools/toolchain/bin/b1nix-cc ]; then
  ./build/b1cc --target=x86_64-b1nix tests/return_42.c -o "$tmp/return_42.b1nix"
  test -s "$tmp/return_42.b1nix"
  echo "ok x86_64_b1nix_elf"
fi

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

./build/b1cc tests/m7_enum_typedef.c -o "$tmp/m7_enum_typedef"
set +e
"$tmp/m7_enum_typedef"
rc=$?
set -e
test "$rc" = 3
echo "ok m7_enum_typedef"

./build/b1cc tests/m7_cast.c -o "$tmp/m7_cast"
set +e
"$tmp/m7_cast"
rc=$?
set -e
test "$rc" = 42
echo "ok m7_cast"

./build/b1cc tests/m7_struct.c -o "$tmp/m7_struct"
set +e
"$tmp/m7_struct"
rc=$?
set -e
test "$rc" = 30
echo "ok m7_struct"

./build/b1cc tests/m7_array.c -o "$tmp/m7_array"
set +e
"$tmp/m7_array"
rc=$?
set -e
test "$rc" = 80
echo "ok m7_array"

./build/b1cc tests/m7_macro.c -o "$tmp/m7_macro"
set +e
"$tmp/m7_macro"
rc=$?
set -e
test "$rc" = 42
echo "ok m7_macro"

./build/b1cc tests/m10_logical.c -o "$tmp/m10_logical"
set +e
"$tmp/m10_logical"
rc=$?
set -e
test "$rc" = 42
echo "ok m10_logical"

./build/b1cc tests/m10_bitwise.c -o "$tmp/m10_bitwise"
set +e
"$tmp/m10_bitwise"
rc=$?
set -e
test "$rc" = 42
echo "ok m10_bitwise"

./build/b1cc tests/m10_compound.c -o "$tmp/m10_compound"
set +e
"$tmp/m10_compound"
rc=$?
set -e
test "$rc" = 42
echo "ok m10_compound"

./build/b1cc tests/m10_switch.c -o "$tmp/m10_switch"
set +e
"$tmp/m10_switch"
rc=$?
set -e
test "$rc" = 42
echo "ok m10_switch"

./build/b1cc tests/m11_globals.c -o "$tmp/m11_globals"
set +e
"$tmp/m11_globals"
rc=$?
set -e
test "$rc" = 42
echo "ok m11_globals"

./build/b1cc tests/m11_pointer_scale.c -o "$tmp/m11_pointer_scale"
set +e
"$tmp/m11_pointer_scale"
rc=$?
set -e
test "$rc" = 42
echo "ok m11_pointer_scale"

./build/b1cc tests/m11_static.c -o "$tmp/m11_static"
set +e
"$tmp/m11_static"
rc=$?
set -e
test "$rc" = 42
echo "ok m11_static"

./build/b1cc tests/m11_multidim.c -o "$tmp/m11_multidim"
set +e
"$tmp/m11_multidim"
rc=$?
set -e
test "$rc" = 42
echo "ok m11_multidim"

./build/b1cc -I tests/include tests/m12_preprocessor.c -o "$tmp/m12_preprocessor"
set +e
"$tmp/m12_preprocessor"
rc=$?
set -e
test "$rc" = 42
echo "ok m12_preprocessor"

./build/b1cc tests/m14_sizeof.c -o "$tmp/m14_sizeof"
set +e
"$tmp/m14_sizeof"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_sizeof"

./build/b1cc tests/m14_cast_trunc.c -o "$tmp/m14_cast_trunc"
set +e
"$tmp/m14_cast_trunc"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_cast_trunc"

./build/b1cc tests/m14_union.c -o "$tmp/m14_union"
set +e
"$tmp/m14_union"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_union"

./build/b1cc tests/m15_abi_stack_args.c -o "$tmp/m15_abi_stack_args"
set +e
"$tmp/m15_abi_stack_args"
rc=$?
set -e
test "$rc" = 0
echo "ok m15_abi_stack_args"

./build/b1cc tests/m15_abi_func_ptr.c -o "$tmp/m15_abi_func_ptr"
set +e
"$tmp/m15_abi_func_ptr"
rc=$?
set -e
test "$rc" = 0
echo "ok m15_abi_func_ptr"

./build/b1cc tests/m15_abi_returns.c -o "$tmp/m15_abi_returns"
set +e
"$tmp/m15_abi_returns"
rc=$?
set -e
test "$rc" = 0
echo "ok m15_abi_returns"

./build/b1cc src/b1cc_token_lexer.c -o "$tmp/b1cc_token_lexer"
"$tmp/b1cc_token_lexer" < tests/local.c > "$tmp/lexer_output.txt"
grep -q "IDENT: int" "$tmp/lexer_output.txt"
grep -q "NUM: 9" "$tmp/lexer_output.txt"
grep -q "EOF" "$tmp/lexer_output.txt"
echo "ok self_hosting_lexer"

./build/b1cc --target=x86_64-b1nix tests/return_42.c -S -o "$tmp/return_42_x86_64.s"
grep -q '^main:' "$tmp/return_42_x86_64.s"
grep -q 'ret' "$tmp/return_42_x86_64.s"
echo "ok x86_64_b1nix_asm"

if [ -x ../b1nix/tools/toolchain/bin/b1nix-cc ]; then
  ./build/b1cc --target=x86_64-b1nix tests/return_42.c -o "$tmp/return_42.b1nix"
  test -s "$tmp/return_42.b1nix"
  echo "ok x86_64_b1nix_elf"
fi

./build/b1cc --target=i386-b1nix tests/return_42.c -S -o "$tmp/return_42_i386.s"
grep -q '^main:' "$tmp/return_42_i386.s"
grep -q 'ret' "$tmp/return_42_i386.s"
echo "ok i386_b1nix_asm"

if [ -x ../b1nix/tools/toolchain/bin/b1nix-cc ]; then
  ./build/b1cc --target=i386-b1nix tests/return_42.c -o "$tmp/return_42_i386.b1nix"
  test -s "$tmp/return_42_i386.b1nix"
  echo "ok i386_b1nix_elf"
fi

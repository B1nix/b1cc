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

./build/b1cc -c tests/puts.c -fdump-symbols -fdump-sections -fdump-relocs -o "$tmp/m6_object_dump.o" > "$tmp/m6_object_dump.txt" 2>&1
test -s "$tmp/m6_object_dump.o"
grep -Eq '(^|[ _])main' "$tmp/m6_object_dump.txt"
grep -Eq '(__text|\\.text)' "$tmp/m6_object_dump.txt"
grep -Eq '(_puts|puts)' "$tmp/m6_object_dump.txt"
echo "ok m6_object_dumps"

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

./build/b1cc tests/m7_varargs_printf.c -o "$tmp/m7_varargs_printf"
"$tmp/m7_varargs_printf" > "$tmp/m7_varargs_printf.out"
grep -qx "1 2" "$tmp/m7_varargs_printf.out"
echo "ok m7_varargs_printf"

if [ -f ../b1nix/userspace/bin/b1cc_hello.c ]; then
  ./build/b1cc ../b1nix/userspace/bin/b1cc_hello.c -o "$tmp/b1nix_b1cc_hello"
  "$tmp/b1nix_b1cc_hello" > "$tmp/b1nix_b1cc_hello.out"
  grep -qx "M25-HELLO: hello from b1cc" "$tmp/b1nix_b1cc_hello.out"
  echo "ok b1nix_userspace_b1cc_hello"
fi

if [ -f ../b1nix/userspace/bin/b1cc_better_c.c ]; then
  ./build/b1cc ../b1nix/userspace/bin/b1cc_better_c.c -o "$tmp/b1nix_b1cc_better_c"
  "$tmp/b1nix_b1cc_better_c" > "$tmp/b1nix_b1cc_better_c.out"
  grep -qx "B1CC-BETTER-C-SMOKE: ok" "$tmp/b1nix_b1cc_better_c.out"
  echo "ok b1nix_userspace_b1cc_better_c"
fi

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

./build/b1cc tests/m14_type_system_regressions.c -o "$tmp/m14_type_system_regressions"
set +e
"$tmp/m14_type_system_regressions"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_type_system_regressions"

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

printf '%s\n' '#ifndef VALUE' '#define VALUE 1' '#endif' 'int main(void) { return VALUE; }' > "$tmp/m15_driver_preprocess.c"
./build/b1cc -E -DVALUE=42 "$tmp/m15_driver_preprocess.c" -o "$tmp/m15_driver_preprocess.i"
grep -q 'return 42' "$tmp/m15_driver_preprocess.i"

./build/b1cc -c tests/return_42.c -o "$tmp/m15_driver_return_42.o"
test -s "$tmp/m15_driver_return_42.o"

printf '%s\n' 'int f(void) { return 40; }' > "$tmp/m15_driver_a.c"
printf '%s\n' 'int f(void); int main(void) { return f() + 2; }' > "$tmp/m15_driver_b.c"
./build/b1cc "$tmp/m15_driver_a.c" "$tmp/m15_driver_b.c" -Wl,-no_warn_duplicate_libraries -o "$tmp/m15_driver_multi"
set +e
"$tmp/m15_driver_multi"
rc=$?
set -e
test "$rc" = 42
echo "ok m15_driver_modes"

# M15: native ELF object output for x86_64-b1nix
./build/b1cc --target=x86_64-b1nix -c tests/return_42.c -o "$tmp/m15_elf_x86_64.o"
test -s "$tmp/m15_elf_x86_64.o"
# Check ELF magic (bytes 0-3: 7f 45 4c 46)
elf_magic=$(od -A n -N 4 -t x1 "$tmp/m15_elf_x86_64.o" | tr -d ' \n')
test "$elf_magic" = "7f454c46"
# Check ELF class = 2 (ELFCLASS64)
elf_class=$(od -A n -j 4 -N 1 -t u1 "$tmp/m15_elf_x86_64.o" | tr -d ' \n')
test "$elf_class" = "2"
echo "ok m15_elf_obj_x86_64"

# Check that nm can find 'main' in native ELF
nm "$tmp/m15_elf_x86_64.o" 2>/dev/null | grep -q "main"
echo "ok m15_elf_symbols_x86_64"

# Check DWARF Line Info section presence in native ELF
./build/b1cc --target=x86_64-b1nix -c -fdump-sections tests/return_42.c -o "$tmp/m15_elf_x86_64_debug.o" > "$tmp/m15_elf_x86_64_debug.txt" 2>&1
grep -q ".debug_line" "$tmp/m15_elf_x86_64_debug.txt"
grep -q ".rela.debug_line" "$tmp/m15_elf_x86_64_debug.txt"
echo "ok m15_elf_debug_line_x86_64"

# M15: native ELF object output for i386-b1nix
./build/b1cc --target=i386-b1nix -c tests/return_42.c -o "$tmp/m15_elf_i386.o"
test -s "$tmp/m15_elf_i386.o"
# Check ELF magic
elf_magic_i=$(od -A n -N 4 -t x1 "$tmp/m15_elf_i386.o" | tr -d ' \n')
test "$elf_magic_i" = "7f454c46"
# Check ELF class = 1 (ELFCLASS32)
elf_class_i=$(od -A n -j 4 -N 1 -t u1 "$tmp/m15_elf_i386.o" | tr -d ' \n')
test "$elf_class_i" = "1"
echo "ok m15_elf_obj_i386"

./build/b1cc tests/m17_preprocessor_full.c -o "$tmp/m17_preprocessor_full"
set +e
"$tmp/m17_preprocessor_full"
rc=$?
set -e
test "$rc" = 0
echo "ok m17_preprocessor_full"

./build/b1cc tests/m17_if_macro_expr.c -o "$tmp/m17_if_macro_expr"
set +e
"$tmp/m17_if_macro_expr"
rc=$?
set -e
test "$rc" = 0
echo "ok m17_if_macro_expr"

./build/b1cc tests/m18_aggregates.c -o "$tmp/m18_aggregates"
set +e
"$tmp/m18_aggregates"
rc=$?
set -e
test "$rc" = 0
echo "ok m18_aggregates"

./build/b1cc tests/m18_aggregate_abi.c -o "$tmp/m18_aggregate_abi"
set +e
"$tmp/m18_aggregate_abi"
rc=$?
set -e
test "$rc" = 0
echo "ok m18_aggregate_abi"

./build/b1cc tests/m18_large_aggregate_abi.c -o "$tmp/m18_large_aggregate_abi"
set +e
"$tmp/m18_large_aggregate_abi"
rc=$?
set -e
test "$rc" = 0
echo "ok m18_large_aggregate_abi"


./build/b1cc tests/m18_bitfields.c -o "$tmp/m18_bitfields"
set +e
"$tmp/m18_bitfields"
rc=$?
set -e
test "$rc" = 0
echo "ok m18_bitfields"

./build/b1cc tests/m19_integer_typing.c -o "$tmp/m19_integer_typing"
set +e
"$tmp/m19_integer_typing"
rc=$?
set -e
test "$rc" = 0
echo "ok m19_integer_typing"

./build/b1cc tests/c11_alignof.c -o "$tmp/c11_alignof"
set +e
"$tmp/c11_alignof"
rc=$?
set -e
test "$rc" = 0
echo "ok c11_alignof"

./build/b1cc tests/c11_static_assert.c -o "$tmp/c11_static_assert"
set +e
"$tmp/c11_static_assert"
rc=$?
set -e
test "$rc" = 0
echo "ok c11_static_assert"

./build/b1cc tests/c11_noreturn.c -o "$tmp/c11_noreturn"
set +e
"$tmp/c11_noreturn"
rc=$?
set -e
test "$rc" = 0
echo "ok c11_noreturn"

./build/b1cc tests/c11_generic.c -o "$tmp/c11_generic"
set +e
"$tmp/c11_generic"
rc=$?
set -e
test "$rc" = 0
echo "ok c11_generic"

./build/b1cc tests/c11_anon_struct.c -o "$tmp/c11_anon_struct"
set +e
"$tmp/c11_anon_struct"
rc=$?
set -e
test "$rc" = 0
echo "ok c11_anon_struct"

./build/b1cc tests/c23_nullptr.c -o "$tmp/c23_nullptr"
set +e
"$tmp/c23_nullptr"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_nullptr"

./build/b1cc tests/c23_bool.c -o "$tmp/c23_bool"
set +e
"$tmp/c23_bool"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_bool"

./build/b1cc tests/c23_static_assert.c -o "$tmp/c23_static_assert"
set +e
"$tmp/c23_static_assert"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_static_assert"

./build/b1cc tests/c23_constexpr.c -o "$tmp/c23_constexpr"
set +e
"$tmp/c23_constexpr"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_constexpr"

./build/b1cc tests/c23_attributes.c -o "$tmp/c23_attributes"
set +e
"$tmp/c23_attributes"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_attributes"

./build/b1cc tests/c23_empty_init.c -o "$tmp/c23_empty_init"
set +e
"$tmp/c23_empty_init"
rc=$?
set -e
test "$rc" = 0
echo "ok c23_empty_init"

./build/b1cc tests/m14_promotions.c -o "$tmp/m14_promotions"
set +e
"$tmp/m14_promotions"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_promotions"

./build/b1cc tests/m15_debug.c -S -o "$tmp/m15_debug.s"
grep -q '\.file 1 "tests/m15_debug.c"' "$tmp/m15_debug.s"
grep -q '\.loc 1 ' "$tmp/m15_debug.s"
echo "ok m15_debug"

./build/b1cc --target=x86_64-b1nix tests/m15_debug.c -S -o "$tmp/m15_debug_x86_64.s"
grep -q '\.file 1 "tests/m15_debug.c"' "$tmp/m15_debug_x86_64.s"
grep -q '\.loc 1 ' "$tmp/m15_debug_x86_64.s"
echo "ok m15_debug_x86_64"

./build/b1cc --target=i386-b1nix tests/m15_debug.c -S -o "$tmp/m15_debug_i386.s"
grep -q '\.file 1 "tests/m15_debug.c"' "$tmp/m15_debug_i386.s"
grep -q '\.loc 1 ' "$tmp/m15_debug_i386.s"
echo "ok m15_debug_i386"

./build/b1cc tests/m17_preproc_edge.c -o "$tmp/m17_preproc_edge"
set +e
"$tmp/m17_preproc_edge"
rc=$?
set -e
test "$rc" = 0
echo "ok m17_preproc_edge"





./build/b1cc src/b1cc_token_lexer.c -o "$tmp/b1cc_token_lexer"
"$tmp/b1cc_token_lexer" < tests/local.c > "$tmp/lexer_output.txt"
grep -q "IDENT: int" "$tmp/lexer_output.txt"
grep -q "NUM: 9" "$tmp/lexer_output.txt"
grep -q "EOF" "$tmp/lexer_output.txt"
echo "ok self_hosting_lexer"

./build/b1cc src/ast.c -c -o "$tmp/ast_self.o"
test -s "$tmp/ast_self.o"
echo "ok self_hosting_ast"

./build/b1cc --target=x86_64-b1nix tests/return_42.c -S -o "$tmp/return_42_x86_64.s"
grep -q '^main:' "$tmp/return_42_x86_64.s"
grep -q 'ret' "$tmp/return_42_x86_64.s"
echo "ok x86_64_b1nix_asm"

./build/b1cc --target=x86_64-b1nix tests/m18_aggregate_abi.c -S -o "$tmp/m18_aggregate_abi_x86_64.s"
grep -q '^make_pair:' "$tmp/m18_aggregate_abi_x86_64.s"
grep -q '^stack_pair:' "$tmp/m18_aggregate_abi_x86_64.s"
echo "ok x86_64_b1nix_m18_aggregate_abi_asm"

./build/b1cc --target=x86_64-b1nix tests/m18_large_aggregate_abi.c -S -o "$tmp/m18_large_aggregate_abi_x86_64.s"
grep -q '^make_large:' "$tmp/m18_large_aggregate_abi_x86_64.s"
grep -q '^stack_large:' "$tmp/m18_large_aggregate_abi_x86_64.s"
echo "ok x86_64_b1nix_m18_large_aggregate_abi_asm"

if [ -x ../b1nix/tools/toolchain/bin/b1nix-cc ]; then
  ./build/b1cc --target=x86_64-b1nix tests/return_42.c -o "$tmp/return_42.b1nix"
  test -s "$tmp/return_42.b1nix"
  echo "ok x86_64_b1nix_elf"

  ./build/b1cc --target=x86_64-b1nix tests/m18_aggregate_abi.c -o "$tmp/m18_aggregate_abi.b1nix"
  test -s "$tmp/m18_aggregate_abi.b1nix"
  echo "ok x86_64_b1nix_m18_aggregate_abi_elf"

  ./build/b1cc --target=x86_64-b1nix tests/m18_large_aggregate_abi.c -o "$tmp/m18_large_aggregate_abi.b1nix"
  test -s "$tmp/m18_large_aggregate_abi.b1nix"
  echo "ok x86_64_b1nix_m18_large_aggregate_abi_elf"
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

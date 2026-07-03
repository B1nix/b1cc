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

./build/b1cc --target=i386-b1nix -c -fdump-sections tests/return_42.c -o "$tmp/m15_elf_i386_text.o" > "$tmp/m15_elf_i386_text.txt" 2>&1
grep -Eq '\.text[[:space:]]+0*11[[:space:]]' "$tmp/m15_elf_i386_text.txt"

./build/b1cc --target=i386-b1nix -c -fdump-relocs tests/puts.c -o "$tmp/m15_elf_i386_reloc.o" > "$tmp/m15_elf_i386_reloc.txt" 2>&1
grep -q "R_386_PC32" "$tmp/m15_elf_i386_reloc.txt"
grep -q "puts" "$tmp/m15_elf_i386_reloc.txt"
echo "ok m15_elf_i386_encoding"

./build/b1cc --target=arm64-darwin -c tests/return_42.c -o "$tmp/m15_macho_arm64.o"
test -s "$tmp/m15_macho_arm64.o"
macho_magic=$(od -A n -N 4 -t x1 "$tmp/m15_macho_arm64.o" | tr -d ' \n')
test "$macho_magic" = "cffaedfe"
cc "$tmp/m15_macho_arm64.o" -o "$tmp/m15_macho_arm64_exec"
set +e
"$tmp/m15_macho_arm64_exec"
rc=$?
set -e
test "$rc" = 42
echo "ok m15_macho_arm64_native"


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

./build/b1cc -I tests/include tests/m21_xmacro_undef.c -o "$tmp/m21_xmacro_undef"
set +e
"$tmp/m21_xmacro_undef"
rc=$?
set -e
test "$rc" = 42
./build/b1cc -E -I tests/include tests/m21_xmacro_undef.c -o "$tmp/m21_xmacro_undef.i"
grep -q 'generated_alpha' "$tmp/m21_xmacro_undef.i"
! grep -q 'M21_ITEM' "$tmp/m21_xmacro_undef.i"
echo "ok m21_xmacro_undef"

mkdir -p "$tmp/preferred/include" "$tmp/fallback/include"
printf '%s\n' '#define WHICH_HEADER 42' > "$tmp/preferred/include/shadowed.h"
printf '%s\n' '#define WHICH_HEADER 1' > "$tmp/fallback/include/shadowed.h"
printf '%s\n' '#include <shadowed.h>' 'int main(void) { return WHICH_HEADER; }' > "$tmp/include_precedence.c"
./build/b1cc -I "$tmp/preferred/include" -I "$tmp/fallback/include" "$tmp/include_precedence.c" -o "$tmp/include_precedence"
set +e
"$tmp/include_precedence"
rc=$?
set -e
test "$rc" = 42
echo "ok include_precedence"

./build/b1cc -I tests/include tests/m21_repeat_xmacro.c -o "$tmp/m21_repeat_xmacro"
set +e
"$tmp/m21_repeat_xmacro"
rc=$?
set -e
test "$rc" = 42
echo "ok m21_repeat_xmacro"

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

./build/b1cc tests/m18_float_aggregate_abi.c -o "$tmp/m18_float_aggregate_abi"
set +e
"$tmp/m18_float_aggregate_abi"
rc=$?
set -e
test "$rc" = 0
echo "ok m18_float_aggregate_abi"

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

./build/b1cc tests/m22_gnu_extensions.c -o "$tmp/m22_gnu_extensions"
set +e
"$tmp/m22_gnu_extensions"
rc=$?
set -e
test "$rc" = 42
./build/b1cc tests/m22_gnu_extensions.c -S -o "$tmp/m22_gnu_extensions.s"
grep -q '_m22_global_asm_marker:' "$tmp/m22_gnu_extensions.s"
grep -q 'nop' "$tmp/m22_gnu_extensions.s"
echo "ok m22_gnu_extensions"

./build/b1cc tests/m22_enum_const_expr.c -o "$tmp/m22_enum_const_expr"
set +e
"$tmp/m22_enum_const_expr"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_enum_const_expr"

./build/b1cc tests/m22_enum_zero_case.c -o "$tmp/m22_enum_zero_case"
set +e
"$tmp/m22_enum_zero_case"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_enum_zero_case"

./build/b1cc tests/m22_comma_expr.c -o "$tmp/m22_comma_expr"
set +e
"$tmp/m22_comma_expr"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_comma_expr"

./build/b1cc tests/m22_adjacent_string_array.c -o "$tmp/m22_adjacent_string_array"
set +e
"$tmp/m22_adjacent_string_array"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_adjacent_string_array"

./build/b1cc tests/m22_goto_label.c -o "$tmp/m22_goto_label"
set +e
"$tmp/m22_goto_label"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_goto_label"

./build/b1cc tests/m22_prefix_lvalue.c -o "$tmp/m22_prefix_lvalue"
set +e
"$tmp/m22_prefix_lvalue"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_prefix_lvalue"

./build/b1cc tests/m22_mixed_local_declarators.c -o "$tmp/m22_mixed_local_declarators"
set +e
"$tmp/m22_mixed_local_declarators"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_mixed_local_declarators"

./build/b1cc tests/m22_short_int_function.c -o "$tmp/m22_short_int_function"
set +e
"$tmp/m22_short_int_function"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_short_int_function"

./build/b1cc tests/m22_anonymous_union_scalar_init.c -o "$tmp/m22_anonymous_union_scalar_init"
set +e
"$tmp/m22_anonymous_union_scalar_init"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_anonymous_union_scalar_init"

./build/b1cc tests/m22_static_local_struct_array_inferred.c -o "$tmp/m22_static_local_struct_array_inferred"
set +e
"$tmp/m22_static_local_struct_array_inferred"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_static_local_struct_array_inferred"

./build/b1cc tests/m22_post_array_attribute.c -o "$tmp/m22_post_array_attribute"
set +e
"$tmp/m22_post_array_attribute"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_post_array_attribute"

./build/b1cc tests/m22_array_range_designator.c -o "$tmp/m22_array_range_designator"
set +e
"$tmp/m22_array_range_designator"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_array_range_designator"

./build/b1cc tests/m22_tcc_struct_table_init.c -o "$tmp/m22_tcc_struct_table_init"
set +e
"$tmp/m22_tcc_struct_table_init"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_tcc_struct_table_init"

./build/b1cc tests/m22_typedef_storage_after_type.c -o "$tmp/m22_typedef_storage_after_type"
set +e
"$tmp/m22_typedef_storage_after_type"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_typedef_storage_after_type"

./build/b1cc tests/m22_typedef_multi_alias_pointer.c -o "$tmp/m22_typedef_multi_alias_pointer"
set +e
"$tmp/m22_typedef_multi_alias_pointer"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_typedef_multi_alias_pointer"

./build/b1cc tests/m22_typedef_array_alias.c -o "$tmp/m22_typedef_array_alias"
set +e
"$tmp/m22_typedef_array_alias"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_typedef_array_alias"

./build/b1cc tests/m22_global_function_pointer_initializer.c -o "$tmp/m22_global_function_pointer_initializer"
set +e
"$tmp/m22_global_function_pointer_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_global_function_pointer_initializer"

./build/b1cc tests/m22_builtin_offsetof_initializer.c -o "$tmp/m22_builtin_offsetof_initializer"
set +e
"$tmp/m22_builtin_offsetof_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_builtin_offsetof_initializer"

./build/b1cc tests/m22_struct_char_array_string_init.c -o "$tmp/m22_struct_char_array_string_init"
set +e
"$tmp/m22_struct_char_array_string_init"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_struct_char_array_string_init"

./build/b1cc tests/m22_local_shadows_enum.c -o "$tmp/m22_local_shadows_enum"
set +e
"$tmp/m22_local_shadows_enum"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_shadows_enum"

./build/b1cc tests/m22_struct_tail_attribute.c -o "$tmp/m22_struct_tail_attribute"
set +e
"$tmp/m22_struct_tail_attribute"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_struct_tail_attribute"

./build/b1cc tests/m22_function_asm_label.c -o "$tmp/m22_function_asm_label"
set +e
"$tmp/m22_function_asm_label"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_function_asm_label"

./build/b1cc tests/m22_local_array_constexpr_dim.c -o "$tmp/m22_local_array_constexpr_dim"
set +e
"$tmp/m22_local_array_constexpr_dim"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_array_constexpr_dim"

./build/b1cc tests/m22_block_scope_extern.c -o "$tmp/m22_block_scope_extern"
set +e
"$tmp/m22_block_scope_extern"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_block_scope_extern"

./build/b1cc tests/m22_local_enum_variable.c -o "$tmp/m22_local_enum_variable"
set +e
"$tmp/m22_local_enum_variable"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_enum_variable"

./build/b1cc tests/m22_function_pointer_type_cast.c -o "$tmp/m22_function_pointer_type_cast"
set +e
"$tmp/m22_function_pointer_type_cast"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_function_pointer_type_cast"

./build/b1cc tests/m22_compound_literal_zero.c -o "$tmp/m22_compound_literal_zero"
set +e
"$tmp/m22_compound_literal_zero"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_compound_literal_zero"

./build/b1cc tests/m22_compound_literal_array_cast.c -o "$tmp/m22_compound_literal_array_cast"
set +e
"$tmp/m22_compound_literal_array_cast"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_compound_literal_array_cast"

./build/b1cc tests/m22_compound_literal_designated_union_arg.c -o "$tmp/m22_compound_literal_designated_union_arg"
set +e
"$tmp/m22_compound_literal_designated_union_arg"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_compound_literal_designated_union_arg"

./build/b1cc tests/m22_compound_literal_small_struct_assign.c -o "$tmp/m22_compound_literal_small_struct_assign"
set +e
"$tmp/m22_compound_literal_small_struct_assign"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_compound_literal_small_struct_assign"

./build/b1cc tests/m22_local_struct_definition_declarator.c -o "$tmp/m22_local_struct_definition_declarator"
set +e
"$tmp/m22_local_struct_definition_declarator"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_struct_definition_declarator"

./build/b1cc tests/m22_array_designated_initializer.c -o "$tmp/m22_array_designated_initializer"
set +e
"$tmp/m22_array_designated_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_array_designated_initializer"

./build/b1cc tests/m22_global_pointer_address_initializer.c -o "$tmp/m22_global_pointer_address_initializer"
set +e
"$tmp/m22_global_pointer_address_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_global_pointer_address_initializer"

./build/b1cc tests/m22_global_function_pointer_array.c -o "$tmp/m22_global_function_pointer_array"
set +e
"$tmp/m22_global_function_pointer_array"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_global_function_pointer_array"

./build/b1cc tests/m22_global_struct_function_pointer_field.c -o "$tmp/m22_global_struct_function_pointer_field"
set +e
"$tmp/m22_global_struct_function_pointer_field"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_global_struct_function_pointer_field"

./build/b1cc tests/m22_struct_function_pointer_return_pointer_field.c -o "$tmp/m22_struct_function_pointer_return_pointer_field"
set +e
"$tmp/m22_struct_function_pointer_return_pointer_field"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_struct_function_pointer_return_pointer_field"

./build/b1cc --target=x86_64-b1nix tests/m22_global_struct_casted_symbol_field.c -S -o "$tmp/m22_global_struct_casted_symbol_field.s"
grep -q 'blob' "$tmp/m22_global_struct_casted_symbol_field.s"
echo "ok m22_global_struct_casted_symbol_field_asm"

./build/b1cc tests/m22_aggregate_call_argument.c -o "$tmp/m22_aggregate_call_argument"
set +e
"$tmp/m22_aggregate_call_argument"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_aggregate_call_argument"

./build/b1cc tests/m22_aggregate_return_call.c -o "$tmp/m22_aggregate_return_call"
set +e
"$tmp/m22_aggregate_return_call"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_aggregate_return_call"

./build/b1cc tests/m22_local_function_prototype.c -o "$tmp/m22_local_function_prototype"
set +e
"$tmp/m22_local_function_prototype"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_function_prototype"

./build/b1cc tests/m22_local_function_pointer_array_inferred.c -o "$tmp/m22_local_function_pointer_array_inferred"
set +e
"$tmp/m22_local_function_pointer_array_inferred"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_local_function_pointer_array_inferred"

./build/b1cc tests/m22_static_local_mixed_scalars.c -o "$tmp/m22_static_local_mixed_scalars"
set +e
"$tmp/m22_static_local_mixed_scalars"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_static_local_mixed_scalars"

./build/b1cc tests/m22_global_pointer_casted_string.c -o "$tmp/m22_global_pointer_casted_string"
set +e
"$tmp/m22_global_pointer_casted_string"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_global_pointer_casted_string"

./build/b1cc tests/m22_nested_array_initializer_braces.c -o "$tmp/m22_nested_array_initializer_braces"
set +e
"$tmp/m22_nested_array_initializer_braces"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_nested_array_initializer_braces"

./build/b1cc tests/m22_wide_char_literal.c -o "$tmp/m22_wide_char_literal"
set +e
"$tmp/m22_wide_char_literal"
rc=$?
set -e
test "$rc" = 42
echo "ok m22_wide_char_literal"


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

./build/b1cc tests/m19_float_scalar.c -o "$tmp/m19_float_scalar"
set +e
"$tmp/m19_float_scalar"
rc=$?
set -e
test "$rc" = 0
echo "ok m19_float_scalar"

# M19: float/double scalar codegen also targets x86_64 and i386 backends.
./build/b1cc --target=x86_64-b1nix tests/m19_float_scalar.c -S -o "$tmp/m19_float_scalar_x86_64.s"
grep -q 'mulsd\|addsd\|cvtsi2sd' "$tmp/m19_float_scalar_x86_64.s"
echo "ok m19_float_scalar_x86_64_asm"

./build/b1cc --target=i386-b1nix tests/m19_float_scalar.c -S -o "$tmp/m19_float_scalar_i386.s"
grep -q 'fmulp\|faddp\|fildl' "$tmp/m19_float_scalar_i386.s"
echo "ok m19_float_scalar_i386_asm"

./build/b1cc tests/m19_long_long_i386.c -o "$tmp/m19_long_long_i386"
set +e
"$tmp/m19_long_long_i386"
rc=$?
set -e
test "$rc" = 0
echo "ok m19_long_long"

./build/b1cc --target=i386-b1nix tests/m19_long_long_i386.c -S -o "$tmp/m19_long_long_i386.s"
grep -q 'adcl' "$tmp/m19_long_long_i386.s"
grep -q '__divdi3' "$tmp/m19_long_long_i386.s"
grep -q '__moddi3' "$tmp/m19_long_long_i386.s"
echo "ok m19_long_long_i386_asm"

./build/b1cc --target=i386-b1nix tests/m22_i386_inline_asm_labels.c -S -o "$tmp/m22_i386_inline_asm_labels.s"
grep -q 'movl %edx, %eax' "$tmp/m22_i386_inline_asm_labels.s"
grep -q '.Li386_one_cmp_high' "$tmp/m22_i386_inline_asm_labels.s"
grep -q '.Li386_two_cmp_high' "$tmp/m22_i386_inline_asm_labels.s"
echo "ok m22_i386_inline_asm_labels_asm"

./build/b1cc tests/m14_qualifiers.c -o "$tmp/m14_qualifiers"
set +e
"$tmp/m14_qualifiers"
rc=$?
set -e
test "$rc" = 0
echo "ok m14_qualifiers"

# M14: writing to a const-qualified object is rejected at compile time.
printf '%s\n' 'int main(void){ const int x = 1; x = 2; return x; }' > "$tmp/m14_const_violation.c"
set +e
./build/b1cc "$tmp/m14_const_violation.c" -S -o "$tmp/m14_const_violation.s" 2> "$tmp/m14_const_violation.err"
rc=$?
set -e
test "$rc" != 0
grep -q "const-qualified" "$tmp/m14_const_violation.err"
echo "ok m14_const_violation_rejected"

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





./build/b1cc tests/m20_callee_varargs.c -o "$tmp/m20_callee_varargs"
set +e
"$tmp/m20_callee_varargs"
rc=$?
set -e
test "$rc" = 42
echo "ok m20_callee_varargs"

./build/b1cc tests/m20_self_host_local_array.c -o "$tmp/m20_self_host_local_array"
set +e
"$tmp/m20_self_host_local_array"
rc=$?
set -e
test "$rc" = 42
echo "ok m20_self_host_local_array"

./build/b1cc tests/m20_aggregate_copy_nested.c -o "$tmp/m20_aggregate_copy_nested"
set +e
"$tmp/m20_aggregate_copy_nested"
rc=$?
set -e
test "$rc" = 42
echo "ok m20_aggregate_copy_nested"

./build/b1cc tests/m20_assign_global_to_local.c -o "$tmp/m20_assign_global_to_local"
set +e
"$tmp/m20_assign_global_to_local"
rc=$?
set -e
test "$rc" = 42
echo "ok m20_assign_global_to_local"

./build/b1cc tests/m20_assign_nested_array_union.c -o "$tmp/m20_assign_nested_array_union"
set +e
"$tmp/m20_assign_nested_array_union"
rc=$?
set -e
test "$rc" = 42
echo "ok m20_assign_nested_array_union"

./build/b1cc src/b1cc_token_lexer.c -o "$tmp/b1cc_token_lexer"
"$tmp/b1cc_token_lexer" < tests/local.c > "$tmp/lexer_output.txt"
grep -q "IDENT: int" "$tmp/lexer_output.txt"
grep -q "NUM: 9" "$tmp/lexer_output.txt"
grep -q "EOF" "$tmp/lexer_output.txt"
echo "ok self_hosting_lexer"

./build/b1cc src/ast.c -c -o "$tmp/ast_self.o"
test -s "$tmp/ast_self.o"
echo "ok self_hosting_ast"

# Self-hosting roundtrip: compile b1cc codebase with b1cc
echo "building b1cc_self using b1cc..."
./build/b1cc src/ast.c -c -o "$tmp/ast_self.o"
./build/b1cc src/b1cc.c -c -o "$tmp/b1cc_self.o"
./build/b1cc src/backend.c -c -o "$tmp/backend_self.o"
./build/b1cc src/backend_arm64.c -c -o "$tmp/backend_arm64_self.o"
./build/b1cc src/backend_x86_64.c -c -o "$tmp/backend_x86_64_self.o"
./build/b1cc src/backend_i386.c -c -o "$tmp/backend_i386_self.o"
./build/b1cc src/common.c -c -o "$tmp/common_self.o"
./build/b1cc src/diagnostics.c -c -o "$tmp/diagnostics_self.o"
./build/b1cc src/elf_writer.c -c -o "$tmp/elf_writer_self.o"
./build/b1cc src/ir.c -c -o "$tmp/ir_self.o"
./build/b1cc src/lexer.c -c -o "$tmp/lexer_self.o"
./build/b1cc src/macho_writer.c -c -o "$tmp/macho_writer_self.o"
./build/b1cc src/parser.c -c -o "$tmp/parser_self.o"
./build/b1cc src/preprocessor.c -c -o "$tmp/preprocessor_self.o"

# Link self-hosted binary
cc "$tmp"/ast_self.o "$tmp"/b1cc_self.o "$tmp"/backend_self.o "$tmp"/backend_arm64_self.o "$tmp"/backend_x86_64_self.o "$tmp"/backend_i386_self.o "$tmp"/common_self.o "$tmp"/diagnostics_self.o "$tmp"/elf_writer_self.o "$tmp"/ir_self.o "$tmp"/lexer_self.o "$tmp"/macho_writer_self.o "$tmp"/parser_self.o "$tmp"/preprocessor_self.o -o "$tmp/b1cc_self"
test -s "$tmp/b1cc_self"
echo "ok self_hosted_binary_build"

# Verify build/b1cc_self compiles and links a covered regression corpus.
self_host_case() {
    src="$1"
    expect="$2"
    shift 2
    name=$(basename "$src" .c)
    "$tmp/b1cc_self" "$src" -o "$tmp/${name}_self"
    set +e
    "$tmp/${name}_self" "$@"
    rc=$?
    set -e
    test "$rc" = "$expect"
    echo "ok self_hosted_$name"
}

self_host_case tests/return_42.c 42
self_host_case tests/precedence.c 14
self_host_case tests/local.c 18
self_host_case tests/if_else.c 7
self_host_case tests/while.c 10
self_host_case tests/for.c 15
self_host_case tests/function.c 42
self_host_case tests/argc.c 4 a b c
self_host_case tests/argv.c 10 aa bbb cccc
self_host_case tests/string_pointer.c 10
self_host_case tests/m20_callee_varargs.c 42
self_host_case tests/m20_self_host_local_array.c 42
self_host_case tests/m20_assign_global_to_local.c 42
self_host_case tests/m20_assign_nested_array_union.c 42
self_host_case tests/m22_nested_array_initializer_braces.c 42
self_host_case tests/m22_wide_char_literal.c 42
echo "ok self_hosted_binary_compiles_corpus"

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

./build/b1cc --target=x86_64-b1nix tests/m18_float_aggregate_abi.c -S -o "$tmp/m18_float_aggregate_abi_x86_64.s"
grep -q '^make_f2:' "$tmp/m18_float_aggregate_abi_x86_64.s"
grep -q 'xmm' "$tmp/m18_float_aggregate_abi_x86_64.s"
echo "ok x86_64_b1nix_m18_float_aggregate_abi_asm"

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

  ./build/b1cc --target=x86_64-b1nix tests/m18_float_aggregate_abi.c -o "$tmp/m18_float_aggregate_abi.b1nix"
  test -s "$tmp/m18_float_aggregate_abi.b1nix"
  echo "ok x86_64_b1nix_m18_float_aggregate_abi_elf"

  ./build/b1cc --target=x86_64-b1nix tests/m19_float_scalar.c -o "$tmp/m19_float_scalar.b1nix"
  test -s "$tmp/m19_float_scalar.b1nix"
  echo "ok x86_64_b1nix_m19_float_scalar_elf"

  ./build/b1cc --target=x86_64-b1nix tests/c23_bool.c -o "$tmp/c23_bool.b1nix"
  test -s "$tmp/c23_bool.b1nix"
  echo "ok x86_64_b1nix_c23_bool_elf"
fi

./build/b1cc --target=i386-b1nix tests/return_42.c -S -o "$tmp/return_42_i386.s"
grep -q '^main:' "$tmp/return_42_i386.s"
grep -q 'ret' "$tmp/return_42_i386.s"
echo "ok i386_b1nix_asm"

# ── M23: .S assembly file handling ──

# Test: b1cc handles .S assembly files (x86_64) — assemble to object via B1NIX crt0
./build/b1cc --target=x86_64-b1nix -c ../b1nix/userspace/crt/crt0.S -o "$tmp/crt0_x86_64.o"
test -s "$tmp/crt0_x86_64.o"
echo "ok m23_crt0_x86_64_assemble"

# Test: b1cc handles .S assembly files (i386) — assemble to object
./build/b1cc --target=i386-b1nix -c ../b1nix/userspace/crt/crt0.S -o "$tmp/crt0_i386.o"
test -s "$tmp/crt0_i386.o"
echo "ok m23_crt0_i386_assemble"

# Test: multi-file compilation with .S + .c inputs (object output, no linking)
./build/b1cc --target=x86_64-b1nix -c ../b1nix/userspace/crt/crt0.S tests/return_42.c
echo "ok m23_multifile_sc"

# Test: driver flags -nostdlib and -T are collected as link_flags
# (verify they don't cause a parse error — linking needs b1nix-cc or ld.lld)
./build/b1cc --target=x86_64-b1nix tests/return_42.c -nostdlib -o "$tmp/nostdlib_test" 2>/dev/null || true
echo "ok m23_driver_flags_nostdlib"

# Conditional: verify ELF object output from .S assembly
if [ -x ../b1nix/tools/toolchain/bin/b1nix-cc ]; then
  ./build/b1cc --target=x86_64-b1nix -c ../b1nix/userspace/crt/crt0.S -o "$tmp/crt0_x86_64_link.o"
  nm "$tmp/crt0_x86_64_link.o" 2>/dev/null | grep -q '_start'
  echo "ok m23_crt0_x86_64_has_start"
  od -A n -N 4 -t x1 "$tmp/crt0_x86_64_link.o" | grep -q '7f.*45.*4c.*46'
  echo "ok m23_crt0_x86_64_is_elf"

  ./build/b1cc --target=i386-b1nix -c ../b1nix/userspace/crt/crt0.S -o "$tmp/crt0_i386_link.o"
  nm "$tmp/crt0_i386_link.o" 2>/dev/null | grep -q '_start'
  echo "ok m23_crt0_i386_has_start"
  od -A n -N 4 -t x1 "$tmp/crt0_i386_link.o" | grep -q '7f.*45.*4c.*46'
  echo "ok m23_crt0_i386_is_elf"
fi

# === M24: Kernel Code Model Tests ===

# Test: -mcmodel=kernel produces absolute addressing for x86_64 globals
cat > "$tmp/mcmodel_test.c" << 'EOF'
int kernel_var = 42;
int *kernel_ptr;
char kernel_buf[64];

int main(void) {
    kernel_var = 10;
    kernel_ptr = &kernel_var;
    return kernel_var;
}
EOF

./build/b1cc --target=x86_64-b1nix -S -mcmodel=kernel "$tmp/mcmodel_test.c" -o "$tmp/mcmodel_test.s"
# Verify kernel model uses absolute addressing (movabs) not RIP-relative for globals
grep -q 'movabs.*kernel_var' "$tmp/mcmodel_test.s"
echo "ok m24_kernel_model_absolute_gaddr"
grep -q 'movabs.*kernel_ptr' "$tmp/mcmodel_test.s"
echo "ok m24_kernel_model_absolute_gload"
# Verify small model uses RIP-relative
./build/b1cc --target=x86_64-b1nix -S -mcmodel=small "$tmp/mcmodel_test.c" -o "$tmp/mcmodel_small.s"
grep -q 'kernel_var(%rip)' "$tmp/mcmodel_small.s"
echo "ok m24_small_model_riprel"
# Default (no -mcmodel) should behave like small
./build/b1cc --target=x86_64-b1nix -S "$tmp/mcmodel_test.c" -o "$tmp/mcmodel_default.s"
grep -q 'kernel_var(%rip)' "$tmp/mcmodel_default.s"
echo "ok m24_default_model_riprel"

# === M25: Kernel Target & Makefile Integration Tests ===

# Test: --target=x86_64-elf compiles to assembly
cat > "$tmp/kernel_target_test.c" << 'EOF'
int kvar = 1;
int _start(void) { return kvar; }
EOF
./build/b1cc --target=x86_64-elf -mcmodel=kernel -S "$tmp/kernel_target_test.c" -o "$tmp/kernel_target_test.s"
grep -q '_start' "$tmp/kernel_target_test.s"
echo "ok m25_x86_64_elf_asm"

# Test: --target=x86_64-elf compiles to ELF object
./build/b1cc --target=x86_64-elf -mcmodel=kernel -c "$tmp/kernel_target_test.c" -o "$tmp/kernel_target_test.o"
od -A n -N 4 -t x1 "$tmp/kernel_target_test.o" | grep -q '7f.*45.*4c.*46'
echo "ok m25_x86_64_elf_object"

# Test: --target=x86_64-elf with kernel model uses absolute addressing
grep -q 'movabs' "$tmp/kernel_target_test.s"
echo "ok m25_x86_64_elf_kernel_absolute"

# Test: --target=i686-elf compiles to ELF object
./build/b1cc --target=i686-elf -c "$tmp/kernel_target_test.c" -o "$tmp/kernel_target_test_i686.o"
od -A n -N 4 -t x1 "$tmp/kernel_target_test_i686.o" | grep -q '7f.*45.*4c.*46'
echo "ok m25_i686_elf_object"

# Test: kernel target unknown flags are ignored (passed to link_flags, not causing errors)
./build/b1cc --target=x86_64-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -std=c11 -Wall -Wextra -S "$tmp/kernel_target_test.c" -o "$tmp/kernel_target_flags.s" 2>/dev/null
echo "ok m25_kernel_target_unknown_flags"

# Test: kernel target with .S assembly file (delegated to clang)
cat > "$tmp/kernel_entry.S" << 'ASM'
.globl _start
_start:
    mov $60, %rax
    xor %rdi, %rdi
    syscall
ASM
./build/b1cc --target=x86_64-elf -c "$tmp/kernel_entry.S" -o "$tmp/kernel_entry.o" 2>/dev/null
test -s "$tmp/kernel_entry.o"
echo "ok m25_kernel_asm_assembly"

# === M26: Dynamic Linking & PIC Tests ===

# Test: -fPIC produces GOT-indirect addressing in x86_64 assembly
./build/b1cc --target=x86_64-b1nix -fPIC -S tests/m26_pic_basic.c -o "$tmp/m26_pic_basic.s"
grep -q 'GOTPCREL' "$tmp/m26_pic_basic.s"
echo "ok m26_pic_asm_gotpcrel"

# Test: -fpie also produces GOT-indirect addressing
./build/b1cc --target=x86_64-b1nix -fpie -S tests/m26_pic_basic.c -o "$tmp/m26_pic_pie.s"
grep -q 'GOTPCREL' "$tmp/m26_pic_pie.s"
echo "ok m26_pie_asm_gotpcrel"

# Test: -fPIC produces valid ELF object with native ELF writer
./build/b1cc --target=x86_64-b1nix -fPIC -c tests/m26_pic_basic.c -o "$tmp/m26_pic_basic.o"
test -s "$tmp/m26_pic_basic.o"
echo "ok m26_pic_elf_object"

# Test: ELF object from PIC has R_X86_64_GOTPCREL relocations
./build/b1cc --target=x86_64-b1nix -fPIC -c -fdump-relocs tests/m26_pic_basic.c -o "$tmp/m26_pic_reloc.o" > "$tmp/m26_pic_reloc.txt" 2>&1
grep -q 'R_X86_64_GOTPCREL' "$tmp/m26_pic_reloc.txt"
echo "ok m26_pic_elf_gotpcrel_reloc"

# Test: non-PIC (default) does not contain GOTPCREL references
./build/b1cc --target=x86_64-b1nix -S tests/m26_pic_basic.c -o "$tmp/m26_pic_nopic.s"
if grep -q 'GOTPCREL' "$tmp/m26_pic_nopic.s"; then
    echo "FAIL: non-PIC mode should not contain GOTPCREL"
    exit 1
fi
echo "ok m26_nopic_no_gotpcrel"

# Test: -fPIC compiles correctly on host (arm64-darwin)
./build/b1cc -fPIC tests/m26_pic_basic.c -o "$tmp/m26_pic_basic_host"
set +e
"$tmp/m26_pic_basic_host"
rc=$?
set -e
test "$rc" = 10
echo "ok m26_pic_host_exec"

# M26 — i386 PIC tests
./build/b1cc --target=i386-b1nix -fPIC -S tests/m26_pic_basic.c -o "$tmp/m26_pic_i386.s"
grep -q '@GOT(%ebx)' "$tmp/m26_pic_i386.s"
echo "ok m26_pic_i386_asm_got"

./build/b1cc --target=i386-b1nix -fPIC -c tests/m26_pic_basic.c -o "$tmp/m26_pic_i386.o"
test -s "$tmp/m26_pic_i386.o"
echo "ok m26_pic_i386_elf_object"

./build/b1cc --target=i386-b1nix -fPIC -c -fdump-relocs tests/m26_pic_basic.c -o "$tmp/m26_pic_i386_reloc.o" > "$tmp/m26_pic_i386_reloc.txt" 2>&1
grep -q 'R_386_GOT32' "$tmp/m26_pic_i386_reloc.txt"
echo "ok m26_pic_i386_elf_got32_reloc"

# i386 PIC: check @PLT suffix on function calls
./build/b1cc --target=i386-b1nix -fPIC -S tests/puts.c -o "$tmp/m26_pic_i386_plt.s"
grep -q '@PLT' "$tmp/m26_pic_i386_plt.s"
echo "ok m26_pic_i386_asm_plt"

# i386 non-PIC: no @GOT(%ebx)
./build/b1cc --target=i386-b1nix -S tests/m26_pic_basic.c -o "$tmp/m26_pic_i386_nopic.s"
if grep -q '@GOT' "$tmp/m26_pic_i386_nopic.s"; then
    echo "FAIL: @GOT should not be present in non-PIC output"
    exit 1
fi
echo "ok m26_pic_i386_nopic_no_got"

# === M27: Csmith Coverage & Differential Testing ===

./build/b1cc tests/m27_union_function_decl.c -o "$tmp/m27_union_function_decl"
set +e
"$tmp/m27_union_function_decl"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_union_function_decl"

./build/b1cc tests/m27_for_subscript_lvalue.c -o "$tmp/m27_for_subscript_lvalue"
set +e
"$tmp/m27_for_subscript_lvalue"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_for_subscript_lvalue"

./build/b1cc tests/m27_global_address_initializer.c -o "$tmp/m27_global_address_initializer"
set +e
"$tmp/m27_global_address_initializer"
rc=$?
set -e
test "$rc" = 42
./build/b1cc tests/m27_global_address_initializer.c -S -o "$tmp/m27_global_address_initializer.s"
grep -q '_g+8' "$tmp/m27_global_address_initializer.s"
grep -q '_s+4' "$tmp/m27_global_address_initializer.s"
echo "ok m27_global_address_initializer"

./build/b1cc tests/m27_global_pointer_aggregate_initializer.c -o "$tmp/m27_global_pointer_aggregate_initializer"
set +e
"$tmp/m27_global_pointer_aggregate_initializer"
rc=$?
set -e
test "$rc" = 42
./build/b1cc tests/m27_global_pointer_aggregate_initializer.c -S -o "$tmp/m27_global_pointer_aggregate_initializer.s"
grep -q '_g' "$tmp/m27_global_pointer_aggregate_initializer.s"
echo "ok m27_global_pointer_aggregate_initializer"

./build/b1cc tests/m27_aggregate_assignment_argument.c -o "$tmp/m27_aggregate_assignment_argument"
set +e
"$tmp/m27_aggregate_assignment_argument"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_aggregate_assignment_argument"

./build/b1cc tests/m27_global_struct_array_stride.c -o "$tmp/m27_global_struct_array_stride"
set +e
"$tmp/m27_global_struct_array_stride"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_global_struct_array_stride"

./build/b1cc tests/m27_unsigned_array_comparison.c -o "$tmp/m27_unsigned_array_comparison"
set +e
"$tmp/m27_unsigned_array_comparison"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_array_comparison"

./build/b1cc tests/m27_unsigned_call_comparison.c -o "$tmp/m27_unsigned_call_comparison"
set +e
"$tmp/m27_unsigned_call_comparison"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_call_comparison"

./build/b1cc tests/m27_local_narrow_initializer_trunc.c -o "$tmp/m27_local_narrow_initializer_trunc"
set +e
"$tmp/m27_local_narrow_initializer_trunc"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_local_narrow_initializer_trunc"

./build/b1cc tests/m27_unsigned_local_array_load.c -o "$tmp/m27_unsigned_local_array_load"
set +e
"$tmp/m27_unsigned_local_array_load"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_local_array_load"

./build/b1cc tests/m27_unsigned_narrow_return_param.c -o "$tmp/m27_unsigned_narrow_return_param"
set +e
"$tmp/m27_unsigned_narrow_return_param"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_narrow_return_param"

./build/b1cc tests/m27_small_aggregate_assignment.c -o "$tmp/m27_small_aggregate_assignment"
set +e
"$tmp/m27_small_aggregate_assignment"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_small_aggregate_assignment"

./build/b1cc tests/m27_signed_store_index_assignment_value.c -o "$tmp/m27_signed_store_index_assignment_value"
set +e
"$tmp/m27_signed_store_index_assignment_value"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_signed_store_index_assignment_value"

./build/b1cc tests/m27_pointer_pointee_store_scale.c -o "$tmp/m27_pointer_pointee_store_scale"
set +e
"$tmp/m27_pointer_pointee_store_scale"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_pointer_pointee_store_scale"

./build/b1cc tests/m27_global_pointer_pointee_store_scale.c -o "$tmp/m27_global_pointer_pointee_store_scale"
set +e
"$tmp/m27_global_pointer_pointee_store_scale"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_global_pointer_pointee_store_scale"

./build/b1cc tests/m27_nested_aggregate_assignment.c -o "$tmp/m27_nested_aggregate_assignment"
set +e
"$tmp/m27_nested_aggregate_assignment"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_nested_aggregate_assignment"

./build/b1cc tests/m27_nested_pointer_aggregate_assignment.c -o "$tmp/m27_nested_pointer_aggregate_assignment"
set +e
"$tmp/m27_nested_pointer_aggregate_assignment"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_nested_pointer_aggregate_assignment"

./build/b1cc tests/m27_small_aggregate_call_assignment.c -o "$tmp/m27_small_aggregate_call_assignment"
set +e
"$tmp/m27_small_aggregate_call_assignment"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_small_aggregate_call_assignment"

./build/b1cc tests/m27_hex_literal_unsigned_compare.c -o "$tmp/m27_hex_literal_unsigned_compare"
set +e
"$tmp/m27_hex_literal_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_hex_literal_unsigned_compare"

./build/b1cc tests/m27_signed_narrow_call_unsigned_compare.c -o "$tmp/m27_signed_narrow_call_unsigned_compare"
set +e
"$tmp/m27_signed_narrow_call_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_signed_narrow_call_unsigned_compare"

./build/b1cc tests/m27_small_aggregate_pointer_store.c -o "$tmp/m27_small_aggregate_pointer_store"
set +e
"$tmp/m27_small_aggregate_pointer_store"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_small_aggregate_pointer_store"

./build/b1cc tests/m27_global_narrow_assignment_expr_value.c -o "$tmp/m27_global_narrow_assignment_expr_value"
set +e
"$tmp/m27_global_narrow_assignment_expr_value"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_global_narrow_assignment_expr_value"

./build/b1cc tests/m27_store_index_assignment_expr_value.c -o "$tmp/m27_store_index_assignment_expr_value"
set +e
"$tmp/m27_store_index_assignment_expr_value"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_store_index_assignment_expr_value"

./build/b1cc tests/m27_narrow_unsigned_promotion_compare.c -o "$tmp/m27_narrow_unsigned_promotion_compare"
set +e
"$tmp/m27_narrow_unsigned_promotion_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_narrow_unsigned_promotion_compare"

./build/b1cc tests/m27_nested_tiny_struct_assignment_expr.c -o "$tmp/m27_nested_tiny_struct_assignment_expr"
set +e
"$tmp/m27_nested_tiny_struct_assignment_expr"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_nested_tiny_struct_assignment_expr"

./build/b1cc tests/m27_comparison_result_is_signed_int.c -o "$tmp/m27_comparison_result_is_signed_int"
set +e
"$tmp/m27_comparison_result_is_signed_int"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_comparison_result_is_signed_int"

./build/b1cc tests/m27_assignment_expr_type_for_unsigned_compare.c -o "$tmp/m27_assignment_expr_type_for_unsigned_compare"
set +e
"$tmp/m27_assignment_expr_type_for_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_assignment_expr_type_for_unsigned_compare"

./build/b1cc tests/m27_prefix_inc_deref_unsigned_compare.c -o "$tmp/m27_prefix_inc_deref_unsigned_compare"
set +e
"$tmp/m27_prefix_inc_deref_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_prefix_inc_deref_unsigned_compare"

./build/b1cc tests/m27_signed_pointer_deref_compare.c -o "$tmp/m27_signed_pointer_deref_compare"
set +e
"$tmp/m27_signed_pointer_deref_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_signed_pointer_deref_compare"

./build/b1cc tests/m27_unsigned_pointer_deref_compare.c -o "$tmp/m27_unsigned_pointer_deref_compare"
set +e
"$tmp/m27_unsigned_pointer_deref_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_pointer_deref_compare"

./build/b1cc tests/m27_signed_param_pointer_deref_compare.c -o "$tmp/m27_signed_param_pointer_deref_compare"
set +e
"$tmp/m27_signed_param_pointer_deref_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_signed_param_pointer_deref_compare"

./build/b1cc tests/m27_narrow_signed_return_unsigned_compare.c -o "$tmp/m27_narrow_signed_return_unsigned_compare"
set +e
"$tmp/m27_narrow_signed_return_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_narrow_signed_return_unsigned_compare"

./build/b1cc tests/m27_nested_struct_pointer_assignment_expr.c -o "$tmp/m27_nested_struct_pointer_assignment_expr"
set +e
"$tmp/m27_nested_struct_pointer_assignment_expr"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_nested_struct_pointer_assignment_expr"

./build/b1cc tests/m27_unsigned_short_for_update_wrap.c -o "$tmp/m27_unsigned_short_for_update_wrap"
set +e
"$tmp/m27_unsigned_short_for_update_wrap"
rc=$?
set -e
test "$rc" = 42
echo "ok m27_unsigned_short_for_update_wrap"

./build/b1cc tests/m28_comma_lvalue.c -o "$tmp/m28_comma_lvalue"
set +e
"$tmp/m28_comma_lvalue"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_comma_lvalue"

./build/b1cc tests/m28_local_pointer_array.c -o "$tmp/m28_local_pointer_array"
set +e
"$tmp/m28_local_pointer_array"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_local_pointer_array"

./build/b1cc tests/m28_comma_aggregate_value.c -o "$tmp/m28_comma_aggregate_value"
set +e
"$tmp/m28_comma_aggregate_value"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_comma_aggregate_value"

./build/b1cc tests/m28_comma_aggregate_assignment_arg.c -o "$tmp/m28_comma_aggregate_assignment_arg"
set +e
"$tmp/m28_comma_aggregate_assignment_arg"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_comma_aggregate_assignment_arg"

./build/b1cc tests/m28_narrow_local_pointer_store_load.c -o "$tmp/m28_narrow_local_pointer_store_load"
set +e
"$tmp/m28_narrow_local_pointer_store_load"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_narrow_local_pointer_store_load"

./build/b1cc tests/m28_global_pointer_array.c -o "$tmp/m28_global_pointer_array"
set +e
"$tmp/m28_global_pointer_array"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_global_pointer_array"

./build/b1cc tests/m28_unary_promoted_unsigned.c -o "$tmp/m28_unary_promoted_unsigned"
set +e
"$tmp/m28_unary_promoted_unsigned"
rc=$?
set -e
test "$rc" = 0
echo "ok m28_unary_promoted_unsigned"

./build/b1cc tests/m28_comma_aggregate_call.c -o "$tmp/m28_comma_aggregate_call"
set +e
"$tmp/m28_comma_aggregate_call"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_comma_aggregate_call"

./build/b1cc tests/m28_comma_discarded_aggregate_call_once.c -o "$tmp/m28_comma_discarded_aggregate_call_once"
set +e
"$tmp/m28_comma_discarded_aggregate_call_once"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_comma_discarded_aggregate_call_once"

./build/b1cc tests/m28_unsigned_longlong_initializer.c -o "$tmp/m28_unsigned_longlong_initializer"
set +e
"$tmp/m28_unsigned_longlong_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_unsigned_longlong_initializer"

./build/b1cc tests/m28_large_hex_ll_unsigned_compare.c -o "$tmp/m28_large_hex_ll_unsigned_compare"
set +e
"$tmp/m28_large_hex_ll_unsigned_compare"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_large_hex_ll_unsigned_compare"

./build/b1cc tests/m28_bitfield_initializer_copy.c -o "$tmp/m28_bitfield_initializer_copy"
set +e
"$tmp/m28_bitfield_initializer_copy"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_bitfield_initializer_copy"

./build/b1cc tests/m28_signed_bitfield_load.c -o "$tmp/m28_signed_bitfield_load"
set +e
"$tmp/m28_signed_bitfield_load"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_signed_bitfield_load"

./build/b1cc tests/m28_union_bitfield_initializer.c -o "$tmp/m28_union_bitfield_initializer"
set +e
"$tmp/m28_union_bitfield_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_union_bitfield_initializer"

./build/b1cc tests/m28_unsigned_equality_conversion.c -o "$tmp/m28_unsigned_equality_conversion"
set +e
"$tmp/m28_unsigned_equality_conversion"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_unsigned_equality_conversion"

./build/b1cc tests/m28_unsigned_mod64.c -o "$tmp/m28_unsigned_mod64"
set +e
"$tmp/m28_unsigned_mod64"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_unsigned_mod64"

./build/b1cc tests/m28_bitwise_wide_conversion.c -o "$tmp/m28_bitwise_wide_conversion"
set +e
"$tmp/m28_bitwise_wide_conversion"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_bitwise_wide_conversion"

./build/b1cc tests/m28_unsigned_cast_mod_assignment_expr.c -o "$tmp/m28_unsigned_cast_mod_assignment_expr"
set +e
"$tmp/m28_unsigned_cast_mod_assignment_expr"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_unsigned_cast_mod_assignment_expr"

./build/b1cc tests/m28_local_union_array_initializer.c -o "$tmp/m28_local_union_array_initializer"
set +e
"$tmp/m28_local_union_array_initializer"
rc=$?
set -e
test "$rc" = 42
echo "ok m28_local_union_array_initializer"

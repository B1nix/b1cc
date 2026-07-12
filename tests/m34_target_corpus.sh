#!/bin/sh
# M34: compile every runtime differential corpus member for x86_64-b1nix.
# This is deliberately a separate target build gate; execution belongs to the
# B1NIX/QEMU harness and must not be represented by a host-side link result.
set -u

B1CC="${B1CC:-./build/b1cc}"
tmp="${TMPDIR:-/tmp}/b1cc-c11-target-corpus"
rm -rf "$tmp"
mkdir -p "$tmp"

CORPUS="tests/return_42.c tests/precedence.c tests/local.c tests/if_else.c \
tests/while.c tests/for.c tests/function.c tests/string_pointer.c \
tests/m34_control_flow.c tests/m34_expression_semantics.c \
tests/m34_typedef_array_decay.c tests/m34_local_partial_init.c \
tests/m34_knr_params.c tests/m34_const_correct.c tests/m34_complex_storage.c \
tests/m34_vla_loop.c tests/m34_hideset_recursion.c tests/m34_va_copy.c \
tests/m34_weak_symbol.c tests/m34_headers.c tests/m34_runtime.c tests/puts.c \
tests/m10_switch.c tests/m11_globals.c tests/m18_aggregates.c"

pass=0
fail=0
for src in $CORPUS; do
    name=$(basename "$src" .c)
    if "$B1CC" --target=x86_64-b1nix "$src" -o "$tmp/$name" \
        >"$tmp/$name.out" 2>"$tmp/$name.err"; then
        echo "ok   $name"
        pass=$((pass + 1))
    else
        echo "FAIL $name"
        tail -5 "$tmp/$name.err"
        fail=$((fail + 1))
    fi
done

echo "=== x86_64-b1nix target corpus: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]

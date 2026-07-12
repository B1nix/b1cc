#!/bin/sh
# M34: reference-compiler differential runner. Compiles and runs a corpus of
# self-contained programs with both b1cc and a reference compiler (cc), and
# requires identical process exit status AND identical stdout. Any divergence is
# a compiler-side failure. Reference-only build failures are reported separately
# as skips. No `set -e`: corpus programs use non-zero exit codes as markers.
set -u

B1CC="${B1CC:-./build/b1cc}"
REF="${REF:-cc}"
tmp="${TMPDIR:-/tmp}/b1cc-c11-diff"
rm -rf "$tmp"; mkdir -p "$tmp"

CORPUS="tests/return_42.c tests/precedence.c tests/local.c tests/if_else.c \
tests/while.c tests/for.c tests/function.c tests/string_pointer.c \
tests/m34_control_flow.c tests/m34_expression_semantics.c \
tests/m34_typedef_array_decay.c tests/m34_local_partial_init.c \
tests/m34_knr_params.c tests/m34_const_correct.c tests/m34_complex_storage.c \
tests/m34_vla_loop.c tests/m34_hideset_recursion.c tests/m34_va_copy.c \
tests/m34_weak_symbol.c tests/m34_headers.c tests/m34_runtime.c \
tests/puts.c tests/m10_switch.c tests/m11_globals.c tests/m18_aggregates.c"

pass=0; fail=0; skip=0
for src in $CORPUS; do
  name=$(basename "$src" .c)
  # Some gates need argv/env; give them here so both compilers see the same run.
  args=""; env_pfx=""
  case "$name" in
    m34_runtime) args="one two"; env_pfx="M34VAR=ok" ;;
  esac

  if ! "$REF" "$src" -o "$tmp/${name}_ref" >/dev/null 2>&1; then
    echo "skip  $name (reference build failed)"; skip=$((skip+1)); continue
  fi
  env $env_pfx "$tmp/${name}_ref" $args >"$tmp/${name}_ref.out" 2>/dev/null; ref_rc=$?
  if ! "$B1CC" "$src" -o "$tmp/${name}_b1" >/dev/null 2>&1; then
    echo "FAIL  $name (b1cc build failed, reference built)"; fail=$((fail+1)); continue
  fi
  env $env_pfx "$tmp/${name}_b1" $args >"$tmp/${name}_b1.out" 2>/dev/null; b1_rc=$?

  if [ "$ref_rc" != "$b1_rc" ]; then
    echo "FAIL  $name: exit b1cc=$b1_rc ref=$ref_rc"; fail=$((fail+1)); continue
  fi
  if ! cmp -s "$tmp/${name}_ref.out" "$tmp/${name}_b1.out"; then
    echo "FAIL  $name: stdout differs"; fail=$((fail+1)); continue
  fi
  pass=$((pass+1))
done

echo "=== M34 differential (exit + stdout): $pass passed, $fail failed, $skip skipped ==="

# Negative diagnostics: compare expected-success/failure behavior separately
# from runtime output.  Reference wording is compiler-specific, so the stable
# contract here is non-zero status plus b1cc's documented diagnostic category.
NEGATIVE="undeclared_assignment:constraint non_lvalue_increment:constraint invalid_shift:constant-expression syntax_error:syntax"
for item in $NEGATIVE; do
  name=${item%%:*}; category=${item#*:}; src="tests/negative/$name.c"
  ref_err="$tmp/${name}_negative_ref.err"; b1_err="$tmp/${name}_negative_b1.err"
  if "$REF" "$src" -o "$tmp/${name}_negative_ref" >/dev/null 2>"$ref_err"; then
    echo "FAIL  $name: reference accepted invalid program"; fail=$((fail+1)); continue
  fi
  if "$B1CC" "$src" -o "$tmp/${name}_negative_b1" >/dev/null 2>"$b1_err"; then
    echo "FAIL  $name: b1cc accepted invalid program"; fail=$((fail+1)); continue
  fi
  if ! grep -Eq "error: \\[$category\\]" "$b1_err" || [ ! -s "$ref_err" ]; then
    echo "FAIL  $name: diagnostic category/reference stderr missing"; fail=$((fail+1)); continue
  fi
  echo "ok   diagnostic $name [$category]"
  pass=$((pass+1))
done

# Object differential surface.  Instruction bytes are compiler-specific, but
# the canonical target-independent surface must agree: .text exists, main is
# a global function in .text, and non-debug relocations have the same types.
# In addition, b1cc's actual .text bytes must be reproducible across builds.
if command -v objdump >/dev/null 2>&1 && "$REF" --version >/dev/null 2>&1; then
  obj_b1="$tmp/object_b1.o"; obj_b1_repeat="$tmp/object_b1_repeat.o"; obj_ref="$tmp/object_ref.o"
  if "$B1CC" --target=x86_64-b1nix -c tests/return_42.c -o "$obj_b1" >/dev/null 2>&1 \
      && "$B1CC" --target=x86_64-b1nix -c tests/return_42.c -o "$obj_b1_repeat" >/dev/null 2>&1 \
      && "$REF" --target=x86_64-unknown-elf -ffreestanding -c tests/return_42.c -o "$obj_ref" >/dev/null 2>&1; then
    objdump -s -j .text "$obj_b1" | sed -n '/^[[:space:]]*[0-9a-f]/p' >"$tmp/object_b1.bytes"
    objdump -s -j .text "$obj_b1_repeat" | sed -n '/^[[:space:]]*[0-9a-f]/p' >"$tmp/object_b1_repeat.bytes"
    # Code size is intentionally not compared: different valid instruction
    # selection is expected.  Section identity is the stable representable
    # layout contract available from the portable objdump fallback.
    objdump -h "$obj_b1" | awk '$2 == ".text" { print $2 }' >"$tmp/object_b1.sections"
    objdump -h "$obj_ref" | awk '$2 == ".text" { print $2 }' >"$tmp/object_ref.sections"
    objdump -t "$obj_b1" | awk '$NF == "main" { print "main", $4, $6 }' >"$tmp/object_b1.symbols"
    objdump -t "$obj_ref" | awk '$NF == "main" { print "main", $4, $6 }' >"$tmp/object_ref.symbols"
    objdump -r "$obj_b1" | awk '/RELOCATION RECORDS FOR/ { debug = ($0 ~ /debug/); next } /^[[:space:]]*[0-9a-f].*R_/ && !debug { print $3 }' | sort >"$tmp/object_b1.relocs"
    objdump -r "$obj_ref" | awk '/RELOCATION RECORDS FOR/ { debug = ($0 ~ /debug/); next } /^[[:space:]]*[0-9a-f].*R_/ && !debug { print $3 }' | sort >"$tmp/object_ref.relocs"
    if ! cmp -s "$tmp/object_b1.bytes" "$tmp/object_b1_repeat.bytes"; then
      echo "FAIL  object bytes are not reproducible"; fail=$((fail+1))
    elif ! cmp -s "$tmp/object_b1.sections" "$tmp/object_ref.sections" \
        || ! cmp -s "$tmp/object_b1.symbols" "$tmp/object_ref.symbols" \
        || ! cmp -s "$tmp/object_b1.relocs" "$tmp/object_ref.relocs"; then
      echo "FAIL  normalized object surface differs"; fail=$((fail+1))
    else
      echo "ok   normalized object surface (bytes reproducible, sections/symbols/relocs)"
      pass=$((pass+1))
    fi
  else
    echo "skip  normalized object surface (ELF reference build unavailable)"; skip=$((skip+1))
  fi
else
  echo "skip  normalized object surface (objdump/reference unavailable)"; skip=$((skip+1))
fi

echo "=== M34 differential (runtime + diagnostics + objects): $pass passed, $fail failed, $skip skipped ==="
[ "$fail" -eq 0 ]

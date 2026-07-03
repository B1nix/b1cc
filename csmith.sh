#!/bin/sh
set -eu

# b1cc × csmith differential test harness
# Usage: ./csmith.sh [count] [seed_offset]

count="${1:-100}"
offset="${2:-0}"
tmp="${TMPDIR:-/tmp}/b1cc-csmith"
rm -rf "$tmp"
mkdir -p "$tmp"

CSMITH_INC="${CSMITH_PATH:-/opt/homebrew/Cellar/csmith/2.3.0/include/csmith-2.3.0}"
CSMITH_FLAGS="\
  --no-longlong --no-float --no-consts --no-volatiles \
  --no-jumps --no-bitfields --no-packed-struct --no-builtins \
  --no-divs \
  --no-comma-operators \
  --max-funcs 2 --max-block-depth 2 --max-block-size 2 \
  --max-expr-complexity 4 --max-array-dim 1 --max-array-len-per-dim 3 \
  --max-pointer-depth 1 --max-struct-fields 2 --max-union-fields 1 \
"

make build/b1cc 2>/dev/null

pass=0
fail=0
noc=0
total=0

for i in $(seq 1 "$count"); do
  seed=$((offset + i))
  src="$tmp/t${seed}.c"
  ref_bin="$tmp/t${seed}_ref"
  b1cc_bin="$tmp/t${seed}_b1"
  b1cc_asm="$tmp/t${seed}_b1.s"

  csmith $CSMITH_FLAGS --seed "$seed" -o "$src" 2>/dev/null

  # Reference: compile with host cc
  if ! cc -I"$CSMITH_INC" "$src" -o "$ref_bin" 2>/dev/null; then
    echo "FAIL seed=$seed: reference cc failed to compile"
    noc=$((noc + 1))
    continue
  fi
  ref_out=$("$ref_bin" 2>/dev/null) || true
  ref_checksum=$(echo "$ref_out" | grep checksum)

  # b1cc: compile to assembly
  if ! timeout 10 ./build/b1cc -I"$CSMITH_INC" "$src" -S -o "$b1cc_asm" 2>/dev/null; then
    echo "NOC seed=$seed: b1cc cannot compile"
    noc=$((noc + 1))
    continue
  fi

  # Assemble with host cc
  if ! cc "$b1cc_asm" -o "$b1cc_bin" 2>/dev/null; then
    echo "FAIL seed=$seed: b1cc asm failed to assemble"
    fail=$((fail + 1))
    continue
  fi
  b1cc_out=$("$b1cc_bin" 2>/dev/null) || true
  b1cc_checksum=$(echo "$b1cc_out" | grep checksum)

  total=$((total + 1))
  if [ "$ref_checksum" = "$b1cc_checksum" ]; then
    echo "PASS seed=$seed $ref_checksum"
    pass=$((pass + 1))
  else
    echo "FAIL seed=$seed ref=$ref_checksum b1cc=$b1cc_checksum"
    fail=$((fail + 1))
  fi
done

echo "---"
echo "total tested: $total  pass: $pass  fail: $fail  skipped (noc): $noc"
exit $fail

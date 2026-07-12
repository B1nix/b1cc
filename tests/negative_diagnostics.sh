#!/bin/sh
set -eu

tmp="${TMPDIR:-/tmp}/b1cc-negative-diagnostics"
rm -rf "$tmp"
mkdir -p "$tmp"

run_case() {
  name="$1"
  needle="$2"
  set +e
  ./build/b1cc -S "tests/negative/$name.c" -o "$tmp/$name.s" 2> "$tmp/$name.err"
  rc=$?
  set -e
  test "$rc" -ne 0
  grep -Eq '^tests/negative/'"$name"'\.c:[0-9]+:[0-9]+: error: \[[a-z-]+\] ' "$tmp/$name.err"
  grep -q "$needle" "$tmp/$name.err"
  test "$(grep -Ec '^tests/negative/'"$name"'\.c:[0-9]+:[0-9]+: error: \[[a-z-]+\] ' "$tmp/$name.err")" -eq 1
  echo "ok m34_negative_$name"
}

run_case undeclared_assignment "undeclared variable"
run_case non_lvalue_increment "lvalue required"
run_case constant_divzero "division by zero"
run_case const_assignment "const-qualified"
run_case syntax_error "expected ';'"
run_case invalid_preprocessor "unmatched #else"
run_case preprocessor_error "deliberate preprocessing failure"
run_case line_marker "unexpected character"
grep -Eq '^tests/negative/line_marker\.c:77:[0-9]+: error: ' "$tmp/line_marker.err"
run_case m34_void_return_value "return with a value in void function"
run_case m34_nonvoid_return_empty "return with no value in non-void function"
run_case m34_incompatible_global "incompatible redeclaration"
run_case m34_incompatible_pointer "incompatible pointer assignment"
run_case m34_nonzero_pointer "not a null pointer constant"
run_case m34_incompatible_function "incompatible function redeclaration"
run_case m34_incompatible_parameters "incompatible function parameter list"
run_case m34_const_discard_init "discards 'const'"
run_case m34_const_discard_assign "discards 'const'"

run_target_case() {
  target="$1"
  name="$2"
  needle="$3"
  set +e
  ./build/b1cc --target="$target" -S "tests/negative/$name.c" -o "$tmp/$target-$name.s" 2> "$tmp/$target-$name.err"
  rc=$?
  set -e
  test "$rc" -ne 0
  grep -Eq '^tests/negative/'"$name"'\.c:[0-9]+:[0-9]+: error: \[[a-z-]+\] ' "$tmp/$target-$name.err"
  grep -q "$needle" "$tmp/$target-$name.err"
  echo "ok m34_negative_${target}_${name}"
}

run_target_case arm64-darwin m34_incompatible_global "incompatible redeclaration"
run_target_case x86_64-b1nix m34_incompatible_global "incompatible redeclaration"
run_case m34_break_outside "break statement not within loop or switch"
run_case m34_continue_outside "continue statement not within loop"
run_case m34_duplicate_label "duplicate label"
run_case invalid_character "unexpected character"
run_case unterminated_comment "unterminated block comment"
run_case unknown_struct_initializer "unknown struct tag"
run_case invalid_shift "invalid shift count"
run_case invalid_initializer "too many initializers"
run_case constant_overflow "signed overflow in constant expression"
run_case missing_closing_brace "expected '}' but got 'EOF'"
run_case missing_if_paren "expected '(' but got '1'"
run_case trailing_operator "expected ';' but got '}'"
run_case unmatched_paren "expected ')' but got ';'"
run_case missing_array_bracket "expected ']' but got 'return'"
run_case incomplete_initializer "expected ';' but got '0'"
run_case missing_do_while "expected 'while' but got '}'"
run_case unterminated_string "unterminated string literal"
run_case unterminated_char "unterminated character literal"

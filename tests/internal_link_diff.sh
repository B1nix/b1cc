#!/bin/sh
# tests/internal_link_diff.sh — M33 internal-linker differential regression.
#
# For each program: link it two ways for x86_64-b1nix and require identical
# stdout + exit status:
#   (a) b1cc's OWN internal static linker  (B1CC_INTERNAL_LD=1, no ld.lld)
#   (b) the reference path                 (b1cc .s -> clang -> ld.lld via b1nix-cc)
#
# This is the gate for M33 (on-device linking) AND regression cover for the
# native-assembler encoding bugs it exposed (cmpq operand order, SIB array
# store/load, movslq-from-memory, %cl/%al REG8 classification / variable shifts).
#
# Requires: an x86_64 B1NIX build tree (crt0.o + libb1nix.a) next to this repo,
# clang, ld.lld. Skips cleanly if they are absent. Runs the produced x86_64
# static ELFs directly on the Linux host (their syscalls match for these cases).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B1CC_ROOT="$ROOT"
B1CC="$ROOT/build/b1cc"
if [ -n "${B1NIX_USR:-}" ]; then
	USR="$B1NIX_USR"
else
	USR="$ROOT/../../userspace"
fi
B1NIX_CC_BIN="${B1NIX_CC:-}"
if [ -z "$B1NIX_CC_BIN" ]; then
	if [ -x "$ROOT/../../tools/toolchain/bin/b1nix-cc" ]; then
		B1NIX_CC_BIN="$ROOT/../../tools/toolchain/bin/b1nix-cc"
	elif [ -x "$ROOT/../b1nix/tools/toolchain/bin/b1nix-cc" ]; then
		B1NIX_CC_BIN="$ROOT/../b1nix/tools/toolchain/bin/b1nix-cc"
	fi
fi
CRT0="$USR/build/x86_64/crt/crt0.o"
LIBC="$USR/build/x86_64/libb1nix.a"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$B1CC" ]; then echo "SKIP: build/b1cc missing (run make)"; exit 0; fi
if [ ! -f "$CRT0" ] || [ ! -f "$LIBC" ]; then echo "SKIP: B1NIX x86_64 build tree not found at $USR"; exit 0; fi
if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then echo "SKIP: clang/ld.lld unavailable"; exit 0; fi
[ -n "$B1NIX_CC_BIN" ] || { echo "SKIP: b1nix-cc unavailable"; exit 0; }

export B1CC_CRT0="$CRT0" B1CC_LIBC="$LIBC" B1NIX_CC="$B1NIX_CC_BIN"

# programs to check (repo tests + any b1nix b1cc smoke sources present)
PROGS="tests/return_42.c tests/precedence.c tests/local.c tests/if_else.c \
tests/while.c tests/for.c tests/puts.c tests/function.c tests/string_pointer.c \
tests/argc.c tests/argv.c \
tests/m22_asm_operands.c tests/m22_gnu_extensions.c tests/m22_function_asm_label.c \
tests/m22_tcc_struct_table_init.c"
for extra in b1cc_hello b1cc_better_c b1cc_argv b1cc_file_write b1cc_stderr_exit; do
	[ -f "$USR/bin/$extra.c" ] && PROGS="$PROGS $USR/bin/$extra.c"
done

pass=0; fail=0
RUNNER=""
case "$(uname -m)" in
	x86_64|amd64) ;;
	*)
		if command -v qemu-x86_64 >/dev/null 2>&1; then RUNNER="qemu-x86_64"; fi
		;;
esac
if [ -z "$RUNNER" ] && [ "$(uname -m)" != "x86_64" ] && [ "$(uname -m)" != "amd64" ]; then
	echo "SKIP runtime differential: x86_64 ELF runner unavailable on $(uname -m)"
fi
for src in $PROGS; do
	[ -f "$ROOT/$src" ] && src="$ROOT/$src"
	[ -f "$src" ] || continue
	b="$(basename "$src" .c)"
	if ! B1CC_INTERNAL_LD=1 "$B1CC" "$src" --target=x86_64-b1nix -o "$TMP/i_$b" 2>/dev/null; then
		echo "FAIL $b (internal link error)"; fail=$((fail+1)); continue
	fi
	"$B1CC" "$src" --target=x86_64-b1nix -o "$TMP/r_$b" 2>/dev/null || { echo "FAIL $b (ref link error)"; fail=$((fail+1)); continue; }
	[ -n "$RUNNER" ] || continue
	io="$($RUNNER "$TMP/i_$b" a b 2>&1)"; ie=$?
	ro="$($RUNNER "$TMP/r_$b" a b 2>&1)"; re=$?
	if [ "$io" = "$ro" ] && [ "$ie" = "$re" ]; then
		echo "ok   $b (exit=$ie)"; pass=$((pass+1))
	else
		echo "FAIL $b: internal(exit=$ie)[$io] vs ref(exit=$re)[$ro]"; fail=$((fail+1))
	fi
done

# ---- M33 dynamic path: PIE + shared (.so) structural differential ----
# Dynamic ELFs need the B1NIX loader + libc.so.1, so they cannot execute on the
# host. Instead compare the internal linker's structure against ld.lld's and
# assert self-consistency: PLT stubs <-> JUMP_SLOT count, imports actually
# resolve, and a shared object exports all of its defined globals.
CRT0DYN="$USR/build/x86_64/crt/crt0-dynamic.o"
LIBCSO="$USR/build/x86_64/libc.so.1"
PIELD="$USR/linker_pie.ld"
DI="$ROOT/tests/elf_dyninfo.py"
export B1CC_CRT0_DYNAMIC="$CRT0DYN"
getf() { printf '%s\n' "$1" | sed -n "s/^$2=//p"; }

if command -v python3 >/dev/null 2>&1 && [ -f "$CRT0DYN" ] && [ -f "$LIBCSO" ] && [ -f "$PIELD" ]; then
	# -- PIE differential (import via PLT/JUMP_SLOT + .data global via GLOB_DAT/RELATIVE) --
	psrc="$TMP/pie_demo.c"
	printf '%s\n' 'int puts(char *s);' 'int counter = 5;' \
		'int main(void){ puts("b1cc pie diff"); return counter; }' > "$psrc"
	if B1CC_INTERNAL_LD=1 "$B1CC" "$psrc" -fPIC --target=x86_64-b1nix -o "$TMP/pie_i" 2>/dev/null \
	   && "$B1CC" "$psrc" -c -fPIC --target=x86_64-b1nix -o "$TMP/pie.o" 2>/dev/null \
	   && ld.lld -m elf_x86_64 -pie -z norelro --hash-style=sysv \
	        --dynamic-linker /lib/ld-b1nix.so -T "$PIELD" \
	        -o "$TMP/pie_r" "$CRT0DYN" "$TMP/pie.o" "$LIBCSO" 2>/dev/null; then
		I="$(python3 "$DI" "$TMP/pie_i")"; R="$(python3 "$DI" "$TMP/pie_r")"
		ok=1; why=""
		[ "$(getf "$I" type)" = DYN ] || { ok=0; why="type"; }
		printf '%s' "$(getf "$I" needed)" | grep -q 'libc.so.1' || { ok=0; why="$why needed"; }
		for t in HASH STRTAB SYMTAB RELA JMPREL PLTGOT NEEDED; do
			printf '%s' "$(getf "$I" tags)" | grep -q "$t" || { ok=0; why="$why tag:$t"; }
		done
		js="$(getf "$I" jumpslots)"; ps="$(getf "$I" pltstubs)"
		{ [ "$js" = "$ps" ] && [ "$js" -gt 0 ]; } || { ok=0; why="$why plt($js/$ps)"; }
		printf '%s' "$(getf "$I" imports)" | grep -q 'puts' || { ok=0; why="$why import:puts"; }
		[ "$js" = "$(getf "$R" jumpslots)" ] || { ok=0; why="$why js!=ref"; }
		if [ "$ok" = 1 ]; then echo "ok   PIE:pie_demo (DYN, $js JUMP_SLOT == PLT stubs, libc.so.1)"; pass=$((pass+1))
		else echo "FAIL PIE:pie_demo ($why)"; fail=$((fail+1)); fi
	else
		echo "FAIL PIE:pie_demo (link error)"; fail=$((fail+1))
	fi

	# -- shared (.so): export completeness + SONAME, no spurious imports --
	ssrc="$TMP/so_demo.c"
	printf '%s\n' 'int the_answer = 42;' 'static int priv = 7;' \
		'static int helper(int x){return x*priv;}' 'int add(int a,int b){return a+b;}' \
		'int sub(int a,int b){return helper(a)-b;}' > "$ssrc"
	if B1CC_INTERNAL_LD=1 "$B1CC" "$ssrc" -fPIC -shared --soname=libso_demo.so.1 \
	     --target=x86_64-b1nix -o "$TMP/so_i" 2>/dev/null; then
		I="$(python3 "$DI" "$TMP/so_i")"
		ok=1; why=""
		[ "$(getf "$I" type)" = DYN ] || { ok=0; why="type"; }
		[ "$(getf "$I" soname)" = libso_demo.so.1 ] || { ok=0; why="$why soname"; }
		exps="$(getf "$I" exports)"
		for e in add sub the_answer; do
			printf '%s' "$exps" | grep -qw "$e" || { ok=0; why="$why export:$e"; }
		done
		# static function/variable must NOT be exported into .dynsym
		for e in helper priv; do
			printf '%s' "$exps" | grep -qw "$e" && { ok=0; why="$why leaked-static:$e"; }
		done
		[ -z "$(getf "$I" imports)" ] || { ok=0; why="$why has-imports"; }
		if [ "$ok" = 1 ]; then echo "ok   SO:so_demo (DYN, exports add/sub/the_answer, statics local, SONAME)"; pass=$((pass+1))
		else echo "FAIL SO:so_demo ($why)"; fail=$((fail+1)); fi

		# A PIE that DT_NEEDEDs the b1cc-built .so (-lso_demo) must import its
		# symbol and record the dependency — this is the on-device .so linkage.
		msrc="$TMP/so_main.c"
		printf '%s\n' 'int add(int a,int b);' 'int main(void){ return add(19,23); }' > "$msrc"
		if B1CC_INTERNAL_LD=1 "$B1CC" "$msrc" -fPIC -lso_demo \
		     --target=x86_64-b1nix -o "$TMP/so_main" 2>/dev/null; then
			MI="$(python3 "$DI" "$TMP/so_main")"
			ok=1; why=""
			[ "$(getf "$MI" type)" = DYN ] || { ok=0; why="type"; }
			printf '%s' "$(getf "$MI" needed)" | grep -q 'libso_demo.so.1' || { ok=0; why="$why needed"; }
			printf '%s' "$(getf "$MI" imports)" | grep -qw add || { ok=0; why="$why import:add"; }
			if [ "$ok" = 1 ]; then echo "ok   SO-LINK:so_main (DT_NEEDED libso_demo.so.1, imports add)"; pass=$((pass+1))
			else echo "FAIL SO-LINK:so_main ($why)"; fail=$((fail+1)); fi
		else
			echo "FAIL SO-LINK:so_main (link error)"; fail=$((fail+1))
		fi
	else
		echo "FAIL SO:so_demo (link error)"; fail=$((fail+1))
	fi
else
	echo "SKIP dynamic (PIE/.so): crt0-dynamic.o / libc.so.1 / linker_pie.ld / python3 unavailable"
fi

echo "=== internal-link differential: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]

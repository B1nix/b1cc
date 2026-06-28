# b1cc

Tiny clean-room C-subset compiler experiment for B1NIX.

See [ROADMAP.md](ROADMAP.md) for the staged plan.

Current scope:

- simple C functions
- integer literals
- `+`, `-`, `*`, `/`, parentheses
- comparisons, `if`, `while`, `for`
- local/global/static variables, assignment, and initializers
- fixed-argument function calls and function pointers
- string literals, arrays, structs, unions, enums, typedefs, and casts
- arrays inside structs and integer/pointer `.`/`->` field access for the covered aggregate subset
- integer-only scalar typing foundation: `_Bool`, tolerated `const`/`volatile`/`restrict`, and usual integer promotions/conversions for current integer types
- pointer indexing and scale-aware pointer arithmetic
- basic preprocessor support: includes, conditionals, object/function-like macros, `#`, and `##`
- Darwin ARM64 assembly output on Apple Silicon
- x86_64 B1NIX assembly output, plus ELF linking through `b1nix-cc`
- i386 B1NIX assembly output, plus ELF linking through `b1nix-cc`
- C++17 implementation

Run:

```sh
./test.sh
```

Compile manually:

```sh
make
./build/b1cc tests/return_42.c -S -o /tmp/return_42.s
cc /tmp/return_42.s -o /tmp/return_42
/tmp/return_42
echo $?
```

Still missing: full C, native object/ELF writer, full C99 preprocessing edge cases, floating point, bitfields, callee-side varargs, full qualifier semantics, full target ABI aggregate classification, and a clean-room C self-host path. Add each only when a real B1NIX smoke needs it.

# b1cc

Tiny clean-room C-subset compiler experiment for B1NIX.

See [ROADMAP.md](ROADMAP.md) for the staged plan.

Current scope:

- simple C functions
- integer literals
- `+`, `-`, `*`, `/`, parentheses
- comparisons, `if`, `while`, `for`
- local variables, assignment, local initializers
- fixed-argument function calls
- string literals and word-sized pointer indexing
- Darwin ARM64 assembly output on Apple Silicon
- x86_64 B1NIX assembly output, plus ELF linking through `b1nix-cc`
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

Skipped: full C, direct ELF writer, i386, real preprocessor. Add each only when a real B1NIX smoke needs it.

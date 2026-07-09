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
- integer-only scalar typing foundation: C11 `_Bool`, C23 `bool`/`true`/`false`, tolerated `const`/`volatile`/`restrict`, and usual integer promotions/conversions for current integer types
- pointer indexing and scale-aware pointer arithmetic
- basic preprocessor support: includes, conditionals, object/function-like macros, `#`, and `##`
- Darwin ARM64 assembly output on Apple Silicon
- x86_64 B1NIX assembly output, plus ELF linking through `b1nix-cc`
- i386 B1NIX assembly output, plus ELF linking through `b1nix-cc`
- C23 implementation using standard C11 facilities where useful

## Toolchain boundary

b1cc is a **compiler and assembler, not a linker**. It lowers C to assembly and, for
ELF/Mach-O targets, encodes native relocatable objects directly (`src/elf_writer.c`,
`src/macho_writer.c`); it also assembles `.S`/`.s` inputs. It does **not** provide CRT0
startup code or linker scripts — those live in the B1NIX tree
(`../b1nix/userspace/crt/crt0.S`, `../b1nix/userspace/linker.ld`,
`.../linker_shared.ld`). Final linking is delegated:

- `x86_64-b1nix` / `i386-b1nix`: static executables through `b1nix-cc` (which runs
  `ld.lld` with the B1NIX linker script + CRT0 + libc); `-shared` links a `.so` with
  `ld.lld` directly (override with `$B1NIX_LD`).
- `arm64-darwin`: host `cc` links (and handles PIE/stubs).

## Code models

- `-mcmodel=small` (default): RIP-relative addressing (`sym(%rip)`), for position-
  independent / low-address code.
- `-mcmodel=kernel`: absolute addressing via `movabs`, for code the linker script
  places in the top negative-2 GiB kernel half where RIP-relative displacements do
  not reach. b1cc only selects the addressing form; the linker script (in the
  B1NIX/kernel tree) is responsible for placing sections at the matching addresses.

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

Still missing: full C, native Mach-O object writer for arm64-darwin, full C99 preprocessing edge cases, floating-point aggregate ABI classification, callee-side varargs, `long double`, complete aggregate assignment, and a complete C self-host roundtrip. Current self-hosting is partial: `build/b1cc_self` can be produced and can compile tiny smoke programs, but it is not yet the milestone closure.

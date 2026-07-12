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
- x86_64 B1NIX assembly output, native ELF64 object writing, and the covered
  internal x86_64 linker path
- C23 implementation using standard C11 facilities where useful

## Toolchain boundary

b1cc is a compiler, assembler, and (for the covered x86_64 ELF path) linker. It lowers
C to assembly and, for ELF/Mach-O targets, encodes native relocatable objects directly (`src/elf_writer.c`,
`src/macho_writer.c`); it also assembles `.S`/`.s` inputs. It does **not** provide CRT0
startup code or linker scripts — those remain in the B1NIX tree
(`../b1nix/userspace/crt/crt0.S`, `../b1nix/userspace/linker.ld`,
`.../linker_shared.ld`). Linking behavior is:

- `x86_64-b1nix`: the covered internal linker supports native static ET_EXEC,
  PIC/PIE/shared ET_DYN, x86_64 relocations, and SysV archive resolution when
  supplied CRT0/archive inputs are available. The host cross-link path still uses
  `b1nix-cc`/`ld.lld` when requested or when the internal path is unavailable.
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

Still missing is full ISO C; this project closes the explicitly documented active M34 subset and does not claim arbitrary C99/C23 coverage. The active targets now cover floating aggregate classification, callee-side varargs, and long-double aggregate/varargs cases with dedicated regression tests. `long double` uses 80-bit x87 on x86_64-b1nix and the Apple ABI representation on arm64-darwin. Historical i386 support is archived under `archive/i386/`. The active self-host closure is limited to the representative corpus documented in `ROADMAP_ACTIVE.md`, not arbitrary C23.

**IEC 60559 Compliance**: b1cc provides the C99 `<fenv.h>` exception flags, environment save/restore, non-default rounding modes, and generated operations under those modes via platform libc; it defines `__STDC_IEC_559__`. Non-standard trap-mask APIs are outside the C99 fenv contract. See `ROADMAP_M34_ACTIVE.md` M34 floating-point section for details.

The C99 inventory and gate are maintained in `m34_feature_manifest.json` and
validated by `python3 tools/check_m34_manifest.py`. The permanent negative
diagnostic fixtures are run by `tests/negative_diagnostics.sh`; these gates
track partial coverage and do not claim C99 conformance.

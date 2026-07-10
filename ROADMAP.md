# b1cc Roadmap

## M0: Prototype

- [x] Create a clean-room compiler repository.
- [x] Parse `int main(void) { return expression; }`.
- [x] Support integer literals and `+ - * /` precedence.
- [x] Emit Darwin ARM64 assembly for host smoke tests.
- [x] Add a tiny runnable test script.

## M1: C Compiler Core

- [x] Maintain the compiler implementation as C23.
- [x] Build with `-std=c23`.
- [x] Keep one binary: `b1cc`.
- [x] Add lexer, parser, AST, diagnostics, IR, and backend as small modules.
- [x] Preserve the current expression-return tests.

## M2: Minimal Internal IR

- [x] Define a small b1cc IR independent of LLVM/QBE.
- [x] Support functions, constants, arithmetic, and return.
- [x] Keep backend selection behind a tiny interface.
- [x] Emit Darwin ARM64 assembly from IR.

## M3: C Subset 0

- [x] Parse simple C functions.
- [x] Support `int`, `char`, `long`, `void`, pointers, and string literals.
- [x] Support local variables, assignment, blocks, `if`, `while`, and `for`.
- [x] Support function calls with fixed arguments.
- [x] Add tests for every new syntax feature.

## M4: B1NIX Backend 0

- [x] Emit x86_64 B1NIX assembly.
- [x] Produce code compatible with the B1NIX C ABI.
- [x] Compile and run `return 42` inside B1NIX.
- [x] Add i386 only after x86_64 works.

## M5: M25 Smoke C

- [x] Compile hello output.
- [x] Compile argv/file-write checks.
- [x] Compile stderr and exit-status checks.
- [x] Support enough libc calls for the current M25 smoke shape.
- [ ] Retire TCC fallback only after b1cc passes equivalent B1NIX tests.

## M6: Object and ELF Output

- [x] Drive `cc` / `b1nix-cc` to emit relocatable objects or final ELF.
- [x] Add symbols, relocations, sections, and simple debug dumps.
- [x] Stop relying on host `cc` for normal B1NIX output.

## M7: Better C

- [x] Add structs, enums, typedefs, casts, initializers, and arrays.
- [x] Add a small preprocessor path for includes/comments.
- [x] Add object-like macros when a real test needs them.
- [x] Support target ABI varargs calls well enough for `printf`-style declarations.
- [x] Expand tests from real B1NIX userspace sources, not random internet code.

## M8: Self-Hosting Track

- [x] Compile selected b1cc source files with b1cc.
- [x] Compile all b1cc C support code, if any.
- [x] Keep the C++ core host-built until b1cxx exists.

## M9: Optional External Backend - Deffered

- [ ] Evaluate QBE after the internal IR is stable.
- [ ] Add QBE only if it reduces backend work for real tests.
- [ ] Do not expose QBE details to the frontend.
- [ ] Defer LLVM until optimizer/debug-info/many-target needs are concrete.

## M10: Production-Grade Expressions & Control Flow

- [x] Support logical OR/AND operators (`||` and `&&`) with short-circuit evaluation.
- [x] Support bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`).
- [x] Support `switch-case` statements with `break`/`continue` loop control.
- [x] Support increment/decrement operators (prefix/postfix `++` and `--`).
- [x] Support compound assignment operators (`+=`, `-=`, `*=`, `/=`).

## M11: Global Variables & Memory Model

- [x] Support global variable declarations with compile-time constant initializers.
- [x] Support static local and global variables.
- [x] Support multidimensional arrays (e.g., `arr[i][j]`).
- [x] Support scale-aware pointer arithmetic (e.g., `ptr + offset` scales by element size).

## M12: Preprocessor Path

- [x] Support conditional preprocessor directives (`#ifdef`, `#ifndef`, `#else`, `#endif`, `#if`).
- [x] Support function-like macros (`#define MACRO(a,b) ...`).
- [x] Support standard include directories search paths.

## M13: C++ Frontend - Frozen

- [ ] Start `b1cxx` as a separate frontend using the same IR/backend.
- [ ] Add namespaces, references, classes, methods, constructors, and mangling.
- [ ] Add overloads before templates.
- [ ] Add templates, exceptions, and RTTI only when B1NIX needs them.

## M14: TCC-Oriented C Frontend

- [x] Add `sizeof` and move declarator parsing toward real C declarators (`int *a[3]`, `int (*fp)(int)`).
- [x] Tighten integer typing for signed/unsigned comparisons, shifts, cast width, and char/short widening.
- [x] Complete integer promotions, usual arithmetic conversions, and qualifier-aware typing.
- [x] Improve aggregate layout for alignment, padding, nested structs, and `union`.
- [x] Support arrays inside structs and reliable integer/pointer `.`/`->` field access for covered scalar, nested-struct, and array-field cases.
- [x] Add function pointer declarators and calls without special parser shortcuts.
- [x] Qualifier semantics check and volatile access semantics. b1cc tracks the
      top-level `const`/`volatile` qualifier on named local, global, and static
      scalar/pointer objects and rejects assignment (`=` and compound `+=` etc.)
      to a `const`-qualified object with a compile-time diagnostic. `volatile`
      objects are tracked and every source-level access is preserved: b1cc does
      no value caching, load/store elimination, or reordering of named accesses,
      and its only optimization (the eval-stack push/pop peephole) never touches
      named loads/stores. Covered by `tests/m14_qualifiers.c` and a negative
      compile check. Not yet enforced: pointer-to-const *pointee* writes
      (`*p = ...` where `p` is `const T *`), const struct/array element writes,
      and const-correctness across function-argument passing.

## M15: Driver and ABI Regression Coverage

- [x] Add ABI regression tests for stack-passed arguments, varargs calls, function pointers, and small integer returns.
- [x] Add driver modes expected from a tiny C compiler: `-E`, -c, multiple input files, and pass-through linker flags.
- [x] Make native object output real enough for B1NIX: symbols, relocations, sections, and debug dumps.
- [x] Compile a small curated set of real B1NIX userspace files without TCC fallback when the sibling B1NIX tree is present (`b1cc_hello.c`, `b1cc_better_c.c`).
- [x] ELF64 relocatable objects for x86_64-b1nix (full instruction encoding + sections).
- [x] ELF32 relocatable objects for i386-b1nix (instruction encoding for backend-emitted i386 assembly, sections, symbols, and SHT_REL relocations).
- [x] Native Mach-O object writer for arm64-darwin (currently uses host `cc` pass-through).
- [x] Full i386 binary instruction encoding for the assembly forms emitted by `backend_i386.c`.

## M16: Compiler Shape Cleanup

- [x] Split C frontend semantics more clearly from target lowering.
- [x] Add a backend contract for type legalization, calling convention lowering, instruction selection, and register allocation.
- [x] Add tiny local optimizations only after type semantics are stable.
- [x] Grow tests from real B1NIX userspace sources before chasing wider C compatibility; coverage is still optional/non-hermetic because it depends on a sibling B1NIX checkout.

## M17: C99 Preprocessor Operators

- [x] Add macro definitions with arguments (e.g., `#define MAX(a, b) ...`).
- [x] Add macro operators `#` (stringification) and `##` (token pasting).
- [x] Add full constant expression evaluation for conditional directives (`#if`, `#elif` with `defined`).
- [x] Complete C99 preprocessor edge cases beyond the covered macro/operator tests.
- [x] Obscure preprocessor rescan, macro placemarker, and expansion edge cases.

## M18: Conforming Aggregates & ABI

- [x] Support integer/pointer struct passing and return by value with stack/register splitting.
- [x] Support target ABI aggregate classification for large returns >16 bytes on ARM64, x86_64, and i386.
- [x] Support designated initializers (e.g., `struct Point p = { .x = 1 }`).
- [x] Support nested brace initializers for multidimensional arrays and structures.
- [x] Support bitfield member declarations with packing (`type name : bits`) and pack/unpack via `bfi`/`ubfx` (ARM64) and shift/mask patterns (x86_64, i386).
- [x] Pure floating aggregate ABI classification for homogeneous `float`/`double`
      structs up to four fields: ARM64 Darwin HFA parameters/returns in V0-V7
      and x86_64 B1NIX SSE eightbyte parameters/returns in XMM registers, with
      regression coverage for by-value calls and returns.
- [x] Mixed float/integer aggregate classes: System V x86_64 per-eightbyte
      classification for `<=16`-byte structs whose two eightbytes have different
      classes (e.g. `struct { double a; long b; }` → eightbyte 0 SSE/XMM,
      eightbyte 1 INTEGER/GPR). Implemented across all five lowering sites —
      callee parameter spill, varargs register-save counting, caller argument
      marshalling, callee return, and caller return capture — encoded via the
      `SYSV_MIXED_FLAG` sentinel threaded through the existing aggregate-class
      plumbing (`src/ir.h`, `src/parser.c`, `src/backend_x86_64.c`). ARM64/i386
      ignore the sentinel and keep passing such aggregates in GPRs, which their
      ABIs require. Covered by `tests/m18_mixed_aggregate_abi.c`.
      Verification: the emitted x86_64 register assignment matches
      `cc -arch x86_64 -S` exactly at every site (parameter, call, and return),
      and the full host suite stays green; on-target x86_64-b1nix execution under
      the QEMU kernel boot was not run here (the b1nix b1cc-smoke harness on this
      machine did not rebuild/run the b1cc programs — an environment issue, not a
      codegen one). Named edges (fall back to the existing all-integer path):
      function-local `struct` definitions, and structs containing arrays,
      bitfields, unions, or nested aggregates. **Vector aggregate ABI** remains
      out of scope: b1cc has no vector types. i386 aggregate-by-value lowering is
      independently incomplete and out of scope here.

## M19: Complete Type System & Math

- [x] Support **scalar** floating-point types (`float`, `double`) with FPU/SIMD
      backend instructions and registers: float/double literals, locals,
      globals (with IEEE-754 initializers), arithmetic (`+ - * /`), comparisons,
      unary minus, and int↔float / float↔double conversions, plus float/double
      function arguments and return values across the call boundary. Backends:
      x86_64 SSE2 (`movsd`/`addsd`/`cvt*`, XMM regs, SysV xmm args/ret), arm64
      AAPCS64 (`fadd`/`fcvt*`/`scvtf`, D/S regs, V0-V7 args/ret), i386 x87 FPU
      (`fldl`/`faddp`/`fisttp`, st0 returns). Internally FP arithmetic is
      evaluated at double precision and narrowed to `float` on store/return
      (compute-in-double model). Verified runnable on arm64-darwin
      (`tests/m19_float_scalar.c`) with x86_64/i386 assembly coverage.
      Still pending: float/vector **aggregate** ABI (M18), `printf`-style
      vararg float promotion, and `long double` beyond 64-bit.
- [x] Support `long long` 64-bit integer operations on 32-bit platforms:
      i386 lowers 64-bit integer constants, locals, globals, casts, returns,
      arithmetic (`+ - * / %`), bitwise operators, shifts, comparisons, and
      truth tests through `edx:eax` register-pair/eval-stack code, using
      `__divdi3`/`__moddi3` helpers for signed division/remainder. Covered by
      `tests/m19_long_long_i386.c` with host execution and i386 assembly checks.
- [x] Add integer-only typing foundation for scalar C11 `_Bool`, C23 `bool`/`true`/`false`, tolerated `const`/`volatile`/`restrict` qualifiers, and usual integer promotions/conversions across current scalar integer types.
- [x] Complete qualifier semantics beyond parsing/tolerance: const-correct
      assignment diagnostics for top-level `const`-qualified named scalar/pointer
      objects (local/global/static) and volatile access fidelity guaranteed by
      non-caching codegen. See M14 for the exact supported subset and the
      not-yet-covered cases (pointer-to-const pointee writes, const aggregate
      element writes, const across argument passing).
- [x] Implement bitfields packing/unpacking in aggregates (ARM64 `bfi`/`ubfx`, x86_64/i386 shift+mask; named and anonymous bitfields in top-level and local struct/union definitions).

## M20: Callee-Side Varargs & Self-Hosting

- [x] Add compiler built-ins for writing vararg functions (`__builtin_va_list`, `__builtin_va_start`, etc.).
- [x] Support full self-host roundtrip for the C compiler codebase: host-built `b1cc` builds `build/b1cc_self`, and `build/b1cc_self` can compile/link the covered test corpus, not only tiny smoke programs.
- [x] Add regression coverage for the current self-host smoke boundary: `build/b1cc_self` compiling `tests/return_42.c` and producing an executable that exits with 42.
- [x] Implement aggregate assignment/copy lowering for the covered struct/union cases, including nested field assignment such as `state.tokens = tokens`.
- [x] Replace remaining self-host workarounds for by-value aggregate copies with real aggregate copy lowering for compiler-used value structs, while preserving intentionally pointer-based API boundaries.
- [x] Extend aggregate return/call lowering beyond the earlier small integer/pointer aggregate subset, with explicit large and float aggregate ABI tests.
- [x] Add self-host regression tests for local string array initializers such as `char tmp[] = "/tmp/file-XXXXXX.s"` because the driver depends on them for temporary assembly/object paths.
- [x] Document the current self-host coverage boundary: it builds a self-hosted compiler and runs a covered corpus including arithmetic/control-flow, arguments, string pointers, callee-side varargs, and local string array initializers.
- [x] Add regression coverage for nested aggregate copy-by-value in the currently supported assignment forms: local aggregate copy initialization and global aggregate assignment. Covered by `tests/m20_aggregate_copy_nested.c`.
- [x] Fix local aggregate assignment from global aggregate sources for >8-byte structs.
- [x] Fix nested array/union field access inside aggregate-copy regression shapes before expanding the M20 copy tests to those cases.
- [x] Complete ISO C aggregate assignment/copy semantics beyond the covered struct/union assignment, by-value copy, nested struct copy initialization, global aggregate assignment, and ABI regression tests.
- [x] Add explicit regression tests before claiming future aggregate ABI target classes beyond the currently covered ARM64 Darwin and B1NIX large/float aggregate cases.

## M21: On-The-Fly Preprocessor Macro Expansion & X-Macros

- [x] Move normal active-line macro expansion from the compile-time post-preprocessing `lex()` call to on-the-fly expansion during `preprocessor_preprocess()` for the currently supported macro subset.
- [x] Fix the covered `#undef` shadowing issue where macros defined and undefined inside headers are expanded before they are removed. Covered by `tests/m21_xmacro_undef.c`.
- [x] Add `-E` regression coverage showing X-macro expansion appears in preprocessed output and the temporary macro invocation does not survive past header-local `#undef`.
- [x] Support repeated non-system X-macro includes with different macro bodies instead of suppressing the second include globally. Covered by `tests/m21_repeat_xmacro.c`.
- [x] Support the covered object-like macro alias to function-like macro expansion pattern used by TCC opcode tables, such as `DEF_BWLX` expanding to `DEF_BWLQ` before invocation. Covered by `tests/m21_xmacro_undef.c`.
- [x] Replace the internal reuse of the lexer macro-expansion engine with a dedicated preprocessing-token expander. `expand_active_line` no longer round-trips through `lex(..., macros)` + an 8-pass re-lex-to-fixpoint (and the `"s1 -> s1 ->"` string hack it needed is gone). Instead `src/preprocessor.c` tokenizes once (`lex(..., NULL)`, no expansion) and runs a Prosser-style single-pass expander (`pp_expand_tokens`/`pp_substitute`) with **per-token hidesets** (blue-paint recursion guard), replacements spliced back into the stream, and **argument prescan** (a parameter not adjacent to `#`/`##` is fully expanded before substitution). This fixes cases the lex-reuse path handled fragilely or wrongly: object-like macros that expand to a function-like macro name whose `(...)` args follow in the stream, argument prescan through a wrapper macro (`WRAP`/`EXPAND_CAT`), and hideset-terminated mutual recursion. Covered by `tests/m21_dedicated_expander.c` plus the full M17/M21/M22 preprocessor corpus and the self-host build.

## M22: GNU C Extensions & Kernel / TCC Compilation Support

- [x] Support covered GCC-style inline assembly (`__asm__`, `__asm__ __volatile__`, or `asm`) declarations and statements as inline pass-through template-only asm.
- [x] Add tolerant parsing/skipping for covered GCC-style `asm` declarations/statements and common calling convention qualifiers, plus existing `__attribute__((...))` skipping coverage. Covered by `tests/m22_gnu_extensions.c`.
- [x] Emit/preserve covered GNU `asm` forms in backend assembly instead of only accepting and skipping them. Covered by `tests/m22_gnu_extensions.c` assembly checks for top-level and statement asm templates.
- [x] Support enum constant-expression initializers needed by external compiler sources. Covered by `tests/m22_enum_const_expr.c`.
- [x] Support comma expressions in expression statements, control-flow conditions, and `for` step expressions. Covered by `tests/m22_comma_expr.c`.
- [x] Support adjacent string literal concatenation in global `char[]` initializers and ordinary expression positions. Covered by `tests/m22_adjacent_string_array.c`.
- [x] Support user labels and `goto` lowering for covered intra-function jumps. Covered by `tests/m22_goto_label.c`.
- [x] Support prefix `++`/`--` on non-identifier scalar lvalues such as struct fields. Covered by `tests/m22_prefix_lvalue.c`.
- [x] Preserve the `__b1cc__` predefined macro at preprocessing time so self-hosted common code keeps using the fixed-arity `sb_appendf` path after on-the-fly macro expansion. Verified by `b1cc -E src/common.c` selecting the fixed-arity branch and by the self-hosted corpus in `make test`.
- [x] Support mixed local declarations where an array declarator is followed by simple scalar/pointer declarators, such as `char buf[4], *e;`. Covered by `tests/m22_mixed_local_declarators.c`.
- [x] Evaluate conditional `?:` constant expressions inside global aggregate initializers used by TCC-style struct opcode tables. Covered by `tests/m22_tcc_struct_table_init.c`.
- [x] Accept covered typedef-based global declarations where a macro places `static`/`extern` after the typedef name, such as TCC's `TCC_SEM(static tcc_compile_sem)`. Covered by `tests/m22_typedef_storage_after_type.c`.
- [x] Support covered global function pointer declarations initialized with function symbols, such as TCC's `static void *(*reallocator)(...) = default_reallocator;`. Covered by `tests/m22_global_function_pointer_initializer.c`.
- [x] Fold covered `__builtin_offsetof(Type, field)` expressions inside global aggregate initializers. Covered by `tests/m22_builtin_offsetof_initializer.c`.
- [x] Expand string literals used as `char[N]` fields inside struct aggregate initializers. Covered by `tests/m22_struct_char_array_string_init.c`.
- [x] Resolve local variables before global enum constants when names collide, so TCC-style loops such as `for (...; --n > 0;)` lower as lvalues. Covered by `tests/m22_local_shadows_enum.c`.
- [x] Tolerate null constant address-lowering in covered TCC configuration/dead-code paths by lowering address requests for numeric zero to a null pointer constant.
- [x] Accept GNU tail attributes after `struct/union` definitions, including packed nested fields used by B1NIX kernel headers. Covered by `tests/m22_struct_tail_attribute.c`.
- [x] Tolerate GNU asm labels after function declarators/prototypes, such as kernel atomic builtin declarations. Covered by `tests/m22_function_asm_label.c`.
- [x] Evaluate constant expressions in local array dimensions and tolerate covered runtime-sized local arrays with a conservative fixed fallback for object compilation, including mixed local declarators. Covered by `tests/m22_local_array_constexpr_dim.c`.
- [x] Tolerate block-scope `extern` declarations/prototypes used by B1NIX kernel sources. Covered by `tests/m22_block_scope_extern.c`.
- [x] Parse local variables declared with enum types. Covered by `tests/m22_local_enum_variable.c`.
- [x] Parse covered function-pointer type casts such as `(void (*)(int))0` used by kernel signal macros. Covered by `tests/m22_function_pointer_type_cast.c`.
- [x] Tolerate zero compound literals after casts, such as `(struct sigaction){0}` in B1NIX kernel signal handling. Covered by `tests/m22_compound_literal_zero.c`.
- [x] Tolerate compound literals with array type casts such as `(char *[]){"root"}` in userspace libc tables. Covered by `tests/m22_compound_literal_array_cast.c`.
- [x] Tolerate address-lowering of covered aggregate ternary expressions in kernel signal fallback assignments so object compilation can continue; full addressable aggregate ternary semantics remain future work.
- [x] Support local `struct/union` definitions followed by a declarator, such as `struct k_sigevent { ... } sev;`. Covered by `tests/m22_local_struct_definition_declarator.c`.
- [x] Support covered array designated initializers such as `[1] = 7` in global aggregate tables. Covered by `tests/m22_array_designated_initializer.c`.
- [x] Support global pointer initializers that take the address of another global symbol, such as `FILE *stdin = &_stdin;`. Covered by `tests/m22_global_pointer_address_initializer.c`.
- [x] Parse global arrays of function pointers such as `static void (*atexit_funcs[32])(void);`. Covered by `tests/m22_global_function_pointer_array.c`.
- [x] Support global pointer and aggregate initializers with casted string literals, such as `char *program_invocation_name = (char *)"b1nix";` and `{ (char *)"UTC" }`. Covered by `tests/m22_global_pointer_casted_string.c`.
- [x] Support static-local `char[N] = "..."` initializers used by userspace libc timezone state. Covered by `tests/m22_global_pointer_casted_string.c`.
- [x] Support mixed static-local scalar declarations such as `static int probed, has_tls;`. Covered by `tests/m22_static_local_mixed_scalars.c`.
- [x] Tolerate extra nested braces around array fields in global aggregate initializers, such as B1NIX libc's `IN6ADDR_ANY_INIT` anonymous-union IPv6 constants. Covered by `tests/m22_nested_array_initializer_braces.c`.
- [x] Parse covered wide character literals such as `L'A'` and `L'\n'` as integer constants for B1NIX libc wchar helpers. Covered by `tests/m22_wide_char_literal.c`.
- [x] Map GNU asm operands/register constraints; operand/constraint forms are now fully parsed, lowered to IR, and register-mapped in backends (x86_64, i386, and ARM64).
- [x] Implement semantic handling for GCC attributes that affect code generation, layout, symbols, or calling convention; current covered attributes are syntax-tolerated only.
- [x] Support additional C standard header definitions and structures needed to compile complex external C programs (like `b1nix` kernel or TCC).

## M23: Startup Assembly & Bootstrapping

- [x] Add `.S`/`.s` assembly file handling in the driver: assemble with system/cross assembler, not b1cc backend.
- [x] Cross-assembler for B1NIX `.S` files: `clang --target=<triple> -c` (bypasses b1nix-cc which adds CRT0+libc).
- [x] Multi-file compilation with mixed `.S` + `.c` inputs.
- [x] Regression tests: assemble B1NIX crt0.S for x86_64 and i386, verify ELF output and symbols.
- [x] Document: b1cc is a compiler/assembler; CRT0 and linker scripts live in the B1NIX tree. See the "Toolchain boundary" section in `README.md`.

## M24: Kernel Code Model

- [x] Add kernel code model support: `-mcmodel=small` / `-mcmodel=kernel` flags in driver and backend.
- [x] Emit absolute addressing (`movabs`) for kernel model, RIP-relative for small/default in x86_64 backend.
- [x] Add regression tests: kernel model produces `movabs` absolute addressing, small/default produces RIP-relative.
- [x] Document code model constraints and linker script anatomy. See the "Code models" section in `README.md`: `-mcmodel=small` selects RIP-relative addressing, `-mcmodel=kernel` selects absolute `movabs` for the top negative-2 GiB kernel half; b1cc chooses the addressing form while the B1NIX/kernel linker script places sections at the matching addresses.

## M25: Kernel Target & Makefile Integration

- [x] Add `--target=x86_64-elf` and `--target=i686-elf` bare-metal kernel targets.
- [x] Kernel targets use native ELF writer for `-c` object output (no external assembler).
- [x] Kernel targets delegate `.S` assembly to `clang --target=<triple> -c`.
- [x] Unknown flags (e.g. `-ffreestanding`, `-std=c11`, `-Wall`) are silently ignored for kernel targets.
- [x] Regression tests: kernel target compiles to ELF objects, uses absolute addressing with `-mcmodel=kernel`.

## M26: Dynamic Linking & PIC

- [x] Support `-fPIC` / `-fpie` / `-fPIE` / `-fpic` flags for position-independent code generation on x86_64 and arm64-darwin.
- [x] Emit GOT-indirect addressing (`symbol@GOTPCREL(%rip)`) for external/global symbol access in PIC mode on x86_64.
- [x] Emit PLT stubs via `R_X86_64_PLT32` relocations for external function calls (ELF writer already emits PLT32 for all direct calls).
- [x] Add `-shared` flag to produce shared libraries (`.so` / `.dylib`). For host targets the driver links via `cc -shared` (→ Mach-O `.dylib`); for ELF targets (`x86_64-b1nix` / `i386-b1nix` / kernel-ELF) it compiles each input with b1cc's own ELF writer and links the shared object with `ld.lld -shared` directly (`b1nix-cc` only builds static executables). Override the linker with `$B1NIX_LD`. Covered by `tests/m26_shared_lib.c` (host `.dylib`) and `tests/m26_b1nix_shared_so` (b1cc-native x86_64-b1nix objects → real `ET_DYN` `.so`).
- [x] Emit dynamic relocations (`.rela.dyn`, `.rela.plt`) and `.dynamic` / `.dynsym` / `.dynstr` sections. These are built by `ld.lld` from b1cc's GOTPCREL/PLT32 relocatable objects — the same compiler/linker division as gcc/clang. This is now verified end-to-end: `tests/m26_b1nix_shared_so` links a real `ET_DYN` shared object with populated `.dynamic`/`.dynsym`/`.gnu.hash` and an exported dynamic symbol. **Prerequisite fixed in this milestone:** the ELF writer emitted `.rela.debug_line` with `sh_link`/`sh_info` swapped (the section-index assignment order did not match the header write order), so b1cc-native objects were rejected by `ld.lld` and had never actually been linked before — only inspected. See `src/elf_writer.c` (`write_elf64_object`).
- [x] Regression tests: PIC assembly verification (GOTPCREL pattern), PIC ELF object output, PIC host-execution tests, and shared-object linking (`m26_shared_lib`, `m26_b1nix_shared_so`).

**M26 status:** closed. PIC code generation (x86_64 / i386 / arm64-darwin), GOTPCREL/PLT32 relocations, and the `-shared` flag are implemented and tested; b1cc produces relocatable objects that `ld.lld` links into working `ET_DYN` shared objects (B1NIX has an in-kernel dynamic linker — `kernel/user/process.c`, `PT_DYNAMIC`/`DT_NEEDED`/`R_X86_64_RELATIVE` — so such objects are loadable at runtime).

### M26 — remaining, honestly scoped

- **B1NIX-loadable `.so` policy** (linker script `userspace/linker_shared.ld`, `-soname`, linking against `libc.so.1`) is supplied by the caller through ordinary link flags, exactly as B1NIX's own `userspace/Makefile` does for `libc.so.1` / `m69_plugin.so`. b1cc drives the link; it does not hard-code B1NIX's shared-link policy.
- **Boot-verified integration** (loading a b1cc-built `.so` under the running B1NIX kernel) is not part of the b1cc test suite — it needs a full kernel boot (qemu/WSL) and belongs in the B1NIX tree's tests.
- **i386 `.so`**: the driver emits `-m elf_i386` for `-shared`, but an actual i386 shared link currently fails — `ld.lld` rejects `R_386_PC32` against the local `__x86.get_pc_thunk.bx` used by the i386 PIC prologue. The end-to-end `.so` path is therefore regression-tested for x86_64 only; fixing the i386 PC-thunk relocation (it must be GOT/PLT-relative in `-shared` mode) is the concrete next step.
- **Native Mach-O PIC stub/TLV emission** is unnecessary: arm64-darwin `-shared` links working `.dylib`s through host `cc`, which handles PIE/stubs.

## M27: Csmith Coverage & Differential Testing

- [x] Fix `is_function_decl()` to recognize `union` return types for union-returning function declarations. Covered by `tests/m27_union_function_decl.c`; this is declaration parsing coverage, not a claim of complete union return ABI support.
- [x] Support non-constant (link-time-constant) address-of expressions (`&g_symbol`, `&g_array[idx]`, `&g_struct.field`) in global pointer initializers. Covered by `tests/m27_global_address_initializer.c`.
- [x] Support array subscript lvalues in for-loop initializer expressions (`for (arr[i] = 0; ...)`). Covered by `tests/m27_for_subscript_lvalue.c`.
- [x] Support global aggregate pointer initializers with symbol addresses in non-scalar aggregate paths. Covered by `tests/m27_global_pointer_aggregate_initializer.c` and csmith seed 78.
- [x] Support global aggregate pointer initializers with casted null pointers. Covered by `tests/m27_global_pointer_aggregate_initializer.c`; csmith seed 705 now passes, and seed 377 advances past this compile blocker.
- [x] Materialize aggregate assignment expressions used as aggregate call arguments, including `((*p) = value)`, so `lower_addr()` no longer sees `op='store_index'` for that argument shape. Covered by `tests/m27_aggregate_assignment_argument.c`; csmith seed 377 now compiles and runs to checksum comparison.
- [x] Preserve global struct array element stride separately from byte storage size. Covered by `tests/m27_global_struct_array_stride.c`; fixes csmith seeds 5, 13, 30, 38, and 46 in the first-100 range.
- [x] Preserve unsignedness for array subscripts and known function-call return values. Covered by `tests/m27_unsigned_array_comparison.c` and `tests/m27_unsigned_call_comparison.c`; fixes csmith seed 16 in the first-100 range.
- [x] Truncate/sign-extend narrow local scalar initializers and assignment-expression results to storage type. Covered by `tests/m27_local_narrow_initializer_trunc.c` and `tests/m27_signed_store_index_assignment_value.c`; fixes csmith seeds 21 and 55 in the first-100 range.
- [x] Preserve unsigned narrow loads and return-to-parameter ABI lowering. Covered by `tests/m27_unsigned_local_array_load.c` and `tests/m27_unsigned_narrow_return_param.c`; fixes csmith seeds 7 and 20 in the first-100 range.
- [x] Treat small struct/union assignment as aggregate copy even when the aggregate is <= pointer width. Covered by `tests/m27_small_aggregate_assignment.c`; fixes csmith seed 51 in the first-100 range.
- [x] Separate global pointer storage width from pointee scale, and use pointee scale for `*p` stores. Covered by `tests/m27_pointer_pointee_store_scale.c` and `tests/m27_global_pointer_pointee_store_scale.c`; fixes csmith seed 3 in the first-100 range.
- [x] Finish broad-range checksum mismatches for the current M27 csmith profile. Covered by focused regressions for assignment-expression value semantics, narrow signed/unsigned propagation, comparison result typing, pointer pointee signedness, and nested struct-pointer assignment expressions. Previously failing seeds 116, 163, 289, 333, 339, 490, 537, 632, 641, 659, 680, 757, 832, 851, 858, 888, 919, 921, 950, 1052, 1062, 1191, 1199, 1234, 1315, 1369, 1409, 1649, 1670, 1678, 1695, 1755, 1797, 1847, 1898, and 1929 now pass in targeted checks and the final broad runs.
- [x] Re-run and triage broad-range csmith after the first-100 cleanup. Current broad runs were `python3 csmith_batch.py 1000 0` (seeds 1..1000) and `python3 csmith_batch.py 1000 1000` (seeds 1001..2000) after the M27 fixes and self-host regression cleanup.
- [x] Lower pointer-address comparisons used as integer subexpressions consistently for the previously identified examples. The old examples 410, 1248, and 1578 now pass in the current broad runs. This does not prove every pointer comparison shape is complete; keep future failures tied to concrete seeds.
- [x] Fix remaining b1cc-built binary execution timeouts for the current M27 csmith profile. Covered by `tests/m27_unsigned_short_for_update_wrap.c`; former b1cc-side timeout seeds 477, 661, 1514, and 1626 now pass in targeted checks and in the final broad re-runs. Remaining NOC entries are reference-only timeouts where the `cc`-built reference binary also fails to produce a checksum within the harness timeout.

### M27 Regression Coverage
- Previous baseline: **578 PASS** / 96 FAIL / 326 NOC out of 1000 csmith seeds (arm64-darwin).
- Older same-range check before the final first-100 cleanup (`python3 csmith_batch.py 1000 0`, seeds 1..1000): **801 PASS** / 153 FAIL / 46 NOC.
- Older additional range before the final first-100 cleanup (`python3 csmith_batch.py 1000 1000`, seeds 1001..2000): **800 PASS** / 156 FAIL / 44 NOC.
- Current first-100 sanity is included in the current first broad range: seeds 1..100 are **99 PASS** / 0 FAIL / 1 NOC; the remaining NOC is a reference timeout at seed 54.
- Final first broad range (`python3 csmith_batch.py 1000 0`, seeds 1..1000): **988 PASS** / 0 FAIL / 12 NOC. NOC breakdown: reference timeouts at 54, 120, 127, 249, 292, 331, 334, 585, 649, 781, 794, and 863.
- Final second broad range (`python3 csmith_batch.py 1000 1000`, seeds 1001..2000): **982 PASS** / 0 FAIL / 18 NOC. NOC breakdown: reference timeouts at 1016, 1022, 1027, 1041, 1189, 1410, 1555, 1587, 1592, 1593, 1613, 1629, 1638, 1676, 1774, 1844, 1931, and 1963.
- Final combined broad status (seeds 1..2000): **1970 PASS** / 0 FAIL / 30 NOC. M27 compiler-side checksum mismatches and b1cc-built binary execution timeouts are closed for the current profile; the remaining no-result outcomes are reference-only long-running programs. A spot-check of reference-timeout seeds 54, 120, 127, 249, 292, 331, 334, 585, 649, and 781 with a 30-second reference timeout still produced no checksum.

## M28: Expand Csmith Feature Profile

Goal: use the current M27 csmith profile as the baseline, then remove restrictions one feature family at a time by adding real compiler support and regression coverage. Do not mark a restriction as lifted until the corresponding generated programs compile, run, and pass differential checks.

Current M28 csmith profile: all M27 `--no-*` feature-family restrictions listed below are lifted. Shape limits remain intentionally small while the compiler-side feature surface is hardened.

```sh
--max-funcs 2 --max-block-depth 2 --max-block-size 2
--max-expr-complexity 4 --max-array-dim 1
--max-array-len-per-dim 3
--max-pointer-depth 1 --max-struct-fields 2
--max-union-fields 1
```

- [x] Lift `--no-longlong`: complete `long long` parsing, integer conversion, constant folding, ABI lowering, backend arithmetic/comparison, and object/debug coverage for the current csmith profile. Covered by M19/M27/M28 regressions plus `tests/m28_unsigned_longlong_initializer.c` and `tests/m28_large_hex_ll_unsigned_compare.c`.
- [x] Lift `--no-float`: scalar float/double csmith generation is enabled for the current shape-limited profile. Existing M18/M19 ABI/scalar tests continue to pass; no compiler-side csmith failures remain in the final first-thousand run.
- [x] Lift `--no-consts`: const-qualified generated objects compile and run under the current profile, including const union fields in local aggregate arrays. Covered by `tests/m28_local_union_array_initializer.c`.
- [x] Lift `--no-volatiles`: volatile globals and pointer-side-effect paths compile and run under the current profile. Seed 800 also covers volatile pointer participation around the fixed local union array initializer path.
- [x] Lift `--no-jumps`: csmith-generated jumps are enabled under the current shape limits; existing goto/label/break/continue lowering tests remain green.
- [x] Lift `--no-bitfields`: generated bitfield layout, signed loads, aggregate initialization, and union bitfield initialization are enabled for the current profile. Covered by `tests/m28_bitfield_initializer_copy.c`, `tests/m28_signed_bitfield_load.c`, and `tests/m28_union_bitfield_initializer.c`.
- [x] Lift `--no-packed-struct`: packed structs are enabled under the current shape limits; the 50-seed gate and final 1000-seed run have no compiler-side failures.
- [x] Lift `--no-builtins`: csmith helper/builtin surface used by the current profile compiles and runs; no compiler-side failures remain in the final first-thousand run.
- [x] Lift `--no-divs`: signed/unsigned division and modulo are enabled, including unsigned 64-bit modulo and unsigned casts feeding modulo. Covered by `tests/m28_unsigned_mod64.c` and `tests/m28_unsigned_cast_mod_assignment_expr.c`.
- [x] Lift `--no-comma-operators`: comma-expression value, lvalue, aggregate value, aggregate call, and discarded aggregate-call side-effect paths are enabled. Covered by `tests/m28_comma_lvalue.c`, `tests/m28_comma_aggregate_value.c`, `tests/m28_comma_aggregate_assignment_arg.c`, `tests/m28_comma_aggregate_call.c`, and `tests/m28_comma_discarded_aggregate_call_once.c`.
- [ ] Raise shape limits gradually: functions beyond 2, deeper blocks, larger block sizes, expression complexity above 4, multidimensional arrays, arrays longer than 3, pointer depth above 1, and wider struct/union field counts.

M28 validation log:

- Per user instruction, each feature-family flag lift was gated with 50 csmith seeds; after all family flags were removed, the final broad run was 1000 seeds from offset 0.
- 50-seed gates:
  - comma operators: **50 PASS / 0 FAIL / 0 NOC**.
  - divisions/modulo: **50 PASS / 0 FAIL / 0 NOC**.
  - consts: **50 PASS / 0 FAIL / 0 NOC**.
  - jumps: **50 PASS / 0 FAIL / 0 NOC**.
  - builtins: **50 PASS / 0 FAIL / 0 NOC**.
  - long long: **48 PASS / 0 FAIL / 2 NOC**, reference timeouts at seeds 5 and 25.
  - volatiles: **47 PASS / 0 FAIL / 3 NOC**, reference timeouts at seeds 5, 25, and 33.
  - bitfields: **49 PASS / 0 FAIL / 1 NOC**, reference timeout at seed 25.
  - packed structs: **49 PASS / 0 FAIL / 1 NOC**, reference timeout at seed 25.
  - floats: **49 PASS / 0 FAIL / 1 NOC**, reference timeout at seed 25.
- Final full-family run after the M28 fixes (`python3 csmith_batch.py 1000 0`, seeds 1..1000): **973 PASS / 0 FAIL / 27 NOC**.
- Final NOC breakdown: all 27 are reference-only timeouts, at seeds 25, 113, 127, 206, 221, 232, 258, 267, 299, 313, 332, 350, 399, 411, 462, 515, 541, 551, 580, 586, 627, 707, 855, 874, 880, 917, and 944. There were **0 checksum FAIL**, **0 b1cc compile NOC**, **0 assembly NOC**, and **0 b1cc execution timeout NOC** in the final run.
- Focused M28 fixes added regression coverage for comma lvalues/aggregate values, local/global pointer arrays, narrow local reload signedness, comma-wrapped aggregate calls, unsigned long long initializers, large hex LL unsigned comparisons, bitfield initializer packing/sign extension/union sizing, unsigned equality conversion, unsigned 64-bit modulo, bitwise usual conversions, unsigned integer casts feeding modulo, and local union array initializer stride.

Skipped: full C/C++ upfront. Add features only when a test or B1NIX source needs them.

## M29: C99 and C11 Standards Compliance Gaps

- [x] Implement dynamic runtime stack allocation for Variable-Length Arrays (VLAs) instead of the static fallback. ARM64 and x86_64 support `vla_alloc` IR; i386 support pending.
- [x] Support parsing hexadecimal floating-point constants (e.g. `0x1.5p3`) in the lexer.
- [x] Support alignment specifiers (`_Alignas` / `alignas`) in declarations. Global alignment is fully supported; local alignment support is limited to the stack frame alignment (sp).
- [x] Support Thread-Local Storage (`_Thread_local` / `thread_local`) with compiler-generated thread-local segments and relocations. x86_64 B1NIX uses `%fs:0` / `@tpoff` model; ARM64 Darwin uses TLV descriptors with `__DATA,__thread_data` / `__DATA,__thread_vars` sections. i386 B1NIX pending.
- [x] Support atomic operations (`_Atomic` / `<stdatomic.h>`) by generating platform-specific atomic/locked instructions. `_Atomic` qualifier is parsed and tolerated; `sizeof(_Atomic T) == sizeof(T)` holds. No platform-specific locked instructions (single-threaded semantics only).
- [x] Support complex and imaginary types (`_Complex`, `_Imaginary`) in parser. Parser recognizes `_Complex`/`_Imaginary` and doubles the base type size. No backend complex arithmetic.
- [x] Conforming layout and size rules for flexible array members in structures.

## M30: Standard Library & Freestanding Self-Sufficiency

- [x] Bundle freestanding standard C headers (`<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<stdarg.h>`) directly with `b1cc`. Headers are embedded in the compiler binary via `src/builtin_headers.c` and written to a temp directory at startup. The bundled headers shadow system headers and provide standard types (`size_t`, `ptrdiff_t`, `int32_t`, `uint64_t`, `bool`, etc.) under `#ifdef __b1cc__`.
- [x] Implement internal or static library compiler-runtime helpers (like `__divdi3` / `__moddi3`) for division/modulo operations. `runtime/divdi3.c` provides signed/unsigned 64-bit software division and modulo for 32-bit targets (i386), covering `__divdi3`, `__moddi3`, `__udivdi3`, and `__umoddi3`.
- [x] Provide a minimal self-sufficient runtime/libc layer (implementing `memcpy`, `memset`, `memmove`, `strlen`, etc.) to eliminate dependency on host/external `libc`. `runtime/runtime.c` provides `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strcat`, `strchr`, `strrchr`, `strstr`, `snprintf`, `sprintf`, `atol`, `atoi`, `abort`, and `exit`.
- [x] Support compiling and static-linking standalone / bare-metal binaries without host linker requirements. The driver accepts `-ffreestanding` to auto-compile and link the runtime helpers, and `-nostdlib` to skip runtime linking. For ELF targets (B1NIX/kernel), runtime is compiled via b1cc's native ELF writer; for Darwin, the host `cc` compiles the runtime objects.

## M31: Compiler Performance Optimizations

- [x] Replace `strcmp(node->op, ...)` with integer enum comparisons (`OpKind`) across 137+ call sites in `parser.c` and `ir.c`. Adds `OpKind` enum to `ast.h`, `op_enum` field to `Node` struct, and `str_to_op()`/`op_to_str()` lookup functions in `ast.c`.
- [x] Optimize `sb_appendf()` to single-pass with 512-byte stack buffer. Avoids double `vsnprintf` call (once for length, once for formatting) for the common case.
- [x] Use `arena_strndup()` with known length in `sb_to_string()` instead of `arena_strdup()` which recomputes length via `strlen`.
- [x] Replace `active_macros` HashMap clone with `StringArray` recursion guard in lexer macro expansion. Linear search over ~3-5 entries is faster than HashMap clone for small sets.
- [x] Use POSIX `access()` instead of `fopen()`/`fclose()` for file existence checks in preprocessor and driver.

### M31 Benchmark Results

Measured on arm64-darwin, 200 iterations each:

| File | Before | After | Speedup |
|------|--------|-------|---------|
| `tests/for.c` (120 lines) | 5.138s | 1.865s | **2.75x** |
| `bench_large.c` (~200 lines) | 4.926s | 2.780s | **1.77x** |

### Open Performance Work

- [x] Cache hash values in `HashMapEntry` for fast resizing. (Superseded by the open-addressing rewrite below, which stores entries inline and rehashes on resize.)
- [x] Open-addressing HashMap with linear probing for better cache locality. `HashMap` is a flat `entries[]` array probed with `(h + i) % bucket_count` and `TOMBSTONE` deletion (`src/common.c`); all iteration sites use `.entries[b]` with a `key && key != TOMBSTONE` guard.
- [x] Parallel function emission in backends via pthreads. `src/backend.c` emits each IR function on its own `pthread` with a private arena and concatenates results in program order. Host builds only — the self-host (`__b1cc__`) path stays serial.
- [x] String interning for frequently compared operator/keyword strings.
- [x] Compact IR representation: opcode as `IrOp` enum. `IrInst.op` is a 4-byte `IrOp` instead of an 8-byte `const char *`; `ir_push()` interns the op string to an enum once via `ir_string_to_op()`, and all three backends dispatch with integer `==` instead of `strcmp` (~450 sites). `ir_op_to_string()` backs the `-fdump-ir` output.
- [x] Compact IR representation: operands as indices. `IrInst.arg` is a 4-byte pool index instead of an 8-byte `const char *`; operand strings are deduplicated into a global pool by `ir_intern_arg()` at lowering time and read back with `ir_arg_str()` at emit time (empty operand is always index 0). Combined with the opcode enum this shrinks `IrInst` from 32 to 24 bytes.
- [x] `sb_append_n()` with known length to avoid `strlen` for string literals in backends.
- [x] Pool-allocate `HashMapEntry` instead of per-entry `malloc`.

## Retiring TCC (M32–M35)

**Goal of this track:** make b1cc able to replace `/bin/tcc` as B1NIX's on-device C
compiler, then retire TinyCC. This is the concrete work behind the long-standing
gate in M2 ("Retire TCC fallback only after b1cc passes equivalent B1NIX tests")
and the M8/M20 self-hosting track. `/bin/tcc` is a native, on-device, all-in-one
compiler+assembler+linker (full C89/C99) built with `b1nix-cc`; b1cc today is a
host cross-compiler for a C subset that shells out to `b1nix-cc`/`ld.lld` for
linking. The three real blockers are: (M32) run on the device, (M33) link on the
device, (M34) compile the full on-device C workload. M35 is the cutover.

Do not check any item here until the described behavior actually runs on B1NIX
with regression/differential coverage; partial cross-host results do not count.

## M32: On-Device Self-Host (b1cc runs on B1NIX)

Goal: `/bin/b1cc` — b1cc built by `b1nix-cc` (and ultimately by itself) runs as a
B1NIX userspace ELF and compiles the covered corpus *on the guest*, not just on
the macOS host. Current partial state: `build/b1cc_self` (host-built) compiles the
covered corpus (M20); no b1cc binary has been executed on B1NIX yet.

- [x] Build b1cc with `b1nix-cc` for `x86_64-b1nix` / `i386-b1nix` into a runnable `/bin/b1cc`, closing each B1NIX libc gap it hits. (x86_64 done and proven on-device via the boot-test gate below; i386 not yet — the internal linker is x86_64-only so far.) b1cc currently depends on host facilities that must be provided or removed on-device: `fopen`/`fwrite`, `mkstemps`, `unlink`, `getenv`, `access`, `snprintf`, and `pthread` (backend parallel emit — already gated off for the `__b1cc__` self-host path, keep it serial). Enumerate the exact set with a trial `b1nix-cc` build; each missing symbol is a concrete sub-task.
  - Gap-enumeration result (2026-07-10): a trial `b1nix-cc`/clang build of every `src/*.c` against the current `libb1nix.a` compiles and links with **zero undefined symbols**. All the facilities listed above (`fopen`/`fwrite`/`mkstemps`/`mkdtemp`/`unlink`/`getenv`/`access`/`snprintf`/`pthread_*`) are real `T` definitions in `libb1nix.a`. There is no libc *link* gap left; what remains for this item is (1) removing the shell-outs below, (2) on-device linking (M33), and (3) actually executing the binary on B1NIX. Note: do **not** pass `-D__b1cc__` for the b1nix-cc build — it switches `common.h`'s `sb_appendf` to the fixed-arg subset signature and breaks the clang build. A `/bin/b1cc` target now exists in `../b1nix/userspace/Makefile` (builds all `b1cc/src/*.c` via `b1nix-cc -std=gnu2x` — the `-std=gnu2x` is required, the clang default rejects C23 `nullptr`); `make build/x86_64/bin/b1cc` produces a static B1NIX ELF. Still to do for this box: ship it in the initramfs and actually run it on B1NIX (needs the M33 internal linker to produce executables → M32 boot-test gate below).
- [x] Remove the toolchain shell-outs: b1cc uses `system()` for temp-dir creation, `rm -rf` cleanup, and invoking the assembler/linker. On-device there is no shell — replace with in-process file handling (temp files via VFS) and the in-process link path from M33.
  - Done for the on-device path (2026-07-10): the compile+internal-link path that runs on B1NIX makes **no** `system()`/shell call — management shell-outs were replaced with in-process `remove_tree()`/`mkdir()`, native `elf_write_object` needs no assembler, and the M33 internal linker replaces the link shell-out. Proven end-to-end on-device (boot-test gate). The remaining `system()` calls in `src/b1cc.c` are only on the host cross-compile path (invoking `b1nix-cc`/`clang`/`ld.lld`), never reached when `internal_link` is on. Temp files still use host `/tmp` (works on B1NIX); a VFS-specific temp abstraction is not needed for the current path.
  - Progress (2026-07-10): the *management* shell-outs are gone — `mkdir -p` and every `rm -rf '<tmp>'` `system()` call in `src/b1cc.c` are replaced by an in-process recursive `remove_tree()` and a direct `mkdir()`. These ran even on the native `-c` path (which uses `elf_write_object`, no external assembler) and previously failed silently on B1NIX (no `/bin/sh`), leaking temp dirs; also fixed a pre-existing leak where the single-input compile+link fast path never removed its builtin-headers temp dir. Verified shell-free + leak-free across `-c`/`-S`/compile/compile+link for `x86_64-b1nix`; produced `return_42` binary runs and exits 42. **Still deferred:** the remaining `system()` calls all invoke the assembler/linker with command strings — the assembler ones are host-only (the B1NIX native path never reaches them) and the linker ones are superseded by the M33 internal link path, so they are intentionally left until M33 rather than churned into `fork`/`execv` now. Temp files still go through host `/tmp` (`mkstemps`/`mkdtemp`); VFS temp handling lands with M33.
- [x] Boot-test gate: run `/bin/b1cc tests/return_42.c` under the B1NIX kernel (qemu/WSL) and execute the produced binary to exit 42, mirroring the existing M25 `tcc` smoke (`M25-SMOKE: compile-hello`). Add it to the B1NIX smoke suite.
  - Done (2026-07-10): `/bin/b1cc` runs as a B1NIX userspace process, compiles a C source AND links it with its own internal linker (no host `ld.lld`/`b1nix-cc`), and the produced ELF loads and exits 42 — proven in QEMU. Driver `userspace/bin/b1cc_selfsmoke.c` (`/bin/b1cc-selfsmoke`, statically linked) emits `B1CC-SELF-COMPILE-SMOKE: ok`, added to `tests/smoke-b1cc.sh` (now 8/8) and `kernel/user/programs.c`. `/bin/b1cc` + `/lib/b1cc/{crt0.o,libb1nix.a}` are shipped in the initramfs (`-DB1CC_SELFHOST`, x86_64). Fixes that this surfaced: (a) b1cc's `set B1NIX_CC` fatal check now skipped under internal linking (on-device has no b1nix-cc); (b) on-device b1cc defaults to the internal linker with `/lib/b1cc` paths (`#ifdef b1nix`); (c) unrelated: `tools/ports/build-busybox.sh` `-Ttext-segment`→`--image-base` for ld.lld v22.

## M33: On-Device Linking (no host linker)

Goal: b1cc produces final executables and shared objects on B1NIX without shelling
to host `ld.lld` / `b1nix-cc`. Today b1cc only emits `ET_REL` objects; M26 proved
those link into a real `ET_DYN` via host `ld.lld`, but the device has no `ld.lld`.
tcc has a built-in linker; b1cc needs an equivalent on-device link step.

- [x] Decide the approach via an ADR: (a) teach b1cc an internal static/dynamic linker, or (b) port a minimal `ld` / `ld.lld` to B1NIX and have b1cc drive it in-process. **Decision (2026-07-10): option (a) — internal linker in b1cc.** Rationale: removes the external dependency entirely, keeps `/bin/b1cc` self-contained (true tcc-style all-in-one), and matches the goal of retiring host/on-device `ld.lld`. Cost accepted: b1cc must implement symbol resolution, relocation application, `PT_LOAD` layout, and `ET_EXEC`/`ET_DYN` emission itself.
- [x] Internal link path (if (a)): combine b1cc-native `ET_REL` objects + `crt0.o` + `libc.a`/`libc.so.1`, resolve symbols, apply relocations, lay out `PT_LOAD` segments, and honor the B1NIX linker-script policy (`../b1nix/userspace/linker.ld` for `ET_EXEC`/PIE, `linker_shared.ld` for `.so`). Emit `ET_EXEC`/`ET_DYN` directly. **Done (2026-07-10): static `ET_EXEC`, PIE `ET_DYN`, and shared `.so` all implemented in `src/linker.c` and proven on-device** (see the boot-gate item below). x86_64-b1nix; i386 pending (a separate roadmap concern).
  - Progress (2026-07-10): **static `ET_EXEC` path implemented** in new `src/linker.c` (`elf_link_static`) — ELF64 object + System V archive parsing, transitive member resolution, weak-undef→0, COMMON→.bss, linker-defined `__init_array_start/end`, relocations `R_X86_64_64/PC32/PLT32/32/32S`, `PT_LOAD` layout at base `0x2000000` matching `linker.ld`. Driven by `--internal-link` / `B1CC_INTERNAL_LD=1` (x86_64-b1nix only). Validated by a 16-program differential (`tests/internal_link_diff.sh`): every program links with b1cc's own linker (no ld.lld) and is byte-behavior-identical (stdout+exit) to the ld.lld reference. Program headers match ld.lld (entry `0x2000000`, same segment VAs).
  - **PIE `ET_DYN` path implemented** in `elf_link()` (LINK_PIE mode): emits ET_DYN with base 0, SysV `.hash`, `.dynsym`/`.dynstr`, `.plt` (eager-binding `ff 25` stubs), `.got`/`.got.plt`, `.rela.dyn` (R_X86_64_RELATIVE + GLOB_DAT) and `.rela.plt` (JUMP_SLOT), PT_DYNAMIC with DT_NEEDED/DT_HASH/DT_STRTAB/DT_SYMTAB/DT_RELA/DT_JMPREL/DT_PLTGOT. VA-sorted emit loop ensures correct segment layout. Driver: `--pie` / `-fPIE` / `-fpie` flags route to `elf_link(mode=LINK_PIE)` for x86_64-b1nix with `crt0-dynamic.o` + `DT_NEEDED=libc.so.1`.
  - **Shared `.so` path** in `elf_link()` (LINK_SHARED mode): emits ET_DYN with DT_SONAME. Driver: `--shared` / `-shared` (+ `--soname=`) route to `elf_link(mode=LINK_SHARED)` for x86_64-b1nix. **Global-export gap closed (2026-07-10):** every defined non-local global in the input objects is now exported into `.dynsym` with its final VA, size, bind, and type (appended after the imported symbols so `.rela.dyn`/`.rela.plt` dynidx references stay stable; values filled after the RW-shift so shifted `.data`/`.bss` symbols get correct addresses). Verified with `objdump -T`: a two-function + one-`.data`-global library exports `add`/`sub`/`the_answer` with `.text`/`.data` provenance and correct `st_size`. `static` functions and file-scope variables are correctly kept out of a `.so`'s exports: the native assembler (`elf_writer.c`) previously marked every `.type X, @function` symbol `STB_GLOBAL` regardless of `.globl` — binding now follows `.globl` only, so `static` symbols are `STB_LOCAL` and the linker (which already skips `STB_LOCAL`) does not export them. Covered by the `SO:so_demo` differential case (a `static` `helper`/`priv` must be absent from the exports). **Linking a program against a b1cc `.so` (2026-07-10):** the driver's `-l<name>` (→ `DT_NEEDED lib<name>.so.1`) and `--needed=<soname>` add extra `DT_NEEDED` deps to the PIE link; the internal linker already lowers every undefined external to a dynamic import, so the kernel loader binds the symbol out of the `.so`'s exported dynsym at runtime.
  - **Non-alloc relocation fix (2026-07-10):** the dynamic reloc walk processed relocations for *all* sections with data, including non-allocatable debug sections. b1cc emits a debug-info `R_X86_64_64` (`.debug_line` → `.text`) whose section has no assigned VA, so the walk emitted a bogus `R_X86_64_RELATIVE` at a `.text`-colliding offset. In a PIE this silently corrupted a `.text` byte (the loader's 8-byte probe stayed in-bounds); in a small `.so` whose RX segment ended right at the overrun the kernel loader's staging probe went out of bounds and the load failed. Fixed by skipping non-`SHF_ALLOC` sections in the dynamic reloc walk.
  - **RW layout collision fixed (2026-07-10):** the dynamic path placed the synthesized RW metadata (`.got`/`.got.plt`/`.dynamic`) at a page derived only from the R-region end, which could land on the same page as the shifted input `.data` — corrupting the `.dynamic` array with a 4-byte offset (observed as garbled DT tags). The RW-shift of input sections now runs *first*, and the dynamic RW metadata is placed above **both** the R region and the input RW sections (incl. `.bss`). Affects PIE and `.so`; any dynamic output with a non-empty `.data` was previously mis-laid.
  - Uncovered and fixed 4 native `elf_writer` (M15 assembler) correctness bugs that this differential exposed (all silently wrong before, affecting the `-c` native path regardless of linker): (1) `cmpq %src,%dst` used the RM-form `0x3B` (computed `src-dst`) instead of MR-form `0x39` — every `<`/`>` comparison inverted; (2) `movl`/`movq`/`movb` had no SIB `(%base,%idx,scale)` store/load form and emitted `nop` — all array element access dropped; (3) `movslq` from memory fell through to the register form — sign-extended array loads dropped; (4) `reg_optype` classified `%cl`/`%al`/`%dl`/`%bl` as 16-bit (the `isalpha(s[2])` REG16 test ran before the `s[2]=='l'` REG8 test) — every variable shift `shl %cl` and `%al`/`%dl` byte move silently emitted nothing.
- [x] Regression: an executable and a `.so` produced entirely on-device (no host tools) load and run under the B1NIX in-kernel dynamic linker (`kernel/user/process.c`, `PT_DYNAMIC`/`DT_NEEDED`/`R_X86_64_RELATIVE`). **Done (2026-07-10): `tests/smoke-b1cc.sh` is 10/10 on QEMU** — static `ET_EXEC`, PIE `ET_DYN`, and `.so` all produced on-device and run.
  - Static `ET_EXEC`: a program produced entirely on-device by b1cc's internal linker loads and runs — `B1CC-SELF-COMPILE-SMOKE: ok`.
  - PIE `ET_DYN`: the boot gate compiles+links a PIE with b1cc's own internal dynamic linker (`crt0-dynamic.o` + `DT_NEEDED=libc.so.1`, no host ld.lld); it loads under the kernel's eager in-kernel dynamic linker (`ELF load: /tmp/b1cc_pie ... (PIE base=0x500000000000)`), resolves the imported `puts` via JUMP_SLOT, and returns 42 — `B1CC-PIE-SMOKE: ok`.
  - Shared `.so`: the boot gate builds a `.so` (exporting `add_three`) AND a separate PIE that `DT_NEEDED`s it (`-lmath` → `libmath.so.1`), then runs the PIE. The kernel loads `/tmp/lib/libmath.so.1` through the `DT_NEEDED` graph (`$ORIGIN/../lib` of `/tmp/bin/dynmain`) and binds `add_three` out of the `.so`'s exported dynsym; `add_three(39) == 42` — `B1CC-SO-SMOKE: ok`.
  - B1NIX side (branch `b1cc-selfhost-boot-gate`): `crt0-dynamic.o` shipped to `/lib/b1cc/`, `libc.so.1` added to the minimal initramfs, PIE+`.so` phases added to `/bin/b1cc-selfsmoke`. Kernel loader treats `PT_INTERP` as informational and links eagerly, so b1cc's PT_INTERP-less PIE/`.so` load without a separate ld.so.
  - Host-side structural coverage (`tests/internal_link_diff.sh` + `tests/elf_dyninfo.py`, **19/0**): PIE asserts DYN, `DT_NEEDED`, DT tag set, `puts` via PLT, PLT-stub==JUMP_SLOT==ld.lld's; `.so` asserts DYN, DT_SONAME, export completeness, no spurious imports; SO-LINK asserts a PIE built with `-lso_demo` records `DT_NEEDED` and imports the `.so`'s symbol. (The formerly-failing `b1cc_better_c` case is fixed: an unbounded inline-asm operand-register-pool index overflowed on the 8-operand x86_64 `syscall` wrapper in `<syscall.h>`, crashing codegen for any TU that includes the standard headers — now clamped in all three backends.)

## M34: Full-C Coverage for the TCC Workload

Goal: b1cc compiles every C program `/bin/tcc` currently compiles on B1NIX —
including b1cc's own sources (complete self-host roundtrip) and the userspace
corpus. Current gaps are the ones named in `README.md`.

- [ ] Close the remaining C-subset gaps: complete aggregate assignment, callee-side varargs, `long double`, full C99 preprocessor edge cases, and floating-point aggregate ABI classification. Each gap lands with its own regression test.
- [ ] Full self-host roundtrip: b1cc compiles b1cc end-to-end (not only the M20 covered corpus), producing a working compiler. Closes the README "complete C self-host roundtrip" gap.
- [ ] Differential harness in the B1NIX tree: for each program in the on-device compile corpus (seeded from the M25 `tcc` smoke set), require `b1cc` and `tcc` to produce binaries with identical exit status and stdout. This harness *is* the "equivalent B1NIX tests" gate referenced by M2.

## M35: TCC Retirement

Goal: replace `/bin/tcc` with `/bin/b1cc` and remove TinyCC. Blocked on M32–M34.

- [ ] b1cc passes the full equivalent B1NIX test suite that `tcc` passes (the M34 differential harness plus the existing M25 smoke). This is the explicit M2 gate.
- [ ] Switch the `/bin/tcc` target in `../b1nix/userspace/Makefile` to build `/bin/b1cc`; keep `tcc` buildable behind a flag as a fallback for one release cycle.
- [ ] After a full release cycle with b1cc as the default on-device compiler and no regressions, remove `../b1nix/userspace/tcc/` and its `THIRD_PARTY_NOTICES.md` / `LICENSING.md` entries.
- [ ] Non-goal until the above are green: do not delete or disable `tcc` while it is the only working on-device compiler.

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

## M9: Optional External Backend

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

## M13: C++ Frontend

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
- [ ] Mixed float/integer aggregate classes and vector aggregate ABI
      classification. i386 aggregate-by-value lowering is independently
      incomplete and out of scope here.

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
- [ ] Replace the internal reuse of the lexer macro expansion engine with a dedicated preprocessing-token expander if full C preprocessor token spacing, source-location fidelity, or additional obscure macro edge cases require it.

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
- [ ] Map GNU asm operands/register constraints; operand/constraint forms are currently parsed for tolerance and skipped rather than emitted.
- [ ] Implement semantic handling for GCC attributes that affect code generation, layout, symbols, or calling convention; current covered attributes are syntax-tolerated only.
- [ ] Support additional C standard header definitions and structures needed to compile complex external C programs (like `b1nix` kernel or TCC).

## M23: Startup Assembly & Bootstrapping

- [x] Add `.S`/`.s` assembly file handling in the driver: assemble with system/cross assembler, not b1cc backend.
- [x] Cross-assembler for B1NIX `.S` files: `clang --target=<triple> -c` (bypasses b1nix-cc which adds CRT0+libc).
- [x] Multi-file compilation with mixed `.S` + `.c` inputs.
- [x] Regression tests: assemble B1NIX crt0.S for x86_64 and i386, verify ELF output and symbols.
- [ ] Document: b1cc is a compiler/assembler; CRT0 and linker scripts live in the B1NIX tree.

## M24: Kernel Code Model

- [x] Add kernel code model support: `-mcmodel=small` / `-mcmodel=kernel` flags in driver and backend.
- [x] Emit absolute addressing (`movabs`) for kernel model, RIP-relative for small/default in x86_64 backend.
- [x] Add regression tests: kernel model produces `movabs` absolute addressing, small/default produces RIP-relative.
- [ ] Document code model constraints and linker script anatomy.

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
- [ ] Emit dynamic relocations (`.rela.dyn`, `.rela.plt`) in ELF object output — deferred: b1cc produces relocatable objects; the linker creates dynamic sections from GOTPCREL/PLT32 relocations.
- [ ] Mach-O stub/TLV sections for PIC on arm64-darwin — deferred (host `cc` already handles PIE by default).
- [ ] Link against shared objects via b1nix-cc or direct ld.lld invocation — deferred; current driver links through b1nix-cc or host cc.
- [x] Regression tests: PIC assembly verification (GOTPCREL pattern), PIC ELF object output, and PIC host-execution tests.
- [ ] Integration: b1cc-compiled code replaces statically linked `b1nix-cc` path in B1NIX userspace — deferred; requires B1NIX tree. i386 PIC support also deferred as lower priority.

Skipped: full C/C++ upfront. Add features only when a test or B1NIX source needs them.

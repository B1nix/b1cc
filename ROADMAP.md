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

## M15: Driver and ABI Regression Coverage

- [x] Add ABI regression tests for stack-passed arguments, varargs calls, function pointers, and small integer returns.
- [x] Add driver modes expected from a tiny C compiler: `-E`, [-c](file:///Users/dmytrom/Documents/GitHub/b1cc/ROADMAP.md#L116), multiple input files, and pass-through linker flags.
- [x] Make native object output real enough for B1NIX: symbols, relocations, sections, and debug dumps.
- [x] Compile a small curated set of real B1NIX userspace files without TCC fallback when the sibling B1NIX tree is present (`b1cc_hello.c`, `b1cc_better_c.c`).

## M16: Compiler Shape Cleanup

- [x] Split C frontend semantics more clearly from target lowering.
- [ ] Add a backend contract for type legalization, calling convention lowering, instruction selection, and register allocation.
- [x] Add tiny local optimizations only after type semantics are stable.
- [x] Grow tests from real B1NIX userspace sources before chasing wider C compatibility; coverage is still optional/non-hermetic because it depends on a sibling B1NIX checkout.

## M17: C99 Preprocessor Operators

- [x] Add macro definitions with arguments (e.g., `#define MAX(a, b) ...`).
- [x] Add macro operators `#` (stringification) and `##` (token pasting).
- [x] Add full constant expression evaluation for conditional directives (`#if`, `#elif` with `defined`).
- [ ] Complete C99 preprocessor edge cases beyond the covered macro/operator tests.

## M18: Conforming Aggregates & ABI

- [x] Support integer/pointer struct passing and return by value with stack/register splitting.
- [ ] Support full target ABI aggregate classification, including large returns and float/vector aggregates.
- [x] Support designated initializers (e.g., `struct Point p = { .x = 1 }`).
- [x] Support nested brace initializers for multidimensional arrays and structures.
- [x] Support bitfield member declarations with packing (`type name : bits`) and pack/unpack via `bfi`/`ubfx` (ARM64) and shift/mask patterns (x86_64, i386).

## Honest M14-M18 Gaps

- M14 integer promotions, usual arithmetic conversions, and qualifier-aware typing are complete.
- M15 native ELF object output covers: ELF64 relocatable objects for x86_64-b1nix (full instruction encoding + .text/.data/.rodata/.bss/.symtab/.strtab/.rela.text sections); ELF32 relocatable stubs for i386-b1nix (correct ELF identity + symbol table, code encoding is stub). arm64-darwin output remains Mach-O via host `cc`. Full i386 instruction encoding and native Mach-O writing are future work.
- M16 still lacks a written/enforced backend contract for type legalization, calling convention lowering, instruction selection, and register allocation.
- M17 still only covers the tested C99 preprocessor operator subset; obscure expansion/rescan/placemarker edge cases remain future work.
- M18 large returns (structs >16 bytes) and float/vector aggregates are not yet supported by the ABI lowering.

## M19: Complete Type System & Math

- [ ] Support floating-point types (`float`, `double`) with vector/FPU backend instructions and registers.
- [ ] Support `long long` 64-bit integer operations on 32-bit platforms (register pairs on i386).
- [x] Add integer-only typing foundation for scalar C11 `_Bool`, C23 `bool`/`true`/`false`, tolerated `const`/`volatile`/`restrict` qualifiers, and usual integer promotions/conversions across current scalar integer types.
- [ ] Complete qualifier semantics beyond parsing/tolerance, including const-correct diagnostics and volatile access semantics.
- [x] Implement bitfields packing/unpacking in aggregates (ARM64 `bfi`/`ubfx`, x86_64/i386 shift+mask; named and anonymous bitfields in top-level and local struct/union definitions).

## M20: Callee-Side Varargs & Self-Hosting

- [ ] Add compiler built-ins for writing vararg functions (`__builtin_va_list`, `__builtin_va_start`, etc.).
- [ ] Support full self-host roundtrip for the C compiler codebase: host-built `b1cc` builds `build/b1cc_self`, and `build/b1cc_self` can compile/link the covered test corpus, not only tiny smoke programs.
- [ ] Add regression coverage for the current self-host smoke boundary: `build/b1cc_self` compiling `tests/return_42.c` and producing an executable that exits with 42.
- [ ] Implement general aggregate assignment for structs/unions, including nested field assignment such as `state.tokens = tokens`, instead of relying on field-wise workaround code in compiler internals.
- [ ] Replace remaining self-host workarounds for by-value aggregate copies with real aggregate copy lowering, or document each intentionally pointer-based API boundary.
- [ ] Extend aggregate return/call lowering beyond the currently covered small integer/pointer aggregate subset; large struct returns and non-integer aggregates must have explicit ABI tests before being marked supported.
- [ ] Add self-host regression tests for local string array initializers such as `char tmp[] = "/tmp/file-XXXXXX.s"` because the driver depends on them for temporary assembly/object paths.

## Honest M20 Gaps

- Current self-host progress is partial: `compile_self.sh` can build `build/b1cc_self`, and that binary can compile a minimal `return_42` program, but this is not yet a complete self-host milestone.
- General C aggregate assignment is still incomplete; compiler sources avoid some cases by passing containers and IR objects by pointer or copying fields manually.
- Aggregate ABI support remains pragmatic and test-driven. Do not claim full C struct passing/return support until large returns, nested aggregates, and target ABI classification are covered by regression tests.

Skipped: full C/C++ upfront. Add features only when a test or B1NIX source needs them.

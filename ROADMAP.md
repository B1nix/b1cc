# b1cc Roadmap

## M0: Prototype

- [x] Create a clean-room compiler repository.
- [x] Parse `int main(void) { return expression; }`.
- [x] Support integer literals and `+ - * /` precedence.
- [x] Emit Darwin ARM64 assembly for host smoke tests.
- [x] Add a tiny runnable test script.

## M1: C++17 Compiler Core

- [x] Rewrite the prototype in C++17.
- [x] Build with `-std=c++17 -fno-exceptions -fno-rtti`.
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
- [ ] Complete integer promotions, usual arithmetic conversions, and qualifier-aware typing.
- [x] Improve aggregate layout for alignment, padding, nested structs, and `union`.
- [x] Support arrays inside structs and reliable integer/pointer `.`/`->` field access for covered scalar, nested-struct, and array-field cases.
- [x] Add function pointer declarators and calls without special parser shortcuts.

## M15: Driver and ABI Regression Coverage

- [x] Add ABI regression tests for stack-passed arguments, varargs calls, function pointers, and small integer returns.
- [x] Add driver modes expected from a tiny C compiler: `-E`, `-c`, multiple input files, and pass-through linker flags.
- [ ] Make native object output real enough for B1NIX: symbols, relocations, sections, and debug dumps.
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
- [ ] Support full target ABI aggregate classification, including large returns, float/vector aggregates, and bitfields.
- [x] Support designated initializers (e.g., `struct Point p = { .x = 1 }`).
- [x] Support nested brace initializers for multidimensional arrays and structures.

## Honest M14-M18 Gaps

- M14 still lacks complete integer promotions, usual arithmetic conversions, and qualifier-aware typing.
- M15 still does not have a native object/ELF writer; object/ELF production is driver-assisted through host tools or `b1nix-cc`.
- M16 still lacks a written/enforced backend contract for type legalization, calling convention lowering, instruction selection, and register allocation.
- M17 still only covers the tested C99 preprocessor operator subset; obscure expansion/rescan/placemarker edge cases remain future work.
- M18 aggregate ABI support is intentionally limited to covered integer/pointer aggregates; large returns, float/vector aggregates, and bitfields are not complete.

## M19: Complete Type System & Math

- [ ] Support floating-point types (`float`, `double`) with vector/FPU backend instructions and registers.
- [ ] Support `long long` 64-bit integer operations on 32-bit platforms (register pairs on i386).
- [x] Add integer-only C99 typing foundation for scalar `_Bool`, tolerated `const`/`volatile`/`restrict` qualifiers, and usual integer promotions/conversions across current scalar integer types.
- [ ] Complete qualifier semantics beyond parsing/tolerance, including const-correct diagnostics and volatile access semantics.
- [ ] Implement bitfields packing/unpacking in aggregates.

## M20: Callee-Side Varargs & Self-Hosting

- [ ] Add compiler built-ins for writing vararg functions (`__builtin_va_list`, `__builtin_va_start`, etc.).
- [ ] Support compilation of a clean-room C version of the compiler codebase (e.g., pure C variant of `b1cc` or C++ subsets).

Skipped: full C/C++ upfront. Add features only when a test or B1NIX source needs them.

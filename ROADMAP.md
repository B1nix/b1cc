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

## M5: M25-Level C

- [x] Compile hello output.
- [x] Compile argv/file-write checks.
- [x] Compile stderr and exit-status checks.
- [ ] Support enough libc calls for the full M25 smoke shape.
- [ ] Keep TCC as fallback until b1cc passes equivalent tests.

## M6: Object and ELF Output

- [x] Emit relocatable objects or final static ELF.
- [ ] Add symbols, relocations, sections, and simple debug dumps.
- [x] Stop relying on host `cc` for normal B1NIX output.

## M7: Better C

- [ ] Add structs, enums, typedefs, casts, initializers, and arrays.
- [x] Add a small preprocessor path for includes/comments.
- [ ] Add object-like macros when a real test needs them.
- [x] Support varargs calls well enough for `printf`-style declarations.
- [ ] Expand tests from B1NIX userspace sources, not random internet code.

## M8: Self-Hosting Track

- [ ] Compile selected b1cc source files with b1cc.
- [ ] Compile all b1cc C support code, if any.
- [ ] Keep the C++ core host-built until b1cxx exists.

## M9: Optional External Backend

- [ ] Evaluate QBE after the internal IR is stable.
- [ ] Add QBE only if it reduces backend work for real tests.
- [ ] Do not expose QBE details to the frontend.
- [ ] Defer LLVM until optimizer/debug-info/many-target needs are concrete.

## M10: C++ Frontend

- [ ] Start `b1cxx` as a separate frontend using the same IR/backend.
- [ ] Add namespaces, references, classes, methods, constructors, and mangling.
- [ ] Add overloads before templates.
- [ ] Add templates, exceptions, and RTTI only when B1NIX needs them.

Skipped: full C/C++ upfront. Add features only when a test or B1NIX source needs them.

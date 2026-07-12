# Archived i386 backend

The host-supported b1cc targets are now `arm64-darwin` and `x86_64-b1nix`.
i386 support was moved here because the development system has no 32-bit
execution/toolchain path.

Archived material includes:

- `src/backend_i386.c` — the i386 code-generation backend;
- `src/elf_writer_i386.c` — the ELF32/i386 encoder extracted from the native
  ELF writer;
- the i386-only regression sources under `tests/`.

The active compiler no longer advertises or dispatches i386/i686 targets, and
the active test runner no longer executes i386 cases. This is an archive, not a
claim that the i386 implementation was deleted or validated on this host.

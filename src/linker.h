#ifndef LINKER_H
#define LINKER_H

/*
 * linker.h — b1cc internal static linker (M33)
 *
 * Combines b1cc-native ET_REL objects + crt0.o + libc archive members into a
 * final statically-linked ET_EXEC, entirely in-process — no external ld / ld.lld.
 * This is what lets /bin/b1cc produce runnable executables on B1NIX, where there
 * is no host linker (ADR M33: option (a), an internal linker).
 *
 * Covered (x86_64):
 *   - ELF64 relocatable object parsing (.text/.rodata/.data/.bss + friends)
 *   - System V archive (.a) parsing with transitive member resolution
 *   - Symbol resolution (global/weak; weak-undefined -> 0)
 *   - Linker-defined __init_array_start / __init_array_end
 *   - COMMON symbol allocation into .bss
 *   - Relocations: R_X86_64_64 / PC32 / PLT32 / 32 / 32S
 *   - ET_EXEC emission honoring the B1NIX linker.ld layout (base 0x2000000)
 *
 * Not covered yet:
 *   - i386 / ELF32, dynamic (.so / PIE), GOT/PLT, TLS (PT_TLS)
 */

#include "common.h"
#include <stdint.h>

typedef enum {
    LINK_STATIC_EXE = 0, /* ET_EXEC, fold archives, base 0x2000000 */
    LINK_PIE,            /* ET_DYN executable, imports from a shared libc, base 0 */
    LINK_SHARED,         /* ET_DYN shared object (.so), exports its globals */
} LinkMode;

typedef struct {
    const char **inputs;   /* ordered object paths (crt0 first, then user objs) */
    int          n_inputs;
    const char **archives; /* static archives searched for undefined symbols (static mode) */
    int          n_archives;
    const char  *out_path;
    uint64_t     base_va;  /* load base (0x2000000 static; 0 for PIE/shared) */
    const char  *entry;    /* entry symbol name, e.g. "_start" */
    LinkMode     mode;
    const char **needed;   /* DT_NEEDED shared libs (PIE), e.g. {"libc.so.1"} */
    int          n_needed;
    const char  *soname;   /* DT_SONAME for LINK_SHARED */
} LinkRequest;

/* Returns 0 on success; nonzero after emitting a diagnostic on failure.
 * Dispatches on req->mode. */
int elf_link_static(const LinkRequest *req, Arena *arena);
int elf_link(const LinkRequest *req, Arena *arena);

#endif /* LINKER_H */

#ifndef ELF_WRITER_H
#define ELF_WRITER_H

/*
 * elf_writer.h — Native ELF object writer for b1cc (M15)
 *
 * Takes GNU AS-compatible text assembly (as produced by b1cc backends)
 * and emits a relocatable ELF64 (x86_64) or ELF32 (i386) object file
 * directly in memory, without invoking any external assembler.
 *
 * Covered:
 *   - ELF64 relocatable for x86_64-b1nix
 *   - ELF32 relocatable for i386-b1nix
 *   - Sections: .text, .data, .rodata, .bss, .symtab, .strtab, .shstrtab
 *   - Relocations: .rela.text (x86_64) / .rel.text (i386)
 *   - Symbol table with global/local function and object symbols
 *   - Debug line info (.file / .loc → DWARF .debug_line stub)
 *
 * Not covered (future work):
 *   - arm64-darwin Mach-O (uses host cc)
 *   - Full DWARF debug info
 *   - Linker scripts / section merging
 */

#include "common.h"
#include <stdint.h>
#include <stddef.h>

/* Result of elf_write_object(): raw ELF bytes in arena memory */
typedef struct {
    uint8_t *data;
    size_t   size;
} ElfObject;

/*
 * elf_write_object() — assemble GNU AS text into a relocatable ELF object.
 *
 * Parameters:
 *   asm_text  — NUL-terminated GNU AS source text
 *   target    — "x86_64-b1nix" or "i386-b1nix"
 *   src_path  — original source file path (for .file directive / error messages)
 *   arena     — arena for all allocations; caller owns lifetime
 *
 * Returns ElfObject with data==NULL and size==0 on unsupported target.
 * Calls diagnostics_fatal() on encoding errors.
 */
ElfObject elf_write_object(const char *asm_text, const char *target,
                           const char *src_path, Arena *arena);

#endif /* ELF_WRITER_H */

#ifndef MACHO_WRITER_H
#define MACHO_WRITER_H

#include "common.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *data;
    size_t   size;
} MachObject;

MachObject macho_write_object(const char *asm_text, const char *src_path, Arena *arena);

#endif /* MACHO_WRITER_H */

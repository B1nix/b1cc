#ifndef BACKEND_H
#define BACKEND_H

#include "ir.h"
#include "common.h"

const char *backend_compile_asm(const char *src, const char *target, const char *mcmodel, bool dump_ast, bool dump_ir, Arena *arena);

#endif // BACKEND_H

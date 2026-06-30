#ifndef BACKEND_TARGET_H
#define BACKEND_TARGET_H

#include "ir.h"
#include "common.h"

typedef struct TargetBackend TargetBackend;
#ifdef __b1cc__
struct TargetBackend {
    void *emit_function;
    void *emit_globals;
    void *free;
};
#else
struct TargetBackend {
    const char *(*emit_function)(TargetBackend *self, const IrFunction *fn, Arena *arena);
    const char *(*emit_globals)(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena);
    void (*free)(TargetBackend *self);
};
#endif

TargetBackend* backend_create_arm64(void);
TargetBackend* backend_create_x86_64(void);
TargetBackend* backend_create_i386(void);

#endif // BACKEND_TARGET_H

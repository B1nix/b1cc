#ifndef BACKEND_TARGET_H
#define BACKEND_TARGET_H

#include "ir.h"
#include "common.h"

/*
 * M16 Backend Contract
 * ====================
 * Every target backend must implement the following vtable.  The contract
 * covers four compiler phases:
 *
 *  1. Type legalization  – legalize_type_size
 *  2. Calling convention – get_cc_info / get_int_arg_reg / get_return_reg
 *  3. Instruction info   – get_int_reg_name  (scratch / temp register names)
 *  4. Register allocation– get_int_reg_name + get_target_scale + get_stack_slot_size
 *
 * Existing hooks (emit_function, emit_globals, free, get_target_scale,
 * get_stack_slot_size, get_aggregate_slots) are preserved unchanged.
 */

/* Calling-convention descriptor returned by get_cc_info(). */
typedef struct {
    /* Names of integer argument registers in order (NULL-terminated). */
    const char *const *int_arg_regs;
    /* Number of registers in int_arg_regs. */
    int int_arg_reg_count;
    /* Name of the primary integer return register (e.g. "x0", "rax", "eax"). */
    const char *return_reg;
    /* Name of a caller-saved scratch/temp register safe to clobber (e.g. "x9", "r10", "ecx"). */
    const char *scratch_reg;
    /* Byte alignment required for the stack frame on this target. */
    int stack_align;
} BackendCCInfo;

typedef struct TargetBackend TargetBackend;
#ifdef __b1cc__
struct TargetBackend {
    void *emit_function;
    void *emit_globals;
    void *free;
    void *get_target_scale;
    void *get_stack_slot_size;
    void *get_aggregate_slots;
    /* M16 additions – kept as void* for self-host compatibility */
    void *legalize_type_size;
    void *get_cc_info;
    void *get_int_reg_name;
    void *get_return_reg;
};
#else
struct TargetBackend {
    /* ── Emission ─────────────────────────────────────────────────────── */
    const char *(*emit_function)(TargetBackend *self, const IrFunction *fn, Arena *arena);
    const char *(*emit_globals)(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena);
    void (*free)(TargetBackend *self);

    /* ── Layout / sizes ───────────────────────────────────────────────── */
    /** Pointer size in bytes for the target (4 or 8). */
    int (*get_target_scale)(TargetBackend *self);
    /** Byte size of one stack slot (alignment unit for locals). */
    int (*get_stack_slot_size)(TargetBackend *self);
    /** Number of stack slots required to hold an aggregate of `size` bytes. */
    int (*get_aggregate_slots)(TargetBackend *self, int size);

    /* ── M16: Type legalization ───────────────────────────────────────── */
    /**
     * Map an abstract byte width to the nearest legal integer width
     * supported by this target's load/store instructions.
     * e.g. width=3 → 4 on all current targets.
     * Returns one of: 1, 2, 4, or 8.
     */
    int (*legalize_type_size)(TargetBackend *self, int width);

    /* ── M16: Calling convention ──────────────────────────────────────── */
    /**
     * Fill *out with the calling-convention descriptor for this target.
     * The descriptor is target-owned static data; caller must not free it.
     */
    void (*get_cc_info)(TargetBackend *self, BackendCCInfo *out);

    /* ── M16: Register file / instruction selection info ─────────────── */
    /**
     * Return the assembly name of the n-th general-purpose integer register
     * in the target's natural word size (0-indexed).  Returns NULL when n
     * is out of range.
     * Used by the IR→assembly lowering layer to make register naming
     * target-independent.
     */
    const char *(*get_int_reg_name)(TargetBackend *self, int n);

    /**
     * Return the assembly name of the register used to return integer/pointer
     * values from functions on this target.
     */
    const char *(*get_return_reg)(TargetBackend *self);
};
#endif

TargetBackend* backend_create_arm64(void);
TargetBackend* backend_create_x86_64(void);

#endif // BACKEND_TARGET_H

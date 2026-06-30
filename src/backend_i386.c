#include "backend_target.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    TargetBackend base;
} I386Target;

static const char *i386_emit_globals(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena) {
    (void)self;
    if (globals->count == 0)
        return "";

    StringBuilder out;
    sb_init(&out);
    sb_append(&out, ".data\n");

    for (int i = 0; i < globals->count; ++i) {
        const IrGlobalVar *g = &globals->data[i];
        if (g->is_extern && g->initializers.count == 0) {
            continue;
        }
        if (!g->is_static) {
            sb_appendf(&out, ".globl %s\n", g->name);
        }
        int i386_align = g->align;
        if (i386_align == 0) {
            i386_align = (g->elem_size >= 4) ? 2 : (g->elem_size == 2) ? 1 : 0;
        }
        sb_appendf(&out, ".type %s, @object\n", g->name);
        long total_bytes = g->is_array ? (g->size * g->elem_size) : ((g->initializers.count == 0 ? 1 : g->initializers.count) * g->elem_size);
        sb_appendf(&out, ".size %s, %ld\n", g->name, total_bytes);
        sb_appendf(&out, ".p2align %d\n", i386_align);
        sb_appendf(&out, "%s:\n", g->name);

        if (g->is_array || g->initializers.count > 1) {
            for (int k = 0; k < g->initializers.count; ++k) {
                long val = g->initializers.data[k];
                if (g->elem_size == 1)
                    sb_appendf(&out, "    .byte %d\n", (int)(val & 0xff));
                else if (g->elem_size == 2)
                    sb_appendf(&out, "    .short %d\n", (int)(val & 0xffff));
                else if (g->elem_size == 4)
                    sb_appendf(&out, "    .long %ld\n", val);
                else
                    sb_appendf(&out, "    .quad %ld\n", val);
            }
            long remaining = g->size - g->initializers.count;
            if (remaining > 0) {
                sb_appendf(&out, "    .zero %ld\n", remaining * g->elem_size);
            }
        } else {
            long val = (g->initializers.count == 0) ? 0 : g->initializers.data[0];
            if (g->elem_size == 1)
                sb_appendf(&out, "    .byte %d\n", (int)(val & 0xff));
            else if (g->elem_size == 2)
                sb_appendf(&out, "    .short %d\n", (int)(val & 0xffff));
            else if (g->elem_size == 4)
                sb_appendf(&out, "    .long %ld\n", val);
            else
                sb_appendf(&out, "    .quad %ld\n", val);
        }
    }
    sb_append(&out, "\n");

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static const char *i386_emit_function(TargetBackend *self, const IrFunction *fn, Arena *arena) {
    (void)self;
    StringBuilder out;
    sb_init(&out);

    if (fn->strings.count > 0) {
        sb_append(&out, ".section .rodata\n");
        for (int i = 0; i < fn->strings.count; ++i) {
            sb_appendf(&out, "%s:\n    .asciz \"%s\"\n", fn->strings.data[i].first, fn->strings.data[i].second);
        }
    }
    sb_append(&out, ".text\n");
    if (!fn->is_static) {
        sb_appendf(&out, ".globl %s\n", fn->name);
    }
    sb_appendf(&out, ".type %s, @function\n", fn->name);
    sb_appendf(&out, "%s:\n", fn->name);

    int frame = fn->has_call || (fn->locals.size > 0);
    if (frame) {
        sb_append(&out, "    pushl %ebp\n");
        sb_append(&out, "    movl %esp, %ebp\n");
        if (fn->locals.size > 0) {
            sb_appendf(&out, "    subl $%d, %esp\n", fn->locals.size * 16);
        }
        for (int i = 0; i < fn->params.count; ++i) {
            sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", 8 + i * 4);
            sb_appendf(&out, "    movl %%eax, -%d(%%ebp)\n", (i + 1) * 16);
        }
    }

    int last_loc_line = -1;
    int last_loc_col = -1;
    for (int i_i = 0; i_i < fn->code.count; ++i_i) {
        const IrInst *inst = &fn->code.data[i_i];
        if (inst->line > 0 && (inst->line != last_loc_line || inst->col != last_loc_col)) {
            sb_appendf(&out, "    .loc 1 %d %d\n", inst->line, inst->col);
            last_loc_line = inst->line;
            last_loc_col = inst->col;
        }
        if (strcmp(inst->op, "const") == 0) {
            sb_appendf(&out, "    movl $%ld, %%eax\n", inst->value);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "load") == 0) {
            sb_appendf(&out, "    movl -%ld(%%ebp), %%eax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "str") == 0) {
            sb_appendf(&out, "    movl $%s, %%eax\n", inst->arg);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "store") == 0) {
            sb_append(&out, "    popl %eax\n");
            sb_appendf(&out, "    movl %%eax, -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (strcmp(inst->op, "dup") == 0) {
            sb_append(&out, "    movl (%esp), %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "pop") == 0) {
            sb_append(&out, "    addl $4, %esp\n");
        } else if (strcmp(inst->op, "+") == 0 || strcmp(inst->op, "-") == 0 || strcmp(inst->op, "*") == 0 ||
                   strcmp(inst->op, "/") == 0 || strcmp(inst->op, "%") == 0 || strcmp(inst->op, "==") == 0 || strcmp(inst->op, "!=") == 0 ||
                   strcmp(inst->op, "<") == 0 || strcmp(inst->op, ">") == 0 || strcmp(inst->op, "<=") == 0 ||
                   strcmp(inst->op, "u<") == 0 || strcmp(inst->op, "u>") == 0 || strcmp(inst->op, "u<=") == 0 ||
                   strcmp(inst->op, "u>=") == 0 || strcmp(inst->op, "u>>") == 0 ||
                   strcmp(inst->op, ">=") == 0 || strcmp(inst->op, "index") == 0 ||
                   strcmp(inst->op, "&") == 0 || strcmp(inst->op, "|") == 0 || strcmp(inst->op, "^") == 0 ||
                   strcmp(inst->op, "<<") == 0 || strcmp(inst->op, ">>") == 0) {
            
            sb_append(&out, "    popl %ecx\n");
            sb_append(&out, "    popl %eax\n");
            if (strcmp(inst->op, "index") == 0) {
                if (inst->value == 1) {
                    sb_append(&out, "    movsbl (%eax,%ecx,1), %eax\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    movswl (%eax,%ecx,2), %eax\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    movl (%eax,%ecx,4), %eax\n");
                } else {
                    sb_append(&out, "    movl (%eax,%ecx,8), %eax\n");
                }
            } else if (strcmp(inst->op, "+") == 0)
                sb_append(&out, "    addl %ecx, %eax\n");
            else if (strcmp(inst->op, "-") == 0)
                sb_append(&out, "    subl %ecx, %eax\n");
            else if (strcmp(inst->op, "*") == 0)
                sb_append(&out, "    imull %ecx, %eax\n");
            else if (strcmp(inst->op, "/") == 0) {
                sb_append(&out, "    cltd\n");
                sb_append(&out, "    idivl %ecx\n");
            } else if (strcmp(inst->op, "%") == 0) {
                sb_append(&out, "    cltd\n");
                sb_append(&out, "    idivl %ecx\n");
                sb_append(&out, "    movl %edx, %eax\n");
            } else if (strcmp(inst->op, "&") == 0)
                sb_append(&out, "    andl %ecx, %eax\n");
            else if (strcmp(inst->op, "|") == 0)
                sb_append(&out, "    orl %ecx, %eax\n");
            else if (strcmp(inst->op, "^") == 0)
                sb_append(&out, "    xorl %ecx, %eax\n");
            else if (strcmp(inst->op, "<<") == 0 || strcmp(inst->op, ">>") == 0 || strcmp(inst->op, "u>>") == 0) {
                if (strcmp(inst->op, "<<") == 0)
                    sb_append(&out, "    shll %cl, %eax\n");
                else if (strcmp(inst->op, ">>") == 0)
                    sb_append(&out, "    sarl %cl, %eax\n");
                else
                    sb_append(&out, "    shrl %cl, %eax\n");
            } else {
                sb_append(&out, "    cmpl %ecx, %eax\n");
                if (strcmp(inst->op, "==") == 0)
                    sb_append(&out, "    sete %al\n");
                else if (strcmp(inst->op, "!=") == 0)
                    sb_append(&out, "    setne %al\n");
                else if (strcmp(inst->op, "<") == 0)
                    sb_append(&out, "    setl %al\n");
                else if (strcmp(inst->op, ">") == 0)
                    sb_append(&out, "    setg %al\n");
                else if (strcmp(inst->op, "<=") == 0)
                    sb_append(&out, "    setle %al\n");
                else if (strcmp(inst->op, ">=") == 0)
                    sb_append(&out, "    setge %al\n");
                else if (strcmp(inst->op, "u<") == 0)
                    sb_append(&out, "    setb %al\n");
                else if (strcmp(inst->op, "u>") == 0)
                    sb_append(&out, "    seta %al\n");
                else if (strcmp(inst->op, "u<=") == 0)
                    sb_append(&out, "    setbe %al\n");
                else
                    sb_append(&out, "    setae %al\n");
                sb_append(&out, "    movzbl %al, %eax\n");
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "~") == 0 || strcmp(inst->op, "!") == 0 || strcmp(inst->op, "neg") == 0 || strcmp(inst->op, "cast") == 0) {
            sb_append(&out, "    popl %eax\n");
            if (strcmp(inst->op, "~") == 0)
                sb_append(&out, "    notl %eax\n");
            else if (strcmp(inst->op, "neg") == 0)
                sb_append(&out, "    negl %eax\n");
            else if (strcmp(inst->op, "cast") == 0) {
                if (inst->value == 1)
                    sb_append(&out, "    movsbl %al, %eax\n");
                else if (inst->value == 2)
                    sb_append(&out, "    movswl %ax, %eax\n");
            } else {
                sb_append(&out, "    cmpl $0, %eax\n");
                sb_append(&out, "    sete %al\n");
                sb_append(&out, "    movzbl %al, %eax\n");
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "jz") == 0) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    cmpl $0, %eax\n");
            sb_appendf(&out, "    je %s\n", inst->arg);
        } else if (strcmp(inst->op, "jmp") == 0) {
            sb_appendf(&out, "    jmp %s\n", inst->arg);
        } else if (strcmp(inst->op, "label") == 0) {
            sb_appendf(&out, "%s:\n", inst->arg);
        } else if (strcmp(inst->op, "call") == 0) {
            long num_args = inst->value;
            sb_appendf(&out, "    subl $%ld, %%esp\n", num_args * 4);
            for (long i = 0; i < num_args; ++i) {
                sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args - 1 - i) * 4);
                sb_appendf(&out, "    movl %%eax, %ld(%%esp)\n", i * 4);
            }
            sb_appendf(&out, "    call %s\n", inst->arg);
            sb_appendf(&out, "    addl $%ld, %%esp\n", 2 * num_args * 4);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "icall") == 0) {
            long num_args = inst->value;
            sb_appendf(&out, "    subl $%ld, %%esp\n", num_args * 4);
            for (long i = 0; i < num_args; ++i) {
                sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args - 1 - i) * 4);
                sb_appendf(&out, "    movl %%eax, %ld(%%esp)\n", i * 4);
            }
            sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args) * 4);
            sb_append(&out, "    call *%eax\n");
            sb_appendf(&out, "    addl $%ld, %%esp\n", (2 * num_args + 1) * 4);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "addr") == 0) {
            sb_appendf(&out, "    leal -%ld(%%ebp), %%eax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "gload") == 0) {
            int gsize = 4;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;
            if (gsize == 1)
                sb_appendf(&out, "    movsbl %s, %%eax\n", inst->arg);
            else if (gsize == 2)
                sb_appendf(&out, "    movswl %s, %%eax\n", inst->arg);
            else
                sb_appendf(&out, "    movl %s, %%eax\n", inst->arg);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "gstore") == 0) {
            sb_append(&out, "    popl %eax\n");
            int gsize = 4;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;
            if (gsize == 1)
                sb_appendf(&out, "    movb %%al, %s\n", inst->arg);
            else if (gsize == 2)
                sb_appendf(&out, "    movw %%ax, %s\n", inst->arg);
            else
                sb_appendf(&out, "    movl %%eax, %s\n", inst->arg);
        } else if (strcmp(inst->op, "gaddr") == 0) {
            sb_appendf(&out, "    movl $%s, %%eax\n", inst->arg);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "store_index") == 0) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %ecx\n");
            sb_append(&out, "    popl %edx\n");
            if (inst->value == 1) {
                sb_append(&out, "    movb %dl, (%eax,%ecx,1)\n");
            } else if (inst->value == 2) {
                sb_append(&out, "    movw %dx, (%eax,%ecx,2)\n");
            } else if (inst->value == 4) {
                sb_append(&out, "    movl %edx, (%eax,%ecx,4)\n");
            } else {
                sb_append(&out, "    movl %edx, (%eax,%ecx,8)\n");
            }
        } else if (strcmp(inst->op, "ret") == 0) {
            sb_append(&out, "    popl %eax\n");
            if (frame)
                sb_append(&out, "    leave\n");
            sb_append(&out, "    ret\n");
        } else if (strcmp(inst->op, "extract_bits") == 0) {
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    popl %eax\n");
            if (bf_offset > 0) {
                sb_appendf(&out, "    shrl $%d, %%eax\n", bf_offset);
            }
            long mask = (bf_width < 32) ? ((1L << bf_width) - 1) : 0xFFFFFFFFL;
            sb_appendf(&out, "    andl $%ld, %%eax\n", mask);
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "insert_bits") == 0) {
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            long mask = (bf_width < 32) ? ((1L << bf_width) - 1) : 0xFFFFFFFFL;
            sb_append(&out, "    popl %edi\n");  /* dest_addr */
            sb_append(&out, "    popl %esi\n");  /* index (==0) */
            sb_append(&out, "    popl %eax\n");  /* new value */
            sb_appendf(&out, "    andl $%ld, %%eax\n", mask);
            if (bf_offset > 0) {
                sb_appendf(&out, "    shll $%d, %%eax\n", bf_offset);
            }
            sb_append(&out, "    movl (%edi), %ecx\n");
            sb_appendf(&out, "    andl $%ld, %%ecx\n", (long)(int)(~(unsigned int)(mask << bf_offset)));
            sb_append(&out, "    orl %eax, %ecx\n");
            sb_append(&out, "    movl %ecx, (%edi)\n");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown IR op %s", inst->op);
            diagnostics_fatal(msg);
        }
    }
    int has_explicit_ret = 0;
    if (fn->code.count > 0) {
        const char *last_op = fn->code.data[fn->code.count - 1].op;
        has_explicit_ret = strcmp(last_op, "ret") == 0 || strcmp(last_op, "ret_agg") == 0;
    }
    if (!has_explicit_ret) {
        if (frame)
            sb_append(&out, "    leave\n");
        sb_append(&out, "    ret\n");
    }
    sb_appendf(&out, ".size %s, .-%s\n", fn->name, fn->name);

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static void i386_free(TargetBackend *self) {
    free(self);
}

static int i386_get_target_scale(TargetBackend *self) {
    (void)self;
    return 4;
}

static int i386_get_stack_slot_size(TargetBackend *self) {
    (void)self;
    return 16;
}

static int i386_get_aggregate_slots(TargetBackend *self, int size) {
    (void)self;
    return (size + 15) / 16;
}

/* M16: type legalization – i386 supports 1/2/4/8-byte loads (8 via FILD/MOVQ). */
static int i386_legalize_type_size(TargetBackend *self, int width) {
    (void)self;
    if (width <= 1) return 1;
    if (width <= 2) return 2;
    if (width <= 4) return 4;
    return 8;  /* 64-bit via register pair or FPU */
}

/* M16: calling convention – cdecl (all args passed on stack). */
static void i386_get_cc_info(TargetBackend *self, BackendCCInfo *out) {
    (void)self;
    /* cdecl passes no integer arguments in registers */
    out->int_arg_regs      = nullptr;
    out->int_arg_reg_count = 0;
    out->return_reg        = "%eax";
    out->scratch_reg       = "%ecx";
    out->stack_align       = 4;
}

/* M16: general-purpose integer register names (32-bit forms). */
static const char *i386_get_int_reg_name(TargetBackend *self, int n) {
    (void)self;
    static const char *const names[] = {
        "%eax", "%ecx", "%edx", "%ebx",
        "%esi", "%edi"
    };
    if (n < 0 || n >= 6) return nullptr;
    return names[n];
}

/* M16: return register. */
static const char *i386_get_return_reg(TargetBackend *self) {
    (void)self;
    return "%eax";
}

TargetBackend* backend_create_i386(void) {
    I386Target *b = malloc(sizeof(I386Target));
    b->base.emit_globals       = i386_emit_globals;
    b->base.emit_function      = i386_emit_function;
    b->base.free               = i386_free;
    b->base.get_target_scale   = i386_get_target_scale;
    b->base.get_stack_slot_size  = i386_get_stack_slot_size;
    b->base.get_aggregate_slots  = i386_get_aggregate_slots;
    /* M16 contract hooks */
    b->base.legalize_type_size = i386_legalize_type_size;
    b->base.get_cc_info        = i386_get_cc_info;
    b->base.get_int_reg_name   = i386_get_int_reg_name;
    b->base.get_return_reg     = i386_get_return_reg;
    return &b->base;
}

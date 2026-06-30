#include "backend_target.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_sub_imm(StringBuilder *out, const char *dst, const char *base, long imm) {
    if (imm >= 0 && imm <= 4095) {
        sb_appendf(out, "    sub %s, %s, #%ld\n", dst, base, imm);
    } else {
        sb_appendf(out, "    mov x10, #%ld\n", imm);
        sb_appendf(out, "    sub %s, %s, x10\n", dst, base);
    }
}

static void emit_store_fp(StringBuilder *out, int reg, int offset) {
    if (offset < -256) {
        emit_sub_imm(out, "x9", "x29", -offset);
        sb_appendf(out, "    str x%d, [x9]\n", reg);
    } else if (offset < 0) {
        sb_appendf(out, "    stur x%d, [x29, #%d]\n", reg, offset);
    } else if (offset > 4095 * 8) {
        sb_appendf(out, "    mov x9, #%d\n", offset);
        sb_appendf(out, "    str x%d, [x29, x9]\n", reg);
    } else {
        sb_appendf(out, "    str x%d, [x29, #%d]\n", reg, offset);
    }
}

static void emit_load_fp(StringBuilder *out, int reg, int offset) {
    if (offset < -256) {
        emit_sub_imm(out, "x9", "x29", -offset);
        sb_appendf(out, "    ldr x%d, [x9]\n", reg);
    } else if (offset < 0) {
        sb_appendf(out, "    ldur x%d, [x29, #%d]\n", reg, offset);
    } else if (offset > 4095 * 8) {
        sb_appendf(out, "    mov x9, #%d\n", offset);
        sb_appendf(out, "    ldr x%d, [x29, x9]\n", reg);
    } else {
        sb_appendf(out, "    ldr x%d, [x29, #%d]\n", reg, offset);
    }
}

static void emit_block_copy(StringBuilder *out, const char *src_reg, const char *dest_reg, int size) {
    int offset = 0;
    while (size >= 8) {
        sb_appendf(out, "    ldr x11, [%s, #%d]\n", src_reg, offset);
        sb_appendf(out, "    str x11, [%s, #%d]\n", dest_reg, offset);
        offset += 8;
        size -= 8;
    }
    if (size >= 4) {
        sb_appendf(out, "    ldr w11, [%s, #%d]\n", src_reg, offset);
        sb_appendf(out, "    str w11, [%s, #%d]\n", dest_reg, offset);
        offset += 4;
        size -= 4;
    }
    if (size >= 2) {
        sb_appendf(out, "    ldrh w11, [%s, #%d]\n", src_reg, offset);
        sb_appendf(out, "    strh w11, [%s, #%d]\n", dest_reg, offset);
        offset += 2;
        size -= 2;
    }
    if (size >= 1) {
        sb_appendf(out, "    ldrb w11, [%s, #%d]\n", src_reg, offset);
        sb_appendf(out, "    strb w11, [%s, #%d]\n", dest_reg, offset);
        offset += 1;
        size -= 1;
    }
}

typedef struct {
    TargetBackend base;
} Arm64Target;

static int is_defined_global(const char *name) {
    for (int i = 0; i < ir_global_decls.count; ++i) {
        if (strcmp(ir_global_decls.data[i].name, name) == 0) {
            const IrGlobalVar *g = &ir_global_decls.data[i];
            return !(g->is_extern && g->initializers.count == 0);
        }
    }
    return 0;
}

static const char *arm64_emit_globals(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena) {
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
            sb_appendf(&out, ".globl _%s\n", g->name);
        }
        int align = g->align;
        if (align == 0) {
            align = (g->elem_size == 8) ? 3 : (g->elem_size == 4) ? 2 : (g->elem_size == 2) ? 1 : 0;
        }
        sb_appendf(&out, ".p2align %d\n", align);
        sb_appendf(&out, "_%s:\n", g->name);

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

static const char *arm64_emit_function(TargetBackend *self, const IrFunction *fn, Arena *arena) {
    (void)self;
    StringBuilder out;
    sb_init(&out);

    if (fn->strings.count > 0) {
        sb_append(&out, ".cstring\n");
        for (int i = 0; i < fn->strings.count; ++i) {
            sb_appendf(&out, "%s:\n    .asciz \"%s\"\n", fn->strings.data[i].first, fn->strings.data[i].second);
        }
    }
    sb_append(&out, ".text\n");
    if (!fn->is_static) {
        sb_appendf(&out, ".globl _%s\n", fn->name);
    }
    sb_appendf(&out, ".p2align 2\n_%s:\n", fn->name);

    int frame = fn->has_call || (fn->locals.size > 0);
    if (frame) {
        sb_append(&out, "    stp x29, x30, [sp, #-16]!\n");
        sb_append(&out, "    mov x29, sp\n");
        if (fn->locals.size > 0) {
            emit_sub_imm(&out, "sp", "sp", fn->locals.size * 16);
        }
        int abi_word = 0;
        int stack_word = 0;
        for (int i = 0; i < fn->params.count; ++i) {
            int agg_size = (i < fn->param_aggregate_sizes.count) ? fn->param_aggregate_sizes.data[i] : 0;
            int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
            int num_slots = (agg_size > 0) ? ((agg_size + 15) / 16) : 1;
            int in_regs = (abi_word + words <= 8);
            
            HashMapEntry *entry = hashmap_get((HashMap *)&fn->locals, fn->params.data[i]);
            int slot_idx = entry ? entry->val_int : i;
            
            for (int w = 0; w < words; ++w) {
                int off = -((slot_idx + num_slots) * 16) + (w * 8);
                if (in_regs) {
                    emit_store_fp(&out, abi_word + w, off);
                } else {
                    emit_load_fp(&out, 8, 16 + stack_word * 8);
                    emit_store_fp(&out, 8, off);
                    stack_word++;
                }
            }
            if (in_regs) {
                abi_word += words;
            }
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
            if (inst->value >= 0 && inst->value <= 65535) {
                sb_appendf(&out, "    mov x0, #%ld\n", inst->value);
            } else {
                unsigned long long val = inst->value;
                sb_appendf(&out, "    movz x0, #%d\n", (int)(val & 0xffff));
                if (val & 0xffff0000ULL)
                    sb_appendf(&out, "    movk x0, #%d, lsl #16\n", (int)((val >> 16) & 0xffff));
                if (val & 0xffff00000000ULL)
                    sb_appendf(&out, "    movk x0, #%d, lsl #32\n", (int)((val >> 32) & 0xffff));
                if (val & 0xffff000000000000ULL)
                    sb_appendf(&out, "    movk x0, #%d, lsl #48\n", (int)((val >> 48) & 0xffff));
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "load") == 0) {
            emit_load_fp(&out, 0, -((inst->value + 1) * 16));
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "str") == 0) {
            sb_appendf(&out, "    adrp x0, %s@PAGE\n", inst->arg);
            sb_appendf(&out, "    add x0, x0, %s@PAGEOFF\n", inst->arg);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "store") == 0) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            emit_store_fp(&out, 0, -((inst->value + 1) * 16));
        } else if (strcmp(inst->op, "dup") == 0) {
            sb_append(&out, "    ldr x0, [sp]\n");
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "pop") == 0) {
            sb_append(&out, "    add sp, sp, #16\n");
        } else if (strcmp(inst->op, "+") == 0 || strcmp(inst->op, "-") == 0 || strcmp(inst->op, "*") == 0 ||
                   strcmp(inst->op, "/") == 0 || strcmp(inst->op, "%") == 0 || strcmp(inst->op, "==") == 0 || strcmp(inst->op, "!=") == 0 ||
                   strcmp(inst->op, "<") == 0 || strcmp(inst->op, ">") == 0 || strcmp(inst->op, "<=") == 0 ||
                   strcmp(inst->op, "u<") == 0 || strcmp(inst->op, "u>") == 0 || strcmp(inst->op, "u<=") == 0 ||
                   strcmp(inst->op, "u>=") == 0 || strcmp(inst->op, "u>>") == 0 ||
                   strcmp(inst->op, ">=") == 0 || strcmp(inst->op, "index") == 0 ||
                   strcmp(inst->op, "&") == 0 || strcmp(inst->op, "|") == 0 || strcmp(inst->op, "^") == 0 ||
                   strcmp(inst->op, "<<") == 0 || strcmp(inst->op, ">>") == 0) {
            
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    ldr x1, [sp], #16\n");
            if (strcmp(inst->op, "index") == 0) {
                if (inst->value == 1) {
                    sb_append(&out, "    ldrsb x0, [x1, x0]\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    ldrsh x0, [x1, x0, lsl #1]\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    ldrsw x0, [x1, x0, lsl #2]\n");
                } else {
                    sb_append(&out, "    ldr x0, [x1, x0, lsl #3]\n");
                }
            } else if (strcmp(inst->op, "+") == 0)
                sb_append(&out, "    add x0, x1, x0\n");
            else if (strcmp(inst->op, "-") == 0)
                sb_append(&out, "    sub x0, x1, x0\n");
            else if (strcmp(inst->op, "*") == 0)
                sb_append(&out, "    mul x0, x1, x0\n");
            else if (strcmp(inst->op, "/") == 0)
                sb_append(&out, "    sdiv x0, x1, x0\n");
            else if (strcmp(inst->op, "%") == 0) {
                sb_append(&out, "    sdiv x2, x1, x0\n");
                sb_append(&out, "    msub x0, x2, x0, x1\n");
            }
            else if (strcmp(inst->op, "&") == 0)
                sb_append(&out, "    and x0, x1, x0\n");
            else if (strcmp(inst->op, "|") == 0)
                sb_append(&out, "    orr x0, x1, x0\n");
            else if (strcmp(inst->op, "^") == 0)
                sb_append(&out, "    eor x0, x1, x0\n");
            else if (strcmp(inst->op, "<<") == 0)
                sb_append(&out, "    lsl x0, x1, x0\n");
            else if (strcmp(inst->op, ">>") == 0)
                sb_append(&out, "    asr x0, x1, x0\n");
            else if (strcmp(inst->op, "u>>") == 0)
                sb_append(&out, "    lsr x0, x1, x0\n");
            else {
                sb_append(&out, "    cmp x1, x0\n");
                if (strcmp(inst->op, "==") == 0)
                    sb_append(&out, "    cset x0, eq\n");
                else if (strcmp(inst->op, "!=") == 0)
                    sb_append(&out, "    cset x0, ne\n");
                else if (strcmp(inst->op, "<") == 0)
                    sb_append(&out, "    cset x0, lt\n");
                else if (strcmp(inst->op, ">") == 0)
                    sb_append(&out, "    cset x0, gt\n");
                else if (strcmp(inst->op, "<=") == 0)
                    sb_append(&out, "    cset x0, le\n");
                else if (strcmp(inst->op, ">=") == 0)
                    sb_append(&out, "    cset x0, ge\n");
                else if (strcmp(inst->op, "u<") == 0)
                    sb_append(&out, "    cset x0, lo\n");
                else if (strcmp(inst->op, "u>") == 0)
                    sb_append(&out, "    cset x0, hi\n");
                else if (strcmp(inst->op, "u<=") == 0)
                    sb_append(&out, "    cset x0, ls\n");
                else
                    sb_append(&out, "    cset x0, hs\n");
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "~") == 0 || strcmp(inst->op, "!") == 0 || strcmp(inst->op, "neg") == 0 || strcmp(inst->op, "cast") == 0) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (strcmp(inst->op, "~") == 0)
                sb_append(&out, "    mvn x0, x0\n");
            else if (strcmp(inst->op, "neg") == 0)
                sb_append(&out, "    neg x0, x0\n");
            else if (strcmp(inst->op, "cast") == 0) {
                if (inst->value == 1)
                    sb_append(&out, "    sxtb x0, w0\n");
                else if (inst->value == 2)
                    sb_append(&out, "    sxth x0, w0\n");
                else if (inst->value == 4)
                    sb_append(&out, "    sxtw x0, w0\n");
            } else {
                sb_append(&out, "    cmp x0, #0\n");
                sb_append(&out, "    cset x0, eq\n");
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "jz") == 0) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    cmp x0, #0\n");
            sb_appendf(&out, "    b.eq %s\n", inst->arg);
        } else if (strcmp(inst->op, "jmp") == 0) {
            sb_appendf(&out, "    b %s\n", inst->arg);
        } else if (strcmp(inst->op, "label") == 0) {
            sb_appendf(&out, "%s:\n", inst->arg);
        } else if (strcmp(inst->op, "call") == 0 || strcmp(inst->op, "icall") == 0) {
            long num_args = inst->value;
            IntArray *agg_sizes = nullptr;
            if (strcmp(inst->op, "call") == 0) {
                HashMapEntry *entry = hashmap_get(&ir_function_param_aggregate_sizes, inst->arg);
                if (entry) agg_sizes = (IntArray *)entry->val_ptr;
            }
            int has_aggregate_arg = 0;
            int *arg_in_regs = calloc(num_args + 1, sizeof(int));
            int *arg_first_word = calloc(num_args + 1, sizeof(int));
            int *arg_stack_word = calloc(num_args + 1, sizeof(int));

            int vararg_fixed_count = -1;
            if (strcmp(inst->op, "call") == 0) {
                HashMapEntry *entry = hashmap_get(&ir_function_vararg_fixed_counts, inst->arg);
                if (entry) vararg_fixed_count = entry->val_int;
            }

            int abi_words = 0;
            int stack_words = 0;
            for (long i = 0; i < num_args; ++i) {
                int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                if (agg_size > 0) {
                    has_aggregate_arg = 1;
                }
                if (abi_words + words <= 8) {
                    arg_in_regs[i] = 1;
                    arg_first_word[i] = abi_words;
                    abi_words += words;
                } else {
                    arg_stack_word[i] = stack_words;
                    stack_words += words;
                }
            }

            if (vararg_fixed_count >= 0 && !has_aggregate_arg) {
                if (vararg_fixed_count > 8) {
                    diagnostics_fatal("arm64 variadic calls with more than 8 fixed args are not supported");
                }
                long num_varargs = num_args - vararg_fixed_count;
                long stack_bytes = ((num_varargs * 8 + 15) / 16) * 16;
                if (stack_bytes > 0) {
                    emit_sub_imm(&out, "sp", "sp", stack_bytes);
                }
                for (long i = 0; i < num_args; ++i) {
                    long src_off = stack_bytes + (num_args - 1 - i) * 16;
                    if (i < vararg_fixed_count) {
                        sb_appendf(&out, "    ldr x%ld, [sp, #%ld]\n", i, src_off);
                    } else {
                        sb_appendf(&out, "    ldr x9, [sp, #%ld]\n", src_off);
                        sb_appendf(&out, "    str x9, [sp, #%ld]\n", (i - vararg_fixed_count) * 8);
                    }
                }
                sb_appendf(&out, "    bl _%s\n", inst->arg);
                sb_appendf(&out, "    add sp, sp, #%ld\n", stack_bytes + num_args * 16);
            } else if (has_aggregate_arg) {
                int stack_bytes = ((stack_words * 8 + 15) / 16) * 16;
                if (stack_bytes > 0) {
                    emit_sub_imm(&out, "sp", "sp", stack_bytes);
                }
                for (long i = 0; i < num_args; ++i) {
                    int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                    int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                    int src_off = stack_bytes + (int)(num_args - 1 - i) * 16;
                    if (agg_size > 0) {
                        sb_appendf(&out, "    ldr x9, [sp, #%d]\n", src_off);
                        for (int w = 0; w < words; ++w) {
                            if (arg_in_regs[i]) {
                                sb_appendf(&out, "    ldr x%d, [x9, #%d]\n", arg_first_word[i] + w, w * 8);
                            } else {
                                sb_appendf(&out, "    ldr x10, [x9, #%d]\n", w * 8);
                                sb_appendf(&out, "    str x10, [sp, #%d]\n", (arg_stack_word[i] + w) * 8);
                            }
                        }
                    } else {
                        if (arg_in_regs[i]) {
                            sb_appendf(&out, "    ldr x%d, [sp, #%d]\n", arg_first_word[i], src_off);
                        } else {
                            sb_appendf(&out, "    ldr x10, [sp, #%d]\n", src_off);
                            sb_appendf(&out, "    str x10, [sp, #%d]\n", arg_stack_word[i] * 8);
                        }
                    }
                }
                if (strcmp(inst->op, "icall") == 0) {
                    sb_appendf(&out, "    ldr x16, [sp, #%d]\n", stack_bytes + (int)num_args * 16);
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", inst->arg);
                }
                sb_appendf(&out, "    add sp, sp, #%d\n", stack_bytes + (int)num_args * 16 + (strcmp(inst->op, "icall") == 0 ? 16 : 0));
            } else if (num_args <= 8) {
                for (long i = num_args - 1; i >= 0; --i) {
                    sb_appendf(&out, "    ldr x%ld, [sp], #16\n", i);
                }
                if (strcmp(inst->op, "icall") == 0) {
                    sb_append(&out, "    ldr x16, [sp], #16\n");
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", inst->arg);
                }
            } else {
                long num_stack_args = num_args - 8;
                for (long i = num_args - 1; i >= 8; --i) {
                    int reg_idx = 8 + (int)(i - 8);
                    sb_appendf(&out, "    ldr x%d, [sp], #16\n", reg_idx);
                }
                for (long i = 7; i >= 0; --i) {
                    sb_appendf(&out, "    ldr x%ld, [sp], #16\n", i);
                }
                if (strcmp(inst->op, "icall") == 0) {
                    sb_append(&out, "    ldr x16, [sp], #16\n");
                }
                long stack_bytes = ((num_stack_args + 1) / 2) * 16;
                emit_sub_imm(&out, "sp", "sp", stack_bytes);
                for (long i = 8; i < num_args; ++i) {
                    int reg_idx = 8 + (int)(i - 8);
                    sb_appendf(&out, "    str x%d, [sp, #%ld]\n", reg_idx, (i - 8) * 8);
                }
                if (strcmp(inst->op, "icall") == 0) {
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", inst->arg);
                }
                sb_appendf(&out, "    add sp, sp, #%ld\n", stack_bytes);
            }
            
            free(arg_in_regs);
            free(arg_first_word);
            free(arg_stack_word);

            int ret_agg_size = 0;
            if (strcmp(inst->op, "call") == 0) {
                HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, inst->arg);
                if (entry) ret_agg_size = entry->val_int;
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
            if (ret_agg_size > 8) {
                sb_append(&out, "    str x1, [sp, #-16]!\n");
            }
        } else if (strcmp(inst->op, "addr") == 0) {
            emit_sub_imm(&out, "x0", "x29", (inst->value + 1) * 16);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "gload") == 0) {
            int gsize = 8;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;

            if (is_defined_global(inst->arg)) {
                sb_appendf(&out, "    adrp x0, _%s@PAGE\n", inst->arg);
                if (gsize == 1) {
                    sb_appendf(&out, "    ldrsb x0, [x0, _%s@PAGEOFF]\n", inst->arg);
                } else if (gsize == 2) {
                    sb_appendf(&out, "    ldrsh x0, [x0, _%s@PAGEOFF]\n", inst->arg);
                } else if (gsize == 4) {
                    sb_appendf(&out, "    ldrsw x0, [x0, _%s@PAGEOFF]\n", inst->arg);
                } else {
                    sb_appendf(&out, "    ldr x0, [x0, _%s@PAGEOFF]\n", inst->arg);
                }
            } else {
                sb_appendf(&out, "    adrp x0, _%s@GOTPAGE\n", inst->arg);
                sb_appendf(&out, "    ldr x0, [x0, _%s@GOTPAGEOFF]\n", inst->arg);
                if (gsize == 1) {
                    sb_append(&out, "    ldrsb x0, [x0]\n");
                } else if (gsize == 2) {
                    sb_append(&out, "    ldrsh x0, [x0]\n");
                } else if (gsize == 4) {
                    sb_append(&out, "    ldrsw x0, [x0]\n");
                } else {
                    sb_append(&out, "    ldr x0, [x0]\n");
                }
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "gstore") == 0) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            int gsize = 8;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;

            if (is_defined_global(inst->arg)) {
                sb_appendf(&out, "    adrp x1, _%s@PAGE\n", inst->arg);
                if (gsize == 1) {
                    sb_appendf(&out, "    strb w0, [x1, _%s@PAGEOFF]\n", inst->arg);
                } else if (gsize == 2) {
                    sb_appendf(&out, "    strh w0, [x1, _%s@PAGEOFF]\n", inst->arg);
                } else if (gsize == 4) {
                    sb_appendf(&out, "    str w0, [x1, _%s@PAGEOFF]\n", inst->arg);
                } else {
                    sb_appendf(&out, "    str x0, [x1, _%s@PAGEOFF]\n", inst->arg);
                }
            } else {
                sb_appendf(&out, "    adrp x1, _%s@GOTPAGE\n", inst->arg);
                sb_appendf(&out, "    ldr x1, [x1, _%s@GOTPAGEOFF]\n", inst->arg);
                if (gsize == 1) {
                    sb_append(&out, "    strb w0, [x1]\n");
                } else if (gsize == 2) {
                    sb_append(&out, "    strh w0, [x1]\n");
                } else if (gsize == 4) {
                    sb_append(&out, "    str w0, [x1]\n");
                } else {
                    sb_append(&out, "    str x0, [x1]\n");
                }
            }
        } else if (strcmp(inst->op, "gaddr") == 0) {
            if (is_defined_global(inst->arg)) {
                sb_appendf(&out, "    adrp x0, _%s@PAGE\n", inst->arg);
                sb_appendf(&out, "    add x0, x0, _%s@PAGEOFF\n", inst->arg);
            } else {
                sb_appendf(&out, "    adrp x0, _%s@GOTPAGE\n", inst->arg);
                sb_appendf(&out, "    ldr x0, [x0, _%s@GOTPAGEOFF]\n", inst->arg);
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "store_index") == 0) {
            sb_append(&out, "    ldr x1, [sp], #16\n"); // dest_addr
            sb_append(&out, "    ldr x2, [sp], #16\n"); // offset
            if (inst->value > 8) {
                if (inst->value == 16) {
                    sb_append(&out, "    add x1, x1, x2, lsl #4\n");
                } else if (inst->value == 32) {
                    sb_append(&out, "    add x1, x1, x2, lsl #5\n");
                } else if (inst->value == 64) {
                    sb_append(&out, "    add x1, x1, x2, lsl #6\n");
                } else {
                    sb_appendf(&out, "    mov x10, #%ld\n", inst->value);
                    sb_append(&out, "    mul x10, x2, x10\n");
                    sb_append(&out, "    add x1, x1, x10\n");
                }
                sb_append(&out, "    ldr x0, [sp], #16\n"); // value 1
                sb_append(&out, "    str x0, [x1, #8]\n");
                sb_append(&out, "    ldr x0, [sp], #16\n"); // value 0
                sb_append(&out, "    str x0, [x1]\n");
            } else {
                sb_append(&out, "    ldr x0, [sp], #16\n"); // value 0
                if (inst->value == 1) {
                    sb_append(&out, "    strb w0, [x1, x2]\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    strh w0, [x1, x2, lsl #1]\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    str w0, [x1, x2, lsl #2]\n");
                } else {
                    sb_append(&out, "    str x0, [x1, x2, lsl #3]\n");
                }
            }
        } else if (strcmp(inst->op, "store_agg") == 0) {
            sb_append(&out, "    ldr x9, [sp], #16\n");
            if (inst->value > 8) {
                sb_append(&out, "    ldr x0, [sp], #16\n");
                sb_append(&out, "    str x0, [x9, #8]\n");
            }
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    str x0, [x9]\n");
        } else if (strcmp(inst->op, "copy") == 0) {
            sb_append(&out, "    ldr x9, [sp], #16\n");  // dest_addr
            sb_append(&out, "    ldr x10, [sp], #16\n"); // src_addr
            emit_block_copy(&out, "x10", "x9", inst->value);
        } else if (strcmp(inst->op, "ret_agg") == 0) {
            sb_append(&out, "    ldr x9, [sp], #16\n");
            sb_append(&out, "    ldr x0, [x9]\n");
            if (inst->value > 8) {
                sb_append(&out, "    ldr x1, [x9, #8]\n");
            }
            if (frame) {
                sb_append(&out, "    mov sp, x29\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            }
            sb_append(&out, "    ret\n");
        } else if (strcmp(inst->op, "ret") == 0) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (frame) {
                sb_append(&out, "    mov sp, x29\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            }
            sb_append(&out, "    ret\n");
        } else if (strcmp(inst->op, "extract_bits") == 0) {
            /* packed value: low 16 bits = bit_offset, high bits = bit_width */
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    ldr x0, [sp], #16\n");
            /* UBFX: unsigned bit-field extract: x0 = x0[bf_offset + bf_width - 1 : bf_offset] */
            sb_appendf(&out, "    ubfx x0, x0, #%d, #%d\n", bf_offset, bf_width);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (strcmp(inst->op, "insert_bits") == 0) {
            /* Stack: [top] dest_addr, offset(0), value */
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    ldr x1, [sp], #16\n"); /* dest_addr */
            sb_append(&out, "    ldr x2, [sp], #16\n"); /* index (==0) */
            sb_append(&out, "    ldr x0, [sp], #16\n"); /* new value */
            /* Read current storage word */
            sb_append(&out, "    ldr x3, [x1]\n");
            /* BFI: bit-field insert: x3 = x3 with bits [bf_offset+bf_width-1:bf_offset] = x0 */
            sb_appendf(&out, "    bfi x3, x0, #%d, #%d\n", bf_offset, bf_width);
            sb_append(&out, "    str x3, [x1]\n");
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
        if (frame) {
            sb_append(&out, "    mov sp, x29\n");
            sb_append(&out, "    ldp x29, x30, [sp], #16\n");
        }
        sb_append(&out, "    ret\n");
    }

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static void arm64_free(TargetBackend *self) {
    free(self);
}

static int arm64_get_target_scale(TargetBackend *self) {
    (void)self;
    return 8;
}

static int arm64_get_stack_slot_size(TargetBackend *self) {
    (void)self;
    return 16;
}

static int arm64_get_aggregate_slots(TargetBackend *self, int size) {
    (void)self;
    return (size + 15) / 16;
}

TargetBackend* backend_create_arm64(void) {
    Arm64Target *b = malloc(sizeof(Arm64Target));
    b->base.emit_globals = arm64_emit_globals;
    b->base.emit_function = arm64_emit_function;
    b->base.free = arm64_free;
    b->base.get_target_scale = arm64_get_target_scale;
    b->base.get_stack_slot_size = arm64_get_stack_slot_size;
    b->base.get_aggregate_slots = arm64_get_aggregate_slots;
    return &b->base;
}

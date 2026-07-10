#include "backend_target.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_sub_imm(StringBuilder *out, const char *dst, const char *base, long imm) {
    if (imm >= 0 && imm <= 4095) {
        sb_appendf(out, "    sub %s, %s, #%ld\n", dst, base, imm);
    } else if (strcmp(dst, "sp") == 0 && strcmp(base, "sp") == 0 && imm > 0) {
        while (imm > 0) {
            long chunk = imm > 4095 ? 4095 : imm;
            sb_appendf(out, "    sub sp, sp, #%ld\n", chunk);
            imm -= chunk;
        }
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

static void emit_narrow_int_to_x8(StringBuilder *out, int src_reg, int size, int is_unsigned) {
    if (size == 1) {
        if (is_unsigned) {
            sb_appendf(out, "    and x8, x%d, #0xff\n", src_reg);
        } else {
            sb_appendf(out, "    sxtb x8, w%d\n", src_reg);
        }
    } else if (size == 2) {
        if (is_unsigned) {
            sb_appendf(out, "    and x8, x%d, #0xffff\n", src_reg);
        } else {
            sb_appendf(out, "    sxth x8, w%d\n", src_reg);
        }
    } else if (size == 4) {
        if (is_unsigned) {
            sb_appendf(out, "    mov w8, w%d\n", src_reg);
        } else {
            sb_appendf(out, "    sxtw x8, w%d\n", src_reg);
        }
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

static int agg_float_elem_size(int cls) {
    if (cls & SYSV_MIXED_FLAG) return 0;   /* x86_64 mixed sentinel: not an HFA on AArch64 */
    return cls & 0xff;
}

static int agg_float_count(int cls) {
    if (cls & SYSV_MIXED_FLAG) return 0;   /* x86_64 mixed sentinel: not an HFA on AArch64 */
    return (cls >> 8) & 0xff;
}

/* The x86_64 mixed-aggregate sentinel means nothing on AArch64, where such
 * aggregates are passed in GPRs like any non-HFA struct. Treat it as class 0. */
static int arm64_hfa_class(int cls) { return (cls & SYSV_MIXED_FLAG) ? 0 : cls; }

static int get_max_call_ret_size(const IrFunction *fn, int threshold) {
    int max_size = 0;
    for (int i = 0; i < fn->code.count; ++i) {
        if (fn->code.data[i].op == IR_CALL) {
            HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, ir_arg_str(fn->code.data[i].arg));
            if (entry && entry->val_int > threshold) {
                if (entry->val_int > max_size) {
                    max_size = entry->val_int;
                }
            }
        }
    }
    return max_size;
}

static int is_defined_global(const char *name) {
    for (int i = 0; i < ir_global_decls.count; ++i) {
        if (strcmp(ir_global_decls.data[i].name, name) == 0) {
            const IrGlobalVar *g = &ir_global_decls.data[i];
            return !(g->is_extern && g->initializers.count == 0);
        }
    }
    return 0;
}

static void arm64_emit_data_symbol_ref(StringBuilder *out, const char *label) {
    if (label[0] == '.' || label[0] == '_') {
        sb_appendf(out, "    .quad %s\n", label);
    } else {
        sb_appendf(out, "    .quad _%s\n", label);
    }
}

static const char *arm64_emit_globals(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena) {
    (void)self;
    if (globals->count == 0)
        return "";

    StringBuilder out;
    sb_init(&out);

    for (int i = 0; i < globals->count; ++i) {
        const IrGlobalVar *g = &globals->data[i];
        if (g->is_extern && g->initializers.count == 0) {
            continue;
        }

        int is_zero = 1;
        for (int k = 0; k < g->initializers.count; ++k) {
            if (g->initializers.data[k] != 0) { is_zero = 0; break; }
        }

        if (g->is_thread_local) {
            if (g->initializers.count == 0 || is_zero) {
                sb_append(&out, ".section __DATA,__thread_bss,thread_local_regular\n");
            } else {
                sb_append(&out, ".section __DATA,__thread_data,thread_local_regular\n");
            }
        } else {
            sb_append(&out, ".data\n");
        }

        if (!g->is_static) {
            sb_appendf(&out, ".globl _%s\n", g->name);
        }
        int align = g->align;
        if (align == 0) {
            align = (g->elem_size == 8) ? 3 : (g->elem_size == 4) ? 2 : (g->elem_size == 2) ? 1 : 0;
        }
        sb_appendf(&out, ".p2align %d\n", align);
        if (g->is_thread_local) {
            sb_appendf(&out, "_%s$tlv$init:\n", g->name);
        } else {
            sb_appendf(&out, "_%s:\n", g->name);
        }

        if (g->is_array || g->initializers.count > 1) {
            int k = 0;
            while (k < g->initializers.count) {
                if (g->initializer_is_string.count > k && g->initializer_is_string.data[k]) {
                    int str_idx = (int)g->initializers.data[k];
                    const char *str_lbl = g->strings.data[str_idx].first;
                    if (g->elem_size == 1) {
                        arm64_emit_data_symbol_ref(&out, str_lbl);
                        k += 8;
                    } else {
                        arm64_emit_data_symbol_ref(&out, str_lbl);
                        k++;
                    }
                } else {
                    long val = g->initializers.data[k];
                    if (g->elem_size == 1)
                        sb_appendf(&out, "    .byte %d\n", (int)(val & 0xff));
                    else if (g->elem_size == 2)
                        sb_appendf(&out, "    .short %d\n", (int)(val & 0xffff));
                    else if (g->elem_size == 4)
                        sb_appendf(&out, "    .long %ld\n", val);
                    else
                        sb_appendf(&out, "    .quad %ld\n", val);
                    k++;
                }
            }
            long remaining = g->size - g->initializers.count;
            if (remaining > 0) {
                sb_appendf(&out, "    .zero %ld\n", remaining * g->elem_size);
            }
        } else {
            if (g->initializer_is_string.count > 0 && g->initializer_is_string.data[0]) {
                int str_idx = (int)g->initializers.data[0];
                const char *str_lbl = g->strings.data[str_idx].first;
                arm64_emit_data_symbol_ref(&out, str_lbl);
            } else {
                long val = (g->initializers.count == 0) ? 0 : g->initializers.data[0];
                if (g->elem_size == 1)
                    sb_appendf(&out, "    .byte %d\n", (int)(val & 0xff));
                else if (g->elem_size == 2)
                    sb_appendf(&out, "    .short %d\n", (int)(val & 0xffff));
                else if (g->elem_size == 4)
                    sb_appendf(&out, "    .long %ld\n", val);
                else if (g->elem_size == 8)
                    sb_appendf(&out, "    .quad %ld\n", val);
                else if (g->elem_size > 8) {
                    sb_appendf(&out, "    .quad %ld\n", val);
                    sb_appendf(&out, "    .zero %d\n", g->elem_size - 8);
                }
            }
        }
    }
    sb_append(&out, "\n");

    int has_any_global_strings = 0;
    for (int i = 0; i < globals->count; ++i) {
        if (globals->data[i].strings.count > 0) {
            has_any_global_strings = 1;
            break;
        }
    }
    if (has_any_global_strings) {
        sb_append(&out, ".section __TEXT,__cstring,cstring_literals\n");
        for (int i = 0; i < globals->count; ++i) {
            const IrGlobalVar *g = &globals->data[i];
            for (int k = 0; k < g->strings.count; ++k) {
                if (g->strings.data[k].second) {
                    const char *escaped = escape_asm_string(g->strings.data[k].second, arena);
                    sb_appendf(&out, "%s:\n    .asciz \"%s\"\n", g->strings.data[k].first, escaped);
                }
            }
        }
    }
    sb_append(&out, "\n");

    int has_any_tls = 0;
    for (int i = 0; i < globals->count; ++i) {
        if (globals->data[i].is_thread_local && !globals->data[i].is_extern) {
            has_any_tls = 1;
            break;
        }
    }
    if (has_any_tls) {
        sb_append(&out, ".section __DATA,__thread_vars,thread_local_variables\n");
        for (int i = 0; i < globals->count; ++i) {
            const IrGlobalVar *g = &globals->data[i];
            if (!g->is_thread_local || g->is_extern) continue;
            if (!g->is_static) {
                sb_appendf(&out, ".globl _%s\n", g->name);
            }
            sb_appendf(&out, "_%s:\n", g->name);
            sb_append(&out, "    .quad __tlv_bootstrap\n");
            sb_append(&out, "    .quad 0\n");
            sb_appendf(&out, "    .quad _%s$tlv$init\n", g->name);
        }
    }

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static const char *substitute_asm_operands_arm64(const char *temp, int num_operands, const char **operand_reprs, Arena *arena) {
    StringBuilder sb;
    sb_init(&sb);
    size_t len = strlen(temp);
    for (size_t i = 0; i < len; ) {
        if (temp[i] == '%' && i + 1 < len) {
            if (temp[i + 1] == '%') {
                sb_append_char(&sb, '%');
                i += 2;
            } else if (temp[i + 1] >= '0' && temp[i + 1] <= '9') {
                int op_idx = temp[i + 1] - '0';
                if (op_idx < num_operands) {
                    sb_append(&sb, operand_reprs[op_idx]);
                }
                i += 2;
            } else {
                sb_append_char(&sb, '%');
                i++;
            }
        } else {
            sb_append_char(&sb, temp[i]);
            i++;
        }
    }
    const char *res = sb_to_string(&sb, arena);
    sb_free(&sb);
    return res;
}

static const char *arm64_op_regs[] = {"x9", "x10", "x11", "x12", "x13", "x14", "x15"};
static const char *arm64_dest_regs[] = {"x13", "x14", "x15"};

/* Bounded access to the fixed scratch pool: an asm with more generic operands
 * than pooled registers (e.g. the syscall wrapper) must not index past the
 * array — see the x86_64 backend's get_operand_reg for the rationale. */
static const char *arm64_op_reg(int i) {
    int n = (int)(sizeof(arm64_op_regs) / sizeof(arm64_op_regs[0]));
    if (i >= n) i = n - 1;
    if (i < 0) i = 0;
    return arm64_op_regs[i];
}

static const char *arm64_emit_function(TargetBackend *self, const IrFunction *fn, Arena *arena) {
    (void)self;
    StringBuilder out;
    sb_init(&out);

    if (fn->strings.count > 0) {
        sb_append(&out, ".cstring\n");
        for (int i = 0; i < fn->strings.count; ++i) {
            const char *escaped = escape_asm_string(fn->strings.data[i].second, arena);
            sb_appendf(&out, "%s:\n    .asciz \"%s\"\n", fn->strings.data[i].first, escaped);
        }
    }
    sb_append(&out, ".text\n");
    if (!fn->is_static) {
        sb_appendf(&out, ".globl _%s\n", fn->name);
    }
    sb_appendf(&out, ".p2align 2\n_%s:\n", fn->name);

    int indirect_ret = (fn->return_aggregate_size > 16);
    int max_call_ret = get_max_call_ret_size(fn, 16);
    int call_ret_slots = (max_call_ret > 0) ? (max_call_ret + 15) / 16 : 0;
    int local_slots = fn->locals.size;
    if (indirect_ret) {
        local_slots++;
    }
    local_slots += call_ret_slots;

    int has_custom_align = (fn->max_align > 16);
    int save_rsp_slot = -1;
    if (has_custom_align) {
        save_rsp_slot = local_slots;
        local_slots++;
    }
    int frame = fn->has_call || (local_slots > 0) || has_custom_align;
    if (frame) {
        sb_append(&out, "    stp x29, x30, [sp, #-16]!\n");
        if (has_custom_align) {
            sb_append(&out, "    mov x11, sp\n");
            int align_log = 0;
            int tmp = fn->max_align;
            while (tmp > 1) { tmp >>= 1; align_log++; }
            sb_appendf(&out, "    lsr x11, x11, #%d\n", align_log);
            sb_appendf(&out, "    lsl x11, x11, #%d\n", align_log);
            sb_append(&out, "    mov sp, x11\n");
            sb_append(&out, "    mov x29, sp\n");
        } else {
            sb_append(&out, "    mov x29, sp\n");
        }
        if (local_slots > 0) {
            int align_sz = has_custom_align ? fn->max_align : 16;
            int stack_bytes = (local_slots * 16 + align_sz - 1) & ~(align_sz - 1);
            emit_sub_imm(&out, "sp", "sp", stack_bytes);
            if (has_custom_align) {
                emit_store_fp(&out, 11, -((save_rsp_slot + 1) * 16));
            }
        }
        if (indirect_ret) {
            int off = -((fn->locals.size + 1) * 16);
            emit_store_fp(&out, 8, off);
        }
        int abi_word = 0;
        int stack_word = 0;
        int v_word = 0;
        IntArray *prologue_floats = nullptr;
        IntArray *prologue_agg_floats = nullptr;
        IntArray *prologue_int_sizes = nullptr;
        {
            HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
            if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
            HashMapEntry *paf = hashmap_get(&ir_function_param_aggregate_float_classes, fn->name);
            if (paf) prologue_agg_floats = (IntArray *)paf->val_ptr;
            HashMapEntry *pie = hashmap_get(&ir_function_param_int_sizes, fn->name);
            if (pie) prologue_int_sizes = (IntArray *)pie->val_ptr;
        }
        for (int i = 0; i < fn->params.count; ++i) {
            int agg_size = (i < fn->param_aggregate_sizes.count) ? fn->param_aggregate_sizes.data[i] : 0;
            int agg_float = arm64_hfa_class((prologue_agg_floats && i < prologue_agg_floats->count) ? prologue_agg_floats->data[i] : 0);
            int num_slots = (agg_size > 0) ? ((agg_size + 15) / 16) : 1;

            HashMapEntry *entry = hashmap_get((HashMap *)&fn->locals, fn->params.data[i]);
            int slot_idx = entry ? entry->val_int : i;

            int p_float = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
            int p_unsigned = ir_get_var_unsigned(fn->params.data[i]);
            if (p_float && v_word < 8) {
                /* scalar float/double parameter passed in V0-V7 (AAPCS64) */
                int off = -((slot_idx + 1) * 16);
                emit_sub_imm(&out, "x9", "x29", -off);
                if (p_float == 4) {
                    sb_appendf(&out, "    str s%d, [x9]\n", v_word);
                } else {
                    sb_appendf(&out, "    str d%d, [x9]\n", v_word);
                }
                v_word++;
                continue;
            }
            if (agg_size > 0 && agg_float) {
                int elem = agg_float_elem_size(agg_float);
                int count = agg_float_count(agg_float);
                int off = -((slot_idx + num_slots) * 16);
                emit_sub_imm(&out, "x9", "x29", -off);
                if (v_word + count <= 8) {
                    for (int e = 0; e < count; ++e) {
                        if (elem == 4) {
                            sb_appendf(&out, "    str s%d, [x9, #%d]\n", v_word + e, e * elem);
                        } else {
                            sb_appendf(&out, "    str d%d, [x9, #%d]\n", v_word + e, e * elem);
                        }
                    }
                    v_word += count;
                } else {
                    for (int w = 0; w < (agg_size + 7) / 8; ++w) {
                        emit_load_fp(&out, 8, 16 + stack_word * 8);
                        sb_appendf(&out, "    str x8, [x9, #%d]\n", w * 8);
                        stack_word++;
                    }
                }
                continue;
            }

            if (agg_size > 16) {
                int in_regs = (abi_word + 1 <= 8);
                if (in_regs) {
                    sb_appendf(&out, "    mov x9, x%d\n", abi_word);
                    abi_word++;
                } else {
                    emit_load_fp(&out, 9, 16 + stack_word * 8);
                    stack_word++;
                }
                int base_off = -((slot_idx + num_slots) * 16);
                emit_sub_imm(&out, "x10", "x29", -base_off);
                emit_block_copy(&out, "x9", "x10", agg_size);
            } else {
                int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                int in_regs = (abi_word + words <= 8);
                for (int w = 0; w < words; ++w) {
                    int off = -((slot_idx + num_slots) * 16) + (w * 8);
                    if (in_regs) {
                        int p_size = (prologue_int_sizes && i < prologue_int_sizes->count) ? prologue_int_sizes->data[i] : 8;
                        if (agg_size == 0 && w == 0 && p_size > 0 && p_size < 8) {
                            emit_narrow_int_to_x8(&out, abi_word + w, p_size, p_unsigned);
                            emit_store_fp(&out, 8, off);
                        } else {
                            emit_store_fp(&out, abi_word + w, off);
                        }
                    } else {
                        int p_size = (prologue_int_sizes && i < prologue_int_sizes->count) ? prologue_int_sizes->data[i] : 8;
                        emit_load_fp(&out, 8, 16 + stack_word * 8);
                        if (agg_size == 0 && w == 0 && p_size > 0 && p_size < 8) {
                            emit_narrow_int_to_x8(&out, 8, p_size, p_unsigned);
                        }
                        emit_store_fp(&out, 8, off);
                        stack_word++;
                    }
                }
                if (in_regs) {
                    abi_word += words;
                }
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
        if (inst->op == IR_ASM) {
            const char *encoded = ir_arg_str(inst->arg);
            if (strchr(encoded, '|')) {
                char *buf = strdup(encoded);
                char *bar1 = strchr(buf, '|');
                if (bar1) {
                    *bar1 = '\0';
                    char *template = buf;
                    char *constraints_str = bar1 + 1;
                    char *bar2 = strchr(constraints_str, '|');
                    if (bar2) {
                        *bar2 = '\0';
                        int num_outs = atoi(bar2 + 1);
                        char *constraints[32];
                        int num_ops = 0;
                        char *tok = strtok(constraints_str, ",");
                        while (tok && num_ops < 32) {
                            constraints[num_ops++] = tok;
                            tok = strtok(nullptr, ",");
                        }
                        int num_ins = 0;
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            if (c[0] != '=') num_ins++;
                        }
                        for (int i = num_ops - 1; i >= 0; --i) {
                            const char *c = constraints[i];
                            if (c[0] != '=') {
                                sb_appendf(&out, "    ldr %s, [sp], #16\n", arm64_op_reg(i));
                            }
                        }
                        int out_idx = num_outs - 1;
                        for (int i = num_ops - 1; i >= 0; --i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (strchr(c, 'm')) {
                                    sb_appendf(&out, "    ldr %s, [sp], #16\n", arm64_op_reg(i));
                                } else {
                                    sb_appendf(&out, "    ldr %s, [sp], #16\n", arm64_dest_regs[out_idx]);
                                    out_idx--;
                                }
                            }
                        }
                        const char *operand_reprs[32];
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            if (strchr(c, 'm')) {
                                char repr[64];
                                snprintf(repr, sizeof(repr), "[%s]", arm64_op_reg(i));
                                operand_reprs[i] = arena_strdup(arena, repr);
                            } else {
                                operand_reprs[i] = arm64_op_reg(i);
                            }
                        }
                        const char *substituted = substitute_asm_operands_arm64(template, num_ops, operand_reprs, arena);
                        sb_appendf(&out, "    %s\n", substituted);
                        int out_count = 0;
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (!strchr(c, 'm')) {
                                    sb_appendf(&out, "    str %s, [%s]\n", arm64_op_reg(i), arm64_dest_regs[out_count]);
                                    out_count++;
                                }
                            }
                        }
                    }
                }
                free(buf);
            } else {
                sb_append(&out, encoded);
                if (encoded[0] && encoded[strlen(encoded) - 1] != '\n') {
                    sb_append(&out, "\n");
                }
            }
        } else if (inst->op == IR_CONST) {
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
        } else if (inst->op == IR_LOAD) {
            emit_load_fp(&out, 0, -((inst->value + 1) * 16));
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_VA_START) {
            int stack_word = 0;
            int abi_word = 0;
            int v_word = 0;
            IntArray *prologue_floats = nullptr;
            IntArray *prologue_agg_floats = nullptr;
            {
                HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
                if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
                HashMapEntry *paf = hashmap_get(&ir_function_param_aggregate_float_classes, fn->name);
                if (paf) prologue_agg_floats = (IntArray *)paf->val_ptr;
            }
            int num_fixed = fn->params.count;
            if (num_fixed > 0 && strcmp(fn->params.data[num_fixed - 1], "...") == 0) {
                num_fixed--;
            }
            for (int i = 0; i < num_fixed; ++i) {
                int agg_size = (i < fn->param_aggregate_sizes.count) ? fn->param_aggregate_sizes.data[i] : 0;
                int agg_float = arm64_hfa_class((prologue_agg_floats && i < prologue_agg_floats->count) ? prologue_agg_floats->data[i] : 0);
                int p_float = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
                if (p_float && v_word < 8) {
                    v_word++;
                    continue;
                }
                if (agg_size > 0 && agg_float) {
                    int count = agg_float_count(agg_float);
                    if (v_word + count <= 8) {
                        v_word += count;
                    } else {
                        stack_word += (agg_size + 7) / 8;
                    }
                    continue;
                }
                if (agg_size > 16) {
                    int in_regs = (abi_word + 1 <= 8);
                    if (in_regs) {
                        abi_word++;
                    } else {
                        stack_word++;
                    }
                } else {
                    int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                    int in_regs = (abi_word + words <= 8);
                    if (in_regs) {
                        abi_word += words;
                    } else {
                        stack_word += words;
                    }
                }
            }
            sb_appendf(&out, "    add x0, x29, #%d\n", 16 + stack_word * 8);
            emit_store_fp(&out, 0, -((inst->value + 1) * 16));
        } else if (inst->op == IR_STR) {
            sb_appendf(&out, "    adrp x0, %s@PAGE\n", ir_arg_str(inst->arg));
            sb_appendf(&out, "    add x0, x0, %s@PAGEOFF\n", ir_arg_str(inst->arg));
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_STORE) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            emit_store_fp(&out, 0, -((inst->value + 1) * 16));
        } else if (inst->op == IR_DUP) {
            sb_append(&out, "    ldr x0, [sp]\n");
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_POP) {
            sb_append(&out, "    add sp, sp, #16\n");
        } else if (inst->op == IR_VLA_ALLOC) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    add x0, x0, #15\n");
            sb_append(&out, "    and x0, x0, #~15\n");
            sb_append(&out, "    sub sp, sp, x0\n");
            sb_append(&out, "    mov x0, sp\n");
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_LOAD_ADDR) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (inst->value == 1) {
                sb_append(&out, "    ldrsb x0, [x0]\n");
            } else if (inst->value == 2) {
                sb_append(&out, "    ldrsh x0, [x0]\n");
            } else if (inst->value == 4) {
                sb_append(&out, "    ldrsw x0, [x0]\n");
            } else {
                sb_append(&out, "    ldr x0, [x0]\n");
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL ||
                   inst->op == IR_DIV || inst->op == IR_MOD || inst->op == IR_UDIV || inst->op == IR_UMOD ||
                   inst->op == IR_EQEQ || inst->op == IR_NOTEQ ||
                   inst->op == IR_LT || inst->op == IR_GT || inst->op == IR_LTEQ ||
                   inst->op == IR_ULT || inst->op == IR_UGT || inst->op == IR_ULTEQ ||
                   inst->op == IR_UGTEQ || inst->op == IR_UGTGT ||
                   inst->op == IR_GTEQ || inst->op == IR_INDEX ||
                   inst->op == IR_AND || inst->op == IR_OR || inst->op == IR_XOR ||
                   inst->op == IR_LTLT || inst->op == IR_GTGT) {
            
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    ldr x1, [sp], #16\n");
            if (inst->op == IR_INDEX) {
                if (inst->value == 1) {
                    sb_append(&out, "    ldrsb x0, [x1, x0]\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    ldrsh x0, [x1, x0, lsl #1]\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    ldrsw x0, [x1, x0, lsl #2]\n");
                } else {
                    sb_append(&out, "    ldr x0, [x1, x0, lsl #3]\n");
                }
            } else if (inst->op == IR_ADD)
                sb_append(&out, "    add x0, x1, x0\n");
            else if (inst->op == IR_SUB)
                sb_append(&out, "    sub x0, x1, x0\n");
            else if (inst->op == IR_MUL)
                sb_append(&out, "    mul x0, x1, x0\n");
            else if (inst->op == IR_DIV)
                sb_append(&out, "    sdiv x0, x1, x0\n");
            else if (inst->op == IR_UDIV)
                sb_append(&out, "    udiv x0, x1, x0\n");
            else if (inst->op == IR_MOD) {
                sb_append(&out, "    sdiv x2, x1, x0\n");
                sb_append(&out, "    msub x0, x2, x0, x1\n");
            }
            else if (inst->op == IR_UMOD) {
                sb_append(&out, "    udiv x2, x1, x0\n");
                sb_append(&out, "    msub x0, x2, x0, x1\n");
            }
            else if (inst->op == IR_AND)
                sb_append(&out, "    and x0, x1, x0\n");
            else if (inst->op == IR_OR)
                sb_append(&out, "    orr x0, x1, x0\n");
            else if (inst->op == IR_XOR)
                sb_append(&out, "    eor x0, x1, x0\n");
            else if (inst->op == IR_LTLT)
                sb_append(&out, "    lsl x0, x1, x0\n");
            else if (inst->op == IR_GTGT)
                sb_append(&out, "    asr x0, x1, x0\n");
            else if (inst->op == IR_UGTGT)
                sb_append(&out, "    lsr x0, x1, x0\n");
            else {
                sb_append(&out, "    cmp x1, x0\n");
                if (inst->op == IR_EQEQ)
                    sb_append(&out, "    cset x0, eq\n");
                else if (inst->op == IR_NOTEQ)
                    sb_append(&out, "    cset x0, ne\n");
                else if (inst->op == IR_LT)
                    sb_append(&out, "    cset x0, lt\n");
                else if (inst->op == IR_GT)
                    sb_append(&out, "    cset x0, gt\n");
                else if (inst->op == IR_LTEQ)
                    sb_append(&out, "    cset x0, le\n");
                else if (inst->op == IR_GTEQ)
                    sb_append(&out, "    cset x0, ge\n");
                else if (inst->op == IR_ULT)
                    sb_append(&out, "    cset x0, lo\n");
                else if (inst->op == IR_UGT)
                    sb_append(&out, "    cset x0, hi\n");
                else if (inst->op == IR_ULTEQ)
                    sb_append(&out, "    cset x0, ls\n");
                else
                    sb_append(&out, "    cset x0, hs\n");
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_TILDE || inst->op == IR_NOT || inst->op == IR_NEG ||
                   inst->op == IR_CAST || inst->op == IR_UCAST) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (inst->op == IR_TILDE)
                sb_append(&out, "    mvn x0, x0\n");
            else if (inst->op == IR_NEG)
                sb_append(&out, "    neg x0, x0\n");
            else if (inst->op == IR_CAST) {
                if (inst->value == 1)
                    sb_append(&out, "    sxtb x0, w0\n");
                else if (inst->value == 2)
                    sb_append(&out, "    sxth x0, w0\n");
                else if (inst->value == 4)
                    sb_append(&out, "    sxtw x0, w0\n");
            } else if (inst->op == IR_UCAST) {
                if (inst->value == 1)
                    sb_append(&out, "    and x0, x0, #0xff\n");
                else if (inst->value == 2)
                    sb_append(&out, "    and x0, x0, #0xffff\n");
                else if (inst->value == 4)
                    sb_append(&out, "    mov w0, w0\n");
            } else {
                sb_append(&out, "    cmp x0, #0\n");
                sb_append(&out, "    cset x0, eq\n");
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_JZ) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    cmp x0, #0\n");
            sb_appendf(&out, "    b.eq %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_JMP) {
            sb_appendf(&out, "    b %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_LABEL) {
            sb_appendf(&out, "%s:\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_CALL || inst->op == IR_ICALL) {
            long num_args = inst->value;
            /* Float-scalar call fast path (AAPCS64): float/double args -> V0-V7,
               integer/pointer args -> X0-X7, float/double return -> V0. */
            if (inst->op == IR_CALL || inst->op == IR_ICALL) {
                IntArray *pf = nullptr;
                int retf = 0;
                HashMapEntry *pe = inst->op == IR_CALL ?
                    hashmap_get(&ir_function_param_floats, ir_arg_str(inst->arg)) :
                    hashmap_get(&ir_function_pointer_param_floats, ir_arg_str(inst->arg));
                if (pe) pf = (IntArray *)pe->val_ptr;
                HashMapEntry *re = inst->op == IR_CALL ?
                    hashmap_get(&ir_function_return_floats, ir_arg_str(inst->arg)) :
                    hashmap_get(&ir_function_pointer_return_floats, ir_arg_str(inst->arg));
                if (re) retf = re->val_int;
                int scalar_path_ret_agg_size = 0;
                int scalar_path_has_agg_arg = 0;
                if (inst->op == IR_CALL) {
                    HashMapEntry *ae = hashmap_get(&ir_function_return_aggregate_sizes, ir_arg_str(inst->arg));
                    if (ae) scalar_path_ret_agg_size = ae->val_int;
                    ae = hashmap_get(&ir_function_param_aggregate_sizes, ir_arg_str(inst->arg));
                    if (ae) {
                        IntArray *as = (IntArray *)ae->val_ptr;
                        for (int ai = 0; ai < as->count; ++ai) {
                            if (as->data[ai] > 0) scalar_path_has_agg_arg = 1;
                        }
                    }
                }
                int any_f = 0;
                for (long i = 0; i < num_args; ++i) {
                    int f = (pf && i < pf->count) ? pf->data[i] : 0;
                    if (f) any_f = 1;
                }
                if ((any_f || retf) && scalar_path_ret_agg_size == 0 && !scalar_path_has_agg_arg) {
                    int g = 0, s = 0, stack_words = 0;
                    int *gi = calloc(num_args + 1, sizeof(int));
                    int *si = calloc(num_args + 1, sizeof(int));
                    int *isf = calloc(num_args + 1, sizeof(int));
                    int *on_stack = calloc(num_args + 1, sizeof(int));
                    int *stack_off = calloc(num_args + 1, sizeof(int));
                    for (long i = 0; i < num_args; ++i) {
                        int f = (pf && i < pf->count) ? pf->data[i] : 0;
                        isf[i] = f;
                        if (f) {
                            if (s < 8) si[i] = s++;
                            else { on_stack[i] = 1; stack_off[i] = stack_words++ * 8; }
                        } else {
                            if (g < 8) gi[i] = g++;
                            else { on_stack[i] = 1; stack_off[i] = stack_words++ * 8; }
                        }
                    }
                    long stack_bytes = ((stack_words * 8 + 15) / 16) * 16;
                    if (stack_bytes > 0) {
                        emit_sub_imm(&out, "sp", "sp", stack_bytes);
                    }
                    for (long i = 0; i < num_args; ++i) {
                        long src_off = stack_bytes + (num_args - 1 - i) * 16;
                        if (on_stack[i]) {
                            if (isf[i]) {
                                sb_appendf(&out, "    ldr d16, [sp, #%ld]\n", src_off);
                                if (isf[i] == 4) {
                                    sb_append(&out, "    fcvt s16, d16\n");
                                    sb_appendf(&out, "    str s16, [sp, #%d]\n", stack_off[i]);
                                } else {
                                    sb_appendf(&out, "    str d16, [sp, #%d]\n", stack_off[i]);
                                }
                            } else {
                                sb_appendf(&out, "    ldr x16, [sp, #%ld]\n", src_off);
                                sb_appendf(&out, "    str x16, [sp, #%d]\n", stack_off[i]);
                            }
                        } else {
                            if (isf[i]) {
                                sb_appendf(&out, "    ldr d%d, [sp, #%ld]\n", si[i], src_off);
                                if (isf[i] == 4) {
                                    sb_appendf(&out, "    fcvt s%d, d%d\n", si[i], si[i]);
                                }
                            } else {
                                sb_appendf(&out, "    ldr x%d, [sp, #%ld]\n", gi[i], src_off);
                            }
                        }
                    }
                    if (inst->op == IR_ICALL) {
                        sb_appendf(&out, "    ldr x16, [sp, #%ld]\n", stack_bytes + num_args * 16);
                        sb_append(&out, "    blr x16\n");
                    } else {
                        sb_appendf(&out, "    bl _%s\n", ir_arg_str(inst->arg));
                    }
                    sb_appendf(&out, "    add sp, sp, #%ld\n", stack_bytes + num_args * 16 + (inst->op == IR_ICALL ? 16 : 0));
                    if (retf) {
                        if (retf == 4) {
                            sb_append(&out, "    fcvt d0, s0\n");
                        }
                        sb_append(&out, "    str d0, [sp, #-16]!\n");
                    } else {
                        sb_append(&out, "    str x0, [sp, #-16]!\n");
                    }
                    free(gi); free(si); free(isf); free(on_stack); free(stack_off);
                    continue;
                }
            }
            IntArray *agg_sizes = nullptr;
            IntArray *agg_float_classes = nullptr;
            IntArray *param_floats_for_agg = nullptr;
            if (inst->op == IR_CALL) {
                HashMapEntry *entry = hashmap_get(&ir_function_param_aggregate_sizes, ir_arg_str(inst->arg));
                if (entry) agg_sizes = (IntArray *)entry->val_ptr;
                entry = hashmap_get(&ir_function_param_aggregate_float_classes, ir_arg_str(inst->arg));
                if (entry) agg_float_classes = (IntArray *)entry->val_ptr;
                entry = hashmap_get(&ir_function_param_floats, ir_arg_str(inst->arg));
                if (entry) param_floats_for_agg = (IntArray *)entry->val_ptr;
            }
            int has_aggregate_arg = 0;
            int *arg_in_regs = calloc(num_args + 1, sizeof(int));
            int *arg_in_vregs = calloc(num_args + 1, sizeof(int));
            int *arg_first_word = calloc(num_args + 1, sizeof(int));
            int *arg_first_vreg = calloc(num_args + 1, sizeof(int));
            int *arg_stack_word = calloc(num_args + 1, sizeof(int));

            int vararg_fixed_count = -1;
            if (inst->op == IR_CALL) {
                HashMapEntry *entry = hashmap_get(&ir_function_vararg_fixed_counts, ir_arg_str(inst->arg));
                if (entry) vararg_fixed_count = entry->val_int;
            }

            int ret_agg_size = 0;
            int ret_agg_float = 0;
            int ret_float_for_agg = 0;
            if (inst->op == IR_CALL) {
                HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, ir_arg_str(inst->arg));
                if (entry) ret_agg_size = entry->val_int;
                entry = hashmap_get(&ir_function_return_aggregate_float_classes, ir_arg_str(inst->arg));
                if (entry) ret_agg_float = arm64_hfa_class(entry->val_int);
                entry = hashmap_get(&ir_function_return_floats, ir_arg_str(inst->arg));
                if (entry) ret_float_for_agg = entry->val_int;
            }

            int caller_indirect_ret = (fn->return_aggregate_size > 16);
            int caller_max_call_ret = get_max_call_ret_size(fn, 16);
            int caller_call_ret_slots = (caller_max_call_ret > 0) ? (caller_max_call_ret + 15) / 16 : 0;
            int caller_local_slots = fn->locals.size + (caller_indirect_ret ? 1 : 0);
            int ret_buf_off = -((caller_local_slots + caller_call_ret_slots) * 16);

            int abi_words = 0;
            int v_words = 0;
            int stack_words = 0;
            for (long i = 0; i < num_args; ++i) {
                int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                int agg_float = arm64_hfa_class((agg_float_classes && i < agg_float_classes->count) ? agg_float_classes->data[i] : 0);
                int p_float = (param_floats_for_agg && i < param_floats_for_agg->count) ? param_floats_for_agg->data[i] : 0;
                int words = (agg_size > 16) ? 1 : ((agg_size > 0) ? ((agg_size + 7) / 8) : 1);
                if (agg_size > 0) {
                    has_aggregate_arg = 1;
                }
                if (agg_size == 0 && p_float) {
                    if (v_words < 8) {
                        arg_in_vregs[i] = 1;
                        arg_first_vreg[i] = v_words++;
                    } else {
                        arg_stack_word[i] = stack_words++;
                    }
                    continue;
                } else if (agg_size > 0 && agg_float) {
                    int count = agg_float_count(agg_float);
                    if (v_words + count <= 8) {
                        arg_in_vregs[i] = 1;
                        arg_first_vreg[i] = v_words;
                        v_words += count;
                    } else {
                        arg_stack_word[i] = stack_words;
                        stack_words += words;
                    }
                    continue;
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

            if (vararg_fixed_count >= 0 && !has_aggregate_arg && ret_agg_size <= 16) {
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
                sb_appendf(&out, "    bl _%s\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    add sp, sp, #%ld\n", stack_bytes + num_args * 16);
            } else if (has_aggregate_arg || ret_agg_size > 0) {
                int stack_bytes = ((stack_words * 8 + 15) / 16) * 16;
                if (stack_bytes > 0) {
                    emit_sub_imm(&out, "sp", "sp", stack_bytes);
                }
                for (long i = 0; i < num_args; ++i) {
                    int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                    int agg_float = arm64_hfa_class((agg_float_classes && i < agg_float_classes->count) ? agg_float_classes->data[i] : 0);
                    int p_float = (param_floats_for_agg && i < param_floats_for_agg->count) ? param_floats_for_agg->data[i] : 0;
                    int words = (agg_size > 16) ? 1 : ((agg_size > 0) ? ((agg_size + 7) / 8) : 1);
                    int src_off = stack_bytes + (int)(num_args - 1 - i) * 16;
                    if (agg_size > 0) {
                        sb_appendf(&out, "    ldr x9, [sp, #%d]\n", src_off);
                        if (agg_float && arg_in_vregs[i]) {
                            int elem = agg_float_elem_size(agg_float);
                            int count = agg_float_count(agg_float);
                            for (int e = 0; e < count; ++e) {
                                if (elem == 4) {
                                    sb_appendf(&out, "    ldr s%d, [x9, #%d]\n", arg_first_vreg[i] + e, e * elem);
                                } else {
                                    sb_appendf(&out, "    ldr d%d, [x9, #%d]\n", arg_first_vreg[i] + e, e * elem);
                                }
                            }
                        } else if (agg_size > 16) {
                            if (arg_in_regs[i]) {
                                sb_appendf(&out, "    mov x%d, x9\n", arg_first_word[i]);
                            } else {
                                sb_appendf(&out, "    str x9, [sp, #%d]\n", arg_stack_word[i] * 8);
                            }
                        } else {
                            for (int w = 0; w < words; ++w) {
                                if (arg_in_regs[i]) {
                                    sb_appendf(&out, "    ldr x%d, [x9, #%d]\n", arg_first_word[i] + w, w * 8);
                                } else {
                                    sb_appendf(&out, "    ldr x10, [x9, #%d]\n", w * 8);
                                    sb_appendf(&out, "    str x10, [sp, #%d]\n", (arg_stack_word[i] + w) * 8);
                                }
                            }
                        }
                    } else {
                        if (p_float && arg_in_vregs[i]) {
                            if (p_float == 4) {
                                sb_appendf(&out, "    ldr d%d, [sp, #%d]\n", arg_first_vreg[i], src_off);
                                sb_appendf(&out, "    fcvt s%d, d%d\n", arg_first_vreg[i], arg_first_vreg[i]);
                            } else {
                                sb_appendf(&out, "    ldr d%d, [sp, #%d]\n", arg_first_vreg[i], src_off);
                            }
                        } else if (p_float) {
                            sb_appendf(&out, "    ldr d16, [sp, #%d]\n", src_off);
                            if (p_float == 4) {
                                sb_append(&out, "    fcvt s16, d16\n");
                                sb_appendf(&out, "    str s16, [sp, #%d]\n", arg_stack_word[i] * 8);
                            } else {
                                sb_appendf(&out, "    str d16, [sp, #%d]\n", arg_stack_word[i] * 8);
                            }
                        } else if (arg_in_regs[i]) {
                            sb_appendf(&out, "    ldr x%d, [sp, #%d]\n", arg_first_word[i], src_off);
                        } else {
                            sb_appendf(&out, "    ldr x10, [sp, #%d]\n", src_off);
                            sb_appendf(&out, "    str x10, [sp, #%d]\n", arg_stack_word[i] * 8);
                        }
                    }
                }
                
                if (ret_agg_size > 16) {
                    emit_sub_imm(&out, "x8", "x29", -ret_buf_off);
                }

                if (inst->op == IR_ICALL) {
                    sb_appendf(&out, "    ldr x16, [sp, #%d]\n", stack_bytes + (int)num_args * 16);
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", ir_arg_str(inst->arg));
                }
                sb_appendf(&out, "    add sp, sp, #%d\n", stack_bytes + (int)num_args * 16 + (inst->op == IR_ICALL ? 16 : 0));
            } else if (num_args <= 8) {
                for (long i = num_args - 1; i >= 0; --i) {
                    sb_appendf(&out, "    ldr x%ld, [sp], #16\n", i);
                }
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    ldr x16, [sp], #16\n");
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", ir_arg_str(inst->arg));
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
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    ldr x16, [sp], #16\n");
                }
                long stack_bytes = ((num_stack_args + 1) / 2) * 16;
                emit_sub_imm(&out, "sp", "sp", stack_bytes);
                for (long i = 8; i < num_args; ++i) {
                    int reg_idx = 8 + (int)(i - 8);
                    sb_appendf(&out, "    str x%d, [sp, #%ld]\n", reg_idx, (i - 8) * 8);
                }
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    blr x16\n");
                } else {
                    sb_appendf(&out, "    bl _%s\n", ir_arg_str(inst->arg));
                }
                sb_appendf(&out, "    add sp, sp, #%ld\n", stack_bytes);
            }
            
            free(arg_in_regs);
            free(arg_in_vregs);
            free(arg_first_word);
            free(arg_first_vreg);
            free(arg_stack_word);

            if (ret_float_for_agg) {
                if (ret_float_for_agg == 4) {
                    sb_append(&out, "    fcvt d0, s0\n");
                }
                sb_append(&out, "    str d0, [sp, #-16]!\n");
            } else if (ret_agg_size <= 16) {
                if (ret_agg_float) {
                    int elem = agg_float_elem_size(ret_agg_float);
                    int count = agg_float_count(ret_agg_float);
                    int words = (ret_agg_size + 7) / 8;
                    for (int w = 0; w < words; ++w) {
                        sb_append(&out, "    sub sp, sp, #16\n");
                        int first_elem = (w * 8) / elem;
                        int elems_in_word = 8 / elem;
                        for (int e = 0; e < elems_in_word && first_elem + e < count; ++e) {
                            if (elem == 4) {
                                sb_appendf(&out, "    str s%d, [sp, #%d]\n", first_elem + e, e * elem);
                            } else {
                                sb_appendf(&out, "    str d%d, [sp, #%d]\n", first_elem + e, e * elem);
                            }
                        }
                    }
                } else {
                    sb_append(&out, "    str x0, [sp, #-16]!\n");
                    if (ret_agg_size > 8) {
                        sb_append(&out, "    str x1, [sp, #-16]!\n");
                    }
                }
            } else {
                int words = (ret_agg_size + 7) / 8;
                for (int w = 0; w < words; ++w) {
                    emit_load_fp(&out, 0, ret_buf_off + w * 8);
                    sb_append(&out, "    str x0, [sp, #-16]!\n");
                }
            }
        } else if (inst->op == IR_ADDR) {
            emit_sub_imm(&out, "x0", "x29", (inst->value + 1) * 16);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_GLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;

            int g_is_unsigned = ir_get_var_unsigned(ir_arg_str(inst->arg));
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@TLVPPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@TLVPPAGEOFF]\n", ir_arg_str(inst->arg));
                sb_append(&out, "    ldr x8, [x0]\n");
                sb_append(&out, "    blr x8\n");
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldrb w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsb x0, [x0]\n");
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldrh w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsh x0, [x0]\n");
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldr w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsw x0, [x0]\n");
                } else {
                    sb_append(&out, "    ldr x0, [x0]\n");
                }
            } else if (is_defined_global(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@PAGE\n", ir_arg_str(inst->arg));
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    ldrb w0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    ldrsb x0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    ldrh w0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    ldrsh x0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    ldr w0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    ldrsw x0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    ldr x0, [x0, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                }
            } else {
                sb_appendf(&out, "    adrp x0, _%s@GOTPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@GOTPAGEOFF]\n", ir_arg_str(inst->arg));
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldrb w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsb x0, [x0]\n");
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldrh w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsh x0, [x0]\n");
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_append(&out, "    ldr w0, [x0]\n");
                    else
                        sb_append(&out, "    ldrsw x0, [x0]\n");
                } else {
                    sb_append(&out, "    ldr x0, [x0]\n");
                }
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_GSTORE) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;

            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    mov x2, x0\n");
                sb_appendf(&out, "    adrp x0, _%s@TLVPPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@TLVPPAGEOFF]\n", ir_arg_str(inst->arg));
                sb_append(&out, "    ldr x8, [x0]\n");
                sb_append(&out, "    blr x8\n");
                if (gsize == 1) {
                    sb_append(&out, "    strb w2, [x0]\n");
                } else if (gsize == 2) {
                    sb_append(&out, "    strh w2, [x0]\n");
                } else if (gsize == 4) {
                    sb_append(&out, "    str w2, [x0]\n");
                } else {
                    sb_append(&out, "    str x2, [x0]\n");
                }
            } else if (is_defined_global(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x1, _%s@PAGE\n", ir_arg_str(inst->arg));
                if (gsize == 1) {
                    sb_appendf(&out, "    strb w0, [x1, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else if (gsize == 2) {
                    sb_appendf(&out, "    strh w0, [x1, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else if (gsize == 4) {
                    sb_appendf(&out, "    str w0, [x1, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    str x0, [x1, _%s@PAGEOFF]\n", ir_arg_str(inst->arg));
                }
            } else {
                sb_appendf(&out, "    adrp x1, _%s@GOTPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x1, [x1, _%s@GOTPAGEOFF]\n", ir_arg_str(inst->arg));
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
        } else if (inst->op == IR_GADDR) {
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@TLVPPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@TLVPPAGEOFF]\n", ir_arg_str(inst->arg));
                sb_append(&out, "    ldr x8, [x0]\n");
                sb_append(&out, "    blr x8\n");
            } else if (is_defined_global(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@PAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    add x0, x0, _%s@PAGEOFF\n", ir_arg_str(inst->arg));
            } else {
                sb_appendf(&out, "    adrp x0, _%s@GOTPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@GOTPAGEOFF]\n", ir_arg_str(inst->arg));
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_STORE_INDEX || inst->op == IR_STORE_INDEX_KEEP) {
            int keep_value = inst->op == IR_STORE_INDEX_KEEP;
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
                if (keep_value) {
                    sb_append(&out, "    str x0, [sp, #-16]!\n");
                }
            }
        } else if (inst->op == IR_STORE_AGG) {
            sb_append(&out, "    ldr x9, [sp], #16\n"); // dest_addr
            int words = (inst->value + 7) / 8;
            for (int w = words - 1; w >= 0; --w) {
                sb_append(&out, "    ldr x0, [sp], #16\n");
                sb_appendf(&out, "    str x0, [x9, #%d]\n", w * 8);
            }
        } else if (inst->op == IR_COPY) {
            sb_append(&out, "    ldr x9, [sp], #16\n");  // dest_addr
            sb_append(&out, "    ldr x10, [sp], #16\n"); // src_addr
            emit_block_copy(&out, "x10", "x9", inst->value);
        } else if (inst->op == IR_RET_AGG) {
            int ret_size = inst->value;
            sb_append(&out, "    ldr x9, [sp], #16\n"); // src_addr
            if (arm64_hfa_class(fn->return_aggregate_float_class)) {
                int elem = agg_float_elem_size(fn->return_aggregate_float_class);
                int count = agg_float_count(fn->return_aggregate_float_class);
                for (int e = 0; e < count; ++e) {
                    if (elem == 4) {
                        sb_appendf(&out, "    ldr s%d, [x9, #%d]\n", e, e * elem);
                    } else {
                        sb_appendf(&out, "    ldr d%d, [x9, #%d]\n", e, e * elem);
                    }
                }
            } else if (ret_size > 16) {
                int off = -((fn->locals.size + 1) * 16);
                emit_load_fp(&out, 10, off); // dest_addr (hidden pointer)
                emit_block_copy(&out, "x9", "x10", ret_size);
                sb_append(&out, "    mov x0, x10\n"); // Return hidden pointer in x0
            } else {
                sb_append(&out, "    ldr x0, [x9]\n");
                if (ret_size > 8) {
                    sb_append(&out, "    ldr x1, [x9, #8]\n");
                }
            }
            if (has_custom_align) {
                emit_load_fp(&out, 11, -((save_rsp_slot + 1) * 16));
                sb_append(&out, "    mov sp, x11\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            } else if (frame) {
                sb_append(&out, "    mov sp, x29\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_RET) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (has_custom_align) {
                emit_load_fp(&out, 11, -((save_rsp_slot + 1) * 16));
                sb_append(&out, "    mov sp, x11\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            } else if (frame) {
                sb_append(&out, "    mov sp, x29\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_EXTRACT_BITS) {
            /* packed value: low 16 bits = bit_offset, high bits = bit_width */
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    ldr x0, [sp], #16\n");
            /* UBFX: unsigned bit-field extract: x0 = x0[bf_offset + bf_width - 1 : bf_offset] */
            sb_appendf(&out, "    ubfx x0, x0, #%d, #%d\n", bf_offset, bf_width);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_SEXT_BITS) {
            int width = (int)inst->value;
            int shift = 64 - width;
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (width > 0 && width < 64) {
                sb_appendf(&out, "    lsl x0, x0, #%d\n", shift);
                sb_appendf(&out, "    asr x0, x0, #%d\n", shift);
            }
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_INSERT_BITS) {
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
        } else if (inst->op == IR_FCONST) {
            unsigned long long val = inst->value;
            sb_appendf(&out, "    movz x0, #%d\n", (int)(val & 0xffff));
            if (val & 0xffff0000ULL) sb_appendf(&out, "    movk x0, #%d, lsl #16\n", (int)((val >> 16) & 0xffff));
            if (val & 0xffff00000000ULL) sb_appendf(&out, "    movk x0, #%d, lsl #32\n", (int)((val >> 32) & 0xffff));
            if (val & 0xffff000000000000ULL) sb_appendf(&out, "    movk x0, #%d, lsl #48\n", (int)((val >> 48) & 0xffff));
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_FADD || inst->op == IR_FSUB ||
                   inst->op == IR_FMUL || inst->op == IR_FDIV) {
            const char *fop = inst->op == IR_FADD ? "fadd" :
                              inst->op == IR_FSUB ? "fsub" :
                              inst->op == IR_FMUL ? "fmul" : "fdiv";
            sb_append(&out, "    ldr d0, [sp], #16\n");  /* rhs */
            sb_append(&out, "    ldr d1, [sp], #16\n");  /* lhs */
            sb_appendf(&out, "    %s d0, d1, d0\n", fop);
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_FNEG) {
            sb_append(&out, "    ldr d0, [sp], #16\n");
            sb_append(&out, "    fneg d0, d0\n");
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_I2F) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            sb_append(&out, "    scvtf d0, x0\n");
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_F2I) {
            sb_append(&out, "    ldr d0, [sp], #16\n");
            sb_append(&out, "    fcvtzs x0, d0\n");
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_D2F) {
            sb_append(&out, "    ldr d0, [sp], #16\n");
            sb_append(&out, "    fcvt s0, d0\n");
            sb_append(&out, "    fcvt d0, s0\n");
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_FLT || inst->op == IR_FGT ||
                   inst->op == IR_FLTEQ || inst->op == IR_FGTEQ ||
                   inst->op == IR_FEQEQ || inst->op == IR_FNOTEQ) {
            sb_append(&out, "    ldr d0, [sp], #16\n");  /* rhs */
            sb_append(&out, "    ldr d1, [sp], #16\n");  /* lhs */
            sb_append(&out, "    fcmp d1, d0\n");
            const char *cc = inst->op == IR_FLT ? "lt" :
                             inst->op == IR_FLTEQ ? "le" :
                             inst->op == IR_FGT ? "gt" :
                             inst->op == IR_FGTEQ ? "ge" :
                             inst->op == IR_FEQEQ ? "eq" : "ne";
            sb_appendf(&out, "    cset x0, %s\n", cc);
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_FLOAD4) {
            int off = -((inst->value + 1) * 16);
            emit_sub_imm(&out, "x9", "x29", -off);
            sb_append(&out, "    ldr s0, [x9]\n");
            sb_append(&out, "    fcvt d0, s0\n");
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_FLOAD8) {
            emit_load_fp(&out, 0, -((inst->value + 1) * 16));
            sb_append(&out, "    str x0, [sp, #-16]!\n");
        } else if (inst->op == IR_FSTORE4) {
            int off = -((inst->value + 1) * 16);
            sb_append(&out, "    ldr d0, [sp], #16\n");
            sb_append(&out, "    fcvt s0, d0\n");
            emit_sub_imm(&out, "x9", "x29", -off);
            sb_append(&out, "    str s0, [x9]\n");
        } else if (inst->op == IR_FSTORE8) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            emit_store_fp(&out, 0, -((inst->value + 1) * 16));
        } else if (inst->op == IR_FGLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@TLVPPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@TLVPPAGEOFF]\n", ir_arg_str(inst->arg));
                sb_append(&out, "    ldr x8, [x0]\n");
                sb_append(&out, "    blr x8\n");
                if (gsize == 4) {
                    sb_append(&out, "    ldr s0, [x0]\n");
                    sb_append(&out, "    fcvt d0, s0\n");
                } else {
                    sb_append(&out, "    ldr d0, [x0]\n");
                }
            } else if (is_defined_global(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x1, _%s@PAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    add x1, x1, _%s@PAGEOFF\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    ldr s0, [x1]\n");
                    sb_append(&out, "    fcvt d0, s0\n");
                } else {
                    sb_append(&out, "    ldr d0, [x1]\n");
                }
            } else {
                sb_appendf(&out, "    adrp x1, _%s@GOTPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x1, [x1, _%s@GOTPAGEOFF]\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    ldr s0, [x1]\n");
                    sb_append(&out, "    fcvt d0, s0\n");
                } else {
                    sb_append(&out, "    ldr d0, [x1]\n");
                }
            }
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_FGSTORE) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;
            sb_append(&out, "    ldr d0, [sp], #16\n");
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_appendf(&out, "    adrp x0, _%s@TLVPPAGE\n", ir_arg_str(inst->arg));
                sb_appendf(&out, "    ldr x0, [x0, _%s@TLVPPAGEOFF]\n", ir_arg_str(inst->arg));
                sb_append(&out, "    ldr x8, [x0]\n");
                sb_append(&out, "    blr x8\n");
                if (gsize == 4) {
                    sb_append(&out, "    fcvt s0, d0\n");
                    sb_append(&out, "    str s0, [x0]\n");
                } else {
                    sb_append(&out, "    str d0, [x0]\n");
                }
            } else {
                if (is_defined_global(ir_arg_str(inst->arg))) {
                    sb_appendf(&out, "    adrp x1, _%s@PAGE\n", ir_arg_str(inst->arg));
                    sb_appendf(&out, "    add x1, x1, _%s@PAGEOFF\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    adrp x1, _%s@GOTPAGE\n", ir_arg_str(inst->arg));
                    sb_appendf(&out, "    ldr x1, [x1, _%s@GOTPAGEOFF]\n", ir_arg_str(inst->arg));
                }
                if (gsize == 4) {
                    sb_append(&out, "    fcvt s0, d0\n");
                    sb_append(&out, "    str s0, [x1]\n");
                } else {
                    sb_append(&out, "    str d0, [x1]\n");
                }
            }
        } else if (inst->op == IR_FLOAD_ADDR4 || inst->op == IR_FLOAD_ADDR8) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (inst->op == IR_FLOAD_ADDR4) {
                sb_append(&out, "    ldr s0, [x0]\n");
                sb_append(&out, "    fcvt d0, s0\n");
            } else {
                sb_append(&out, "    ldr d0, [x0]\n");
            }
            sb_append(&out, "    str d0, [sp, #-16]!\n");
        } else if (inst->op == IR_FSTORE_ADDR4 || inst->op == IR_FSTORE_ADDR8) {
            sb_append(&out, "    ldr x0, [sp], #16\n");
            if (inst->op == IR_FSTORE_ADDR4) {
                sb_append(&out, "    ldr d0, [sp], #16\n");
                sb_append(&out, "    fcvt s0, d0\n");
                sb_append(&out, "    str s0, [x0]\n");
            } else {
                sb_append(&out, "    ldr x8, [sp], #16\n");
                sb_append(&out, "    str x8, [x0]\n");
            }
        } else if (inst->op == IR_FRET) {
            sb_append(&out, "    ldr d0, [sp], #16\n");
            if (inst->value == 4) {
                sb_append(&out, "    fcvt s0, d0\n");  /* return float as single */
            }
            if (frame) {
                sb_append(&out, "    mov sp, x29\n");
                sb_append(&out, "    ldp x29, x30, [sp], #16\n");
            }
            sb_append(&out, "    ret\n");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown IR op %s", ir_op_to_string(inst->op));
            diagnostics_fatal(msg);
        }
    }

    int has_explicit_ret = 0;
    if (fn->code.count > 0) {
        IrOp last_op = fn->code.data[fn->code.count - 1].op;
        has_explicit_ret = last_op == IR_RET || last_op == IR_RET_AGG;
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

/* M16: type legalization – ARM64 supports 1/2/4/8-byte loads/stores. */
static int arm64_legalize_type_size(TargetBackend *self, int width) {
    (void)self;
    if (width <= 1) return 1;
    if (width <= 2) return 2;
    if (width <= 4) return 4;
    return 8;
}

/* M16: calling convention – AAPCS64 integer registers. */
static void arm64_get_cc_info(TargetBackend *self, BackendCCInfo *out) {
    (void)self;
    static const char *const regs[] = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"
    };
    out->int_arg_regs      = regs;
    out->int_arg_reg_count = 8;
    out->return_reg        = "x0";
    out->scratch_reg       = "x9";
    out->stack_align       = 16;
}

/* M16: general-purpose integer register names (x0-x15). */
static const char *arm64_get_int_reg_name(TargetBackend *self, int n) {
    (void)self;
    static const char *const names[] = {
        "x0",  "x1",  "x2",  "x3",
        "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11",
        "x12", "x13", "x14", "x15"
    };
    if (n < 0 || n >= 16) return nullptr;
    return names[n];
}

/* M16: return register. */
static const char *arm64_get_return_reg(TargetBackend *self) {
    (void)self;
    return "x0";
}

TargetBackend* backend_create_arm64(void) {
    Arm64Target *b = malloc(sizeof(Arm64Target));
    b->base.emit_globals       = arm64_emit_globals;
    b->base.emit_function      = arm64_emit_function;
    b->base.free               = arm64_free;
    b->base.get_target_scale   = arm64_get_target_scale;
    b->base.get_stack_slot_size  = arm64_get_stack_slot_size;
    b->base.get_aggregate_slots  = arm64_get_aggregate_slots;
    /* M16 contract hooks */
    b->base.legalize_type_size = arm64_legalize_type_size;
    b->base.get_cc_info        = arm64_get_cc_info;
    b->base.get_int_reg_name   = arm64_get_int_reg_name;
    b->base.get_return_reg     = arm64_get_return_reg;
    return &b->base;
}

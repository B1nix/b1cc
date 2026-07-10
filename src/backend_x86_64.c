#include "backend_target.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void x86_64_emit_block_copy(StringBuilder *out, const char *src_reg, const char *dest_reg, int size) {
    int offset = 0;
    while (size >= 8) {
        sb_appendf(out, "    movq %d(%s), %%r11\n", offset, src_reg);
        sb_appendf(out, "    movq %%r11, %d(%s)\n", offset, dest_reg);
        offset += 8;
        size -= 8;
    }
    if (size >= 4) {
        sb_appendf(out, "    movl %d(%s), %%r11d\n", offset, src_reg);
        sb_appendf(out, "    movl %%r11d, %d(%s)\n", offset, dest_reg);
        offset += 4;
        size -= 4;
    }
    if (size >= 2) {
        sb_appendf(out, "    movw %d(%s), %%r11w\n", offset, src_reg);
        sb_appendf(out, "    movw %%r11w, %d(%s)\n", offset, dest_reg);
        offset += 2;
        size -= 2;
    }
    if (size >= 1) {
        sb_appendf(out, "    movb %d(%s), %%r11b\n", offset, src_reg);
        sb_appendf(out, "    movb %%r11b, %d(%s)\n", offset, dest_reg);
        offset += 1;
        size -= 1;
    }
}

typedef struct {
    TargetBackend base;
} X86_64Target;

static int agg_float_elem_size(int cls) {
    if (cls & SYSV_MIXED_FLAG) return 0;   /* mixed sentinel is not a homogeneous float aggregate */
    return cls & 0xff;
}

static int agg_float_count(int cls) {
    if (cls & SYSV_MIXED_FLAG) return 0;
    return (cls >> 8) & 0xff;
}

static int x86_64_agg_sse_slots(int cls) {
    int bytes = agg_float_elem_size(cls) * agg_float_count(cls);
    return (bytes + 7) / 8;
}

/* True when cls is a System V mixed (INTEGER+SSE) two-eightbyte aggregate. */
static int x86_64_is_mixed(int cls) { return (cls & SYSV_MIXED_FLAG) != 0; }
/* Register kind of eightbyte e (0/1): 2 = SSE (XMM), else INTEGER (GPR). */
static int x86_64_mixed_eb(int cls, int e) { return e == 0 ? SYSV_MIXED_EB0(cls) : SYSV_MIXED_EB1(cls); }

static const char *x86_64_emit_globals(TargetBackend *self, const IrGlobalVarArray *globals, Arena *arena) {
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
            if (g->initializers.data[k] != 0) {
                is_zero = 0;
                break;
            }
        }
        
        if (g->is_thread_local) {
            if (is_zero) {
                sb_append(&out, ".section .tbss,\"awT\",@nobits\n");
            } else {
                sb_append(&out, ".section .tdata,\"awT\",@progbits\n");
            }
        } else {
            sb_append(&out, ".data\n");
        }

        if (!g->is_static) {
            sb_appendf(&out, ".globl %s\n", g->name);
        }
        int align = g->align;
        if (align == 0) {
            align = (g->elem_size == 8) ? 3 : (g->elem_size == 4) ? 2 : (g->elem_size == 2) ? 1 : 0;
        }
        sb_appendf(&out, ".type %s, @object\n", g->name);
        long total_bytes = g->is_array ? (g->size * g->elem_size) : ((g->initializers.count == 0 ? 1 : g->initializers.count) * g->elem_size);
        sb_appendf(&out, ".size %s, %ld\n", g->name, total_bytes);
        sb_appendf(&out, ".p2align %d\n", align);
        sb_appendf(&out, "%s:\n", g->name);

        if (g->is_thread_local && (g->initializers.count == 0 || is_zero)) {
            sb_appendf(&out, "    .zero %ld\n", total_bytes);
        } else if (g->is_array || g->initializers.count > 1) {
            int k = 0;
            while (k < g->initializers.count) {
                if (g->initializer_is_string.count > k && g->initializer_is_string.data[k]) {
                    int str_idx = (int)g->initializers.data[k];
                    const char *str_lbl = g->strings.data[str_idx].first;
                    if (g->elem_size == 1) {
                        sb_appendf(&out, "    .quad %s\n", str_lbl);
                        k += 8;
                    } else {
                        sb_appendf(&out, "    .quad %s\n", str_lbl);
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
                sb_appendf(&out, "    .quad %s\n", str_lbl);
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
        sb_append(&out, ".section .rodata\n");
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

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static const char *get_operand_reg(const char *constraint, int op_idx, const char **default_regs) {
    if (strchr(constraint, 'a')) return "%rax";
    if (strchr(constraint, 'b')) return "%rbx";
    if (strchr(constraint, 'c')) return "%rcx";
    if (strchr(constraint, 'd')) return "%rdx";
    if (strchr(constraint, 'S')) return "%rsi";
    if (strchr(constraint, 'D')) return "%rdi";
    return default_regs[op_idx];
}

static const char *substitute_asm_operands(const char *temp, int num_operands, const char **operand_reprs, Arena *arena) {
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

static const char *x86_64_op_regs[] = {"%r8", "%r9", "%rax", "%rcx", "%rdx", "%rsi", "%rdi"};
static const char *x86_64_dest_regs[] = {"%r10", "%r11", "%rbx", "%rsi"};

static const char *x86_64_emit_function(TargetBackend *self, const IrFunction *fn, Arena *arena) {
    (void)self;
    const char *regs[6] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    
    StringBuilder out;
    sb_init(&out);

    if (fn->strings.count > 0) {
        sb_append(&out, ".section .rodata\n");
        for (int i = 0; i < fn->strings.count; ++i) {
            const char *escaped = escape_asm_string(fn->strings.data[i].second, arena);
            sb_appendf(&out, "%s:\n    .asciz \"%s\"\n", fn->strings.data[i].first, escaped);
        }
    }
    sb_append(&out, ".text\n");
    if (!fn->is_static) {
        sb_appendf(&out, ".globl %s\n", fn->name);
    }
    sb_appendf(&out, ".type %s, @function\n", fn->name);
    sb_appendf(&out, "%s:\n", fn->name);

    int is_vararg = 0;
    if (fn->params.count > 0 && strcmp(fn->params.data[fn->params.count - 1], "...") == 0) {
        is_vararg = 1;
    }
    int indirect_ret = (fn->return_aggregate_size > 16);
    int local_slots_before = fn->locals.size;
    if (indirect_ret) {
        local_slots_before++;
    }
    int local_slots = local_slots_before;
    if (is_vararg) {
        local_slots += 3;
    }
    int has_custom_align = (fn->max_align > 16);
    int save_rsp_slot = -1;
    if (has_custom_align) {
        save_rsp_slot = local_slots;
        local_slots++;
    }
    int frame = fn->has_call || (local_slots > 0) || has_custom_align;
    if (frame) {
        sb_append(&out, "    pushq %rbp\n");
        if (has_custom_align) {
            sb_append(&out, "    movq %rsp, %r11\n");
            sb_appendf(&out, "    andq $-%d, %%rsp\n", fn->max_align);
            sb_append(&out, "    movq %rsp, %rbp\n");
        } else {
            sb_append(&out, "    movq %rsp, %rbp\n");
        }
        if (local_slots > 0) {
            int align_sz = has_custom_align ? fn->max_align : 16;
            int stack_bytes = (local_slots * 16 + align_sz - 1) & ~(align_sz - 1);
            sb_appendf(&out, "    subq $%d, %%rsp\n", stack_bytes);
            if (has_custom_align) {
                sb_appendf(&out, "    movq %%r11, -%d(%%rbp)\n", (save_rsp_slot + 1) * 16);
            }
        }
        if (is_vararg) {
            int base_off = -(local_slots_before * 16);
            sb_appendf(&out, "    movq %%rdi, %d(%%rbp)\n", base_off - 48);
            sb_appendf(&out, "    movq %%rsi, %d(%%rbp)\n", base_off - 40);
            sb_appendf(&out, "    movq %%rdx, %d(%%rbp)\n", base_off - 32);
            sb_appendf(&out, "    movq %%rcx, %d(%%rbp)\n", base_off - 24);
            sb_appendf(&out, "    movq %%r8,  %d(%%rbp)\n", base_off - 16);
            sb_appendf(&out, "    movq %%r9,  %d(%%rbp)\n", base_off - 8);
        }
        if (indirect_ret) {
            int off = -(local_slots_before * 16);
            sb_appendf(&out, "    movq %%rdi, %d(%%rbp)\n", off);
        }
        int abi_word = indirect_ret ? 1 : 0;
        int stack_word = 0;
        int sse_word = 0;
        IntArray *prologue_floats = nullptr;
        IntArray *prologue_agg_floats = nullptr;
        {
            HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
            if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
            HashMapEntry *paf = hashmap_get(&ir_function_param_aggregate_float_classes, fn->name);
            if (paf) prologue_agg_floats = (IntArray *)paf->val_ptr;
        }
        for (int i = 0; i < fn->params.count; ++i) {
            int agg_size = (i < fn->param_aggregate_sizes.count) ? fn->param_aggregate_sizes.data[i] : 0;
            int agg_float = (prologue_agg_floats && i < prologue_agg_floats->count) ? prologue_agg_floats->data[i] : 0;
            int p_float = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
            HashMapEntry *p_entry = hashmap_get((HashMap *)&fn->locals, fn->params.data[i]);
            int p_slot = p_entry ? p_entry->val_int : i;
            if (p_float && sse_word < 8) {
                /* scalar float/double parameter passed in XMM */
                int off = -((p_slot + 1) * 16);
                if (p_float == 4) {
                    sb_appendf(&out, "    movss %%xmm%d, %d(%%rbp)\n", sse_word, off);
                } else {
                    sb_appendf(&out, "    movsd %%xmm%d, %d(%%rbp)\n", sse_word, off);
                }
                sse_word++;
                continue;
            }
            if (agg_size > 0 && x86_64_is_mixed(agg_float)) {
                /* System V mixed aggregate: spill each eightbyte from its own
                 * register class (SSE eightbyte from XMM, INTEGER from a GPR). */
                int num_slots = (agg_size + 15) / 16;
                int off = -((p_slot + num_slots) * 16);
                for (int e = 0; e < 2; ++e) {
                    if (x86_64_mixed_eb(agg_float, e) == 2)
                        sb_appendf(&out, "    movq %%xmm%d, %d(%%rbp)\n", sse_word++, off + e * 8);
                    else
                        sb_appendf(&out, "    movq %s, %d(%%rbp)\n", regs[abi_word++], off + e * 8);
                }
                continue;
            }
            if (agg_size > 0 && agg_float) {
                int slots = x86_64_agg_sse_slots(agg_float);
                int num_slots = (agg_size + 15) / 16;
                int off = -((p_slot + num_slots) * 16);
                if (sse_word + slots <= 8) {
                    for (int s = 0; s < slots; ++s) {
                        sb_appendf(&out, "    movq %%xmm%d, %d(%%rbp)\n", sse_word + s, off + s * 8);
                    }
                    sse_word += slots;
                } else {
                    for (int s = 0; s < slots; ++s) {
                        sb_appendf(&out, "    movq %d(%%rbp), %%rax\n", 16 + stack_word * 8);
                        sb_appendf(&out, "    movq %%rax, %d(%%rbp)\n", off + s * 8);
                        stack_word++;
                    }
                }
                continue;
            }
            int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
            int num_slots = (agg_size > 0) ? ((agg_size + 15) / 16) : 1;
            int in_regs = 0;
            if (agg_size > 0 && agg_size <= 16) {
                in_regs = (abi_word + words <= 6);
            } else if (agg_size == 0) {
                in_regs = (abi_word + 1 <= 6);
            }

            HashMapEntry *entry = hashmap_get((HashMap *)&fn->locals, fn->params.data[i]);
            int slot_idx = entry ? entry->val_int : i;

            for (int w = 0; w < words; ++w) {
                int off = -((slot_idx + num_slots) * 16) + (w * 8);
                if (in_regs) {
                    sb_appendf(&out, "    movq %s, %d(%%rbp)\n", regs[abi_word + w], off);
                } else {
                    sb_appendf(&out, "    movq %d(%%rbp), %%rax\n", 16 + stack_word * 8);
                    sb_appendf(&out, "    movq %%rax, %d(%%rbp)\n", off);
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
                                const char *op_reg = get_operand_reg(c, i, x86_64_op_regs);
                                sb_appendf(&out, "    popq %s\n", op_reg);
                            }
                        }
                        int out_idx = num_outs - 1;
                        for (int i = num_ops - 1; i >= 0; --i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (strchr(c, 'm')) {
                                    const char *op_reg = get_operand_reg(c, i, x86_64_op_regs);
                                    sb_appendf(&out, "    popq %s\n", op_reg);
                                } else {
                                    sb_appendf(&out, "    popq %s\n", x86_64_dest_regs[out_idx]);
                                    out_idx--;
                                }
                            }
                        }
                        const char *operand_reprs[32];
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            const char *op_reg = get_operand_reg(c, i, x86_64_op_regs);
                            if (strchr(c, 'm')) {
                                char repr[64];
                                snprintf(repr, sizeof(repr), "(%s)", op_reg);
                                operand_reprs[i] = arena_strdup(arena, repr);
                            } else {
                                operand_reprs[i] = op_reg;
                            }
                        }
                        const char *substituted = substitute_asm_operands(template, num_ops, operand_reprs, arena);
                        sb_appendf(&out, "    %s\n", substituted);
                        int out_count = 0;
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (!strchr(c, 'm')) {
                                    const char *op_reg = get_operand_reg(c, i, x86_64_op_regs);
                                    sb_appendf(&out, "    movq %s, (%s)\n", op_reg, x86_64_dest_regs[out_count]);
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
            sb_appendf(&out, "    movq $%ld, %%rax\n", inst->value);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_LOAD) {
            sb_appendf(&out, "    movq -%ld(%%rbp), %%rax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_VA_START) {
            int num_fixed = fn->params.count;
            if (num_fixed > 0 && strcmp(fn->params.data[num_fixed - 1], "...") == 0) {
                num_fixed--;
            }
            int indirect_ret = (fn->return_aggregate_size > 16);
            int abi_word = indirect_ret ? 1 : 0;
            int stack_word = 0;
            int sse_word = 0;
            IntArray *prologue_floats = nullptr;
            IntArray *prologue_agg_floats = nullptr;
            {
                HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
                if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
                HashMapEntry *paf = hashmap_get(&ir_function_param_aggregate_float_classes, fn->name);
                if (paf) prologue_agg_floats = (IntArray *)paf->val_ptr;
            }
            for (int i = 0; i < num_fixed; ++i) {
                int agg_size = (i < fn->param_aggregate_sizes.count) ? fn->param_aggregate_sizes.data[i] : 0;
                int agg_float = (prologue_agg_floats && i < prologue_agg_floats->count) ? prologue_agg_floats->data[i] : 0;
                int p_float = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
                if (p_float && sse_word < 8) {
                    sse_word++;
                    continue;
                }
                if (agg_size > 0 && x86_64_is_mixed(agg_float)) {
                    /* Mixed fixed param consumes one GPR + one XMM (per eightbyte). */
                    for (int e = 0; e < 2; ++e) {
                        if (x86_64_mixed_eb(agg_float, e) == 2) sse_word++;
                        else abi_word++;
                    }
                    continue;
                }
                if (agg_size > 0 && agg_float) {
                    int slots = x86_64_agg_sse_slots(agg_float);
                    if (sse_word + slots <= 8) {
                        sse_word += slots;
                    } else {
                        stack_word += slots;
                    }
                    continue;
                }
                int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                int in_regs = 0;
                if (agg_size > 0 && agg_size <= 16) {
                    in_regs = (abi_word + words <= 6);
                } else if (agg_size == 0) {
                    in_regs = (abi_word + 1 <= 6);
                }
                if (in_regs) {
                    abi_word += words;
                } else {
                    stack_word += words;
                }
            }
            int local_slots_before = fn->locals.size + (indirect_ret ? 1 : 0);
            int base_off = -(local_slots_before * 16);
            if (abi_word < 6) {
                int off = base_off - 48 + abi_word * 8;
                sb_appendf(&out, "    leaq %d(%%rbp), %%rax\n", off);
            } else {
                int off = 16 + stack_word * 8;
                sb_appendf(&out, "    leaq %d(%%rbp), %%rax\n", off);
            }
            sb_appendf(&out, "    movq %%rax, -%ld(%%rbp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_STR) {
            sb_appendf(&out, "    leaq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_STORE) {
            sb_append(&out, "    popq %rax\n");
            sb_appendf(&out, "    movq %%rax, -%ld(%%rbp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_DUP) {
            sb_append(&out, "    movq (%rsp), %rax\n");
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_POP) {
            sb_append(&out, "    addq $8, %rsp\n");
        } else if (inst->op == IR_VLA_ALLOC) {
            sb_append(&out, "    popq %rax\n");
            sb_append(&out, "    addq $15, %rax\n");
            sb_append(&out, "    andq $-16, %rax\n");
            sb_append(&out, "    subq %rax, %rsp\n");
            sb_append(&out, "    movq %rsp, %rax\n");
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_LOAD_ADDR) {
            sb_append(&out, "    popq %rax\n");
            if (inst->value == 1) {
                sb_append(&out, "    movsbq (%rax), %rax\n");
            } else if (inst->value == 2) {
                sb_append(&out, "    movswq (%rax), %rax\n");
            } else if (inst->value == 4) {
                sb_append(&out, "    movslq (%rax), %rax\n");
            } else {
                sb_append(&out, "    movq (%rax), %rax\n");
            }
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL ||
                   inst->op == IR_DIV || inst->op == IR_MOD ||
                   inst->op == IR_UDIV || inst->op == IR_UMOD ||
                   inst->op == IR_EQEQ || inst->op == IR_NOTEQ ||
                   inst->op == IR_LT || inst->op == IR_GT || inst->op == IR_LTEQ ||
                   inst->op == IR_ULT || inst->op == IR_UGT || inst->op == IR_ULTEQ ||
                   inst->op == IR_UGTEQ || inst->op == IR_UGTGT ||
                   inst->op == IR_GTEQ || inst->op == IR_INDEX ||
                   inst->op == IR_AND || inst->op == IR_OR || inst->op == IR_XOR ||
                   inst->op == IR_LTLT || inst->op == IR_GTGT) {
            
            sb_append(&out, "    popq %rcx\n");
            sb_append(&out, "    popq %rax\n");
            if (inst->op == IR_INDEX) {
                if (inst->value == 1) {
                    sb_append(&out, "    movsbq (%rax,%rcx,1), %rax\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    movswq (%rax,%rcx,2), %rax\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    movslq (%rax,%rcx,4), %rax\n");
                } else {
                    sb_append(&out, "    movq (%rax,%rcx,8), %rax\n");
                }
            } else if (inst->op == IR_ADD)
                sb_append(&out, "    addq %rcx, %rax\n");
            else if (inst->op == IR_SUB)
                sb_append(&out, "    subq %rcx, %rax\n");
            else if (inst->op == IR_MUL)
                sb_append(&out, "    imulq %rcx, %rax\n");
            else if (inst->op == IR_DIV) {
                sb_append(&out, "    cqto\n");
                sb_append(&out, "    idivq %rcx\n");
            } else if (inst->op == IR_MOD) {
                sb_append(&out, "    cqto\n");
                sb_append(&out, "    idivq %rcx\n");
                sb_append(&out, "    movq %rdx, %rax\n");
            } else if (inst->op == IR_UDIV) {
                sb_append(&out, "    xorq %rdx, %rdx\n");
                sb_append(&out, "    divq %rcx\n");
            } else if (inst->op == IR_UMOD) {
                sb_append(&out, "    xorq %rdx, %rdx\n");
                sb_append(&out, "    divq %rcx\n");
                sb_append(&out, "    movq %rdx, %rax\n");
            } else if (inst->op == IR_AND)
                sb_append(&out, "    andq %rcx, %rax\n");
            else if (inst->op == IR_OR)
                sb_append(&out, "    orq %rcx, %rax\n");
            else if (inst->op == IR_XOR)
                sb_append(&out, "    xorq %rcx, %rax\n");
            else if (inst->op == IR_LTLT || inst->op == IR_GTGT || inst->op == IR_UGTGT) {
                if (inst->op == IR_LTLT)
                    sb_append(&out, "    shlq %cl, %rax\n");
                else if (inst->op == IR_GTGT)
                    sb_append(&out, "    sarq %cl, %rax\n");
                else
                    sb_append(&out, "    shrq %cl, %rax\n");
            } else {
                sb_append(&out, "    cmpq %rcx, %rax\n");
                if (inst->op == IR_EQEQ)
                    sb_append(&out, "    sete %al\n");
                else if (inst->op == IR_NOTEQ)
                    sb_append(&out, "    setne %al\n");
                else if (inst->op == IR_LT)
                    sb_append(&out, "    setl %al\n");
                else if (inst->op == IR_GT)
                    sb_append(&out, "    setg %al\n");
                else if (inst->op == IR_LTEQ)
                    sb_append(&out, "    setle %al\n");
                else if (inst->op == IR_GTEQ)
                    sb_append(&out, "    setge %al\n");
                else if (inst->op == IR_ULT)
                    sb_append(&out, "    setb %al\n");
                else if (inst->op == IR_UGT)
                    sb_append(&out, "    seta %al\n");
                else if (inst->op == IR_ULTEQ)
                    sb_append(&out, "    setbe %al\n");
                else
                    sb_append(&out, "    setae %al\n");
                sb_append(&out, "    movzbq %al, %rax\n");
            }
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_TILDE || inst->op == IR_NOT || inst->op == IR_NEG ||
                   inst->op == IR_CAST || inst->op == IR_UCAST) {
            sb_append(&out, "    popq %rax\n");
            if (inst->op == IR_TILDE)
                sb_append(&out, "    notq %rax\n");
            else if (inst->op == IR_NEG)
                sb_append(&out, "    negq %rax\n");
            else if (inst->op == IR_CAST) {
                if (inst->value == 1)
                    sb_append(&out, "    movsbq %al, %rax\n");
                else if (inst->value == 2)
                    sb_append(&out, "    movswq %ax, %rax\n");
                else if (inst->value == 4)
                    sb_append(&out, "    movslq %eax, %rax\n");
            } else if (inst->op == IR_UCAST) {
                if (inst->value == 1)
                    sb_append(&out, "    movzbq %al, %rax\n");
                else if (inst->value == 2)
                    sb_append(&out, "    movzwq %ax, %rax\n");
                else if (inst->value == 4)
                    sb_append(&out, "    movl %eax, %eax\n");
            } else {
                sb_append(&out, "    cmpq $0, %rax\n");
                sb_append(&out, "    sete %al\n");
                sb_append(&out, "    movzbq %al, %rax\n");
            }
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_JZ) {
            sb_append(&out, "    popq %rax\n");
            sb_append(&out, "    cmpq $0, %rax\n");
            sb_appendf(&out, "    je %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_JMP) {
            sb_appendf(&out, "    jmp %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_LABEL) {
            sb_appendf(&out, "%s:\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_CALL || inst->op == IR_ICALL) {
            long num_args = inst->value;
            /* Float-scalar call path (System V): scalar float/double args go
               in XMM0-7 then stack slots, integer/pointer args use the GPR
               sequence then stack slots, and float/double returns come back in XMM0. */
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
                int any_f = 0, ssec = 0;
                for (long i = 0; i < num_args; ++i) {
                    int f = (pf && i < pf->count) ? pf->data[i] : 0;
                    if (f) { any_f = 1; ssec++; }
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
                            if (g < 6) gi[i] = g++;
                            else { on_stack[i] = 1; stack_off[i] = stack_words++ * 8; }
                        }
                    }
                    int stack_bytes = stack_words * 8;
                    if (stack_bytes > 0) {
                        sb_appendf(&out, "    subq $%d, %%rsp\n", stack_bytes);
                    }
                    for (long i = 0; i < num_args; ++i) {
                        int src_off = stack_bytes + (int)(num_args - 1 - i) * 8;
                        if (on_stack[i]) {
                            if (isf[i]) {
                                sb_appendf(&out, "    movsd %d(%%rsp), %%xmm15\n", src_off);
                                if (isf[i] == 4) {
                                    sb_append(&out, "    cvtsd2ss %xmm15, %xmm15\n");
                                    sb_appendf(&out, "    movss %%xmm15, %d(%%rsp)\n", stack_off[i]);
                                } else {
                                    sb_appendf(&out, "    movsd %%xmm15, %d(%%rsp)\n", stack_off[i]);
                                }
                            } else {
                                sb_appendf(&out, "    movq %d(%%rsp), %%r11\n", src_off);
                                sb_appendf(&out, "    movq %%r11, %d(%%rsp)\n", stack_off[i]);
                            }
                        } else {
                            if (isf[i]) {
                                sb_appendf(&out, "    movsd %d(%%rsp), %%xmm%d\n", src_off, si[i]);
                                if (isf[i] == 4) {
                                    /* a float argument is passed as a 32-bit single */
                                    sb_appendf(&out, "    cvtsd2ss %%xmm%d, %%xmm%d\n", si[i], si[i]);
                                }
                            } else {
                                sb_appendf(&out, "    movq %d(%%rsp), %s\n", src_off, regs[gi[i]]);
                            }
                        }
                    }
                    sb_appendf(&out, "    movl $%d, %%eax\n", ssec > 8 ? 8 : ssec);
                    if (inst->op == IR_ICALL) {
                        sb_appendf(&out, "    movq %d(%%rsp), %%r11\n", stack_bytes + (int)num_args * 8);
                        sb_append(&out, "    call *%r11\n");
                    } else {
                        sb_appendf(&out, "    call %s\n", ir_arg_str(inst->arg));
                    }
                    sb_appendf(&out, "    addq $%d, %%rsp\n", stack_bytes + (int)num_args * 8 + (inst->op == IR_ICALL ? 8 : 0));
                    if (retf) {
                        sb_append(&out, "    subq $8, %rsp\n");
                        if (retf == 4) {
                            /* a float result returns as a single; widen to double */
                            sb_append(&out, "    cvtss2sd %xmm0, %xmm0\n");
                        }
                        sb_append(&out, "    movsd %xmm0, (%rsp)\n");
                    } else {
                        sb_append(&out, "    pushq %rax\n");
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
            int *arg_in_sse = calloc(num_args + 1, sizeof(int));
            int *arg_first_word = calloc(num_args + 1, sizeof(int));
            int *arg_first_sse = calloc(num_args + 1, sizeof(int));
            int *arg_stack_word = calloc(num_args + 1, sizeof(int));
            int *arg_mixed = calloc(num_args + 1, sizeof(int));            /* System V mixed aggregate */
            int *arg_eb_sse = calloc((num_args + 1) * 2, sizeof(int));     /* per eightbyte: 1=XMM, 0=GPR */
            int *arg_eb_idx = calloc((num_args + 1) * 2, sizeof(int));     /* per eightbyte: register index */

            int ret_agg_size = 0;
            int ret_agg_float = 0;
            int ret_float_for_agg = 0;
            if (inst->op == IR_CALL) {
                HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, ir_arg_str(inst->arg));
                if (entry) ret_agg_size = entry->val_int;
                entry = hashmap_get(&ir_function_return_aggregate_float_classes, ir_arg_str(inst->arg));
                if (entry) ret_agg_float = entry->val_int;
                entry = hashmap_get(&ir_function_return_floats, ir_arg_str(inst->arg));
                if (entry) ret_float_for_agg = entry->val_int;
            }
            int ret_val_bytes = 0;
            if (ret_agg_size > 16) {
                ret_val_bytes = ((ret_agg_size + 15) / 16) * 16;
                sb_appendf(&out, "    subq $%d, %%rsp\n", ret_val_bytes);
            }

            int abi_words = (ret_agg_size > 16) ? 1 : 0;
            int sse_words = 0;
            int stack_words = 0;
            for (long i = 0; i < num_args; ++i) {
                int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                int agg_float = (agg_float_classes && i < agg_float_classes->count) ? agg_float_classes->data[i] : 0;
                int p_float = (param_floats_for_agg && i < param_floats_for_agg->count) ? param_floats_for_agg->data[i] : 0;
                int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                if (agg_size > 0) {
                    has_aggregate_arg = 1;
                }
                int in_regs = 0;
                if (agg_size == 0 && p_float) {
                    if (sse_words < 8) {
                        arg_in_sse[i] = 1;
                        arg_first_sse[i] = sse_words++;
                    } else {
                        arg_stack_word[i] = stack_words++;
                    }
                    continue;
                } else if (agg_size > 0 && x86_64_is_mixed(agg_float)) {
                    /* Mixed aggregate: assign each eightbyte to XMM or a GPR. */
                    arg_mixed[i] = 1;
                    for (int e = 0; e < 2; ++e) {
                        if (x86_64_mixed_eb(agg_float, e) == 2) {
                            arg_eb_sse[i * 2 + e] = 1;
                            arg_eb_idx[i * 2 + e] = sse_words++;
                        } else {
                            arg_eb_sse[i * 2 + e] = 0;
                            arg_eb_idx[i * 2 + e] = abi_words++;
                        }
                    }
                    continue;
                } else if (agg_size > 0 && agg_float && agg_size <= 16) {
                    int sse_slots = x86_64_agg_sse_slots(agg_float);
                    if (sse_words + sse_slots <= 8) {
                        arg_in_sse[i] = 1;
                        arg_first_sse[i] = sse_words;
                        sse_words += sse_slots;
                    } else {
                        arg_stack_word[i] = stack_words;
                        stack_words += words;
                    }
                    continue;
                } else if (agg_size > 0 && agg_size <= 16) {
                    in_regs = (abi_words + words <= 6);
                } else if (agg_size == 0) {
                    in_regs = (abi_words + 1 <= 6);
                }
                if (in_regs) {
                    arg_in_regs[i] = 1;
                    arg_first_word[i] = abi_words;
                    abi_words += words;
                } else {
                    arg_stack_word[i] = stack_words;
                    stack_words += words;
                }
            }

            if (has_aggregate_arg || ret_agg_size > 0) {
                int stack_bytes = stack_words * 8;
                if (stack_bytes > 0) {
                    sb_appendf(&out, "    subq $%d, %%rsp\n", stack_bytes);
                }
                for (long i = 0; i < num_args; ++i) {
                    int agg_size = (agg_sizes && i < agg_sizes->count) ? agg_sizes->data[i] : 0;
                    int agg_float = (agg_float_classes && i < agg_float_classes->count) ? agg_float_classes->data[i] : 0;
                    int p_float = (param_floats_for_agg && i < param_floats_for_agg->count) ? param_floats_for_agg->data[i] : 0;
                    int words = (agg_size > 0) ? ((agg_size + 7) / 8) : 1;
                    int src_off = stack_bytes + (int)(num_args - 1 - i) * 8 + ret_val_bytes;
                    if (agg_size > 0) {
                        sb_appendf(&out, "    movq %d(%%rsp), %%r10\n", src_off);
                        if (arg_mixed[i]) {
                            for (int e = 0; e < 2; ++e) {
                                if (arg_eb_sse[i * 2 + e])
                                    sb_appendf(&out, "    movq %d(%%r10), %%xmm%d\n", e * 8, arg_eb_idx[i * 2 + e]);
                                else
                                    sb_appendf(&out, "    movq %d(%%r10), %s\n", e * 8, regs[arg_eb_idx[i * 2 + e]]);
                            }
                        } else if (agg_float && agg_size <= 16 && arg_in_sse[i]) {
                            int sse_slots = x86_64_agg_sse_slots(agg_float);
                            for (int s = 0; s < sse_slots; ++s) {
                                sb_appendf(&out, "    movq %d(%%r10), %%xmm%d\n", s * 8, arg_first_sse[i] + s);
                            }
                        } else if (agg_size > 16) {
                            // Struct > 16 is classified as MEMORY: copy to the stack
                            for (int w = 0; w < words; ++w) {
                                sb_appendf(&out, "    movq %d(%%r10), %%r11\n", w * 8);
                                sb_appendf(&out, "    movq %%r11, %d(%%rsp)\n", (arg_stack_word[i] + w) * 8);
                            }
                        } else {
                            for (int w = 0; w < words; ++w) {
                                if (arg_in_regs[i]) {
                                    sb_appendf(&out, "    movq %d(%%r10), %s\n", w * 8, regs[arg_first_word[i] + w]);
                                } else {
                                    sb_appendf(&out, "    movq %d(%%r10), %%r11\n", w * 8);
                                    sb_appendf(&out, "    movq %%r11, %d(%%rsp)\n", (arg_stack_word[i] + w) * 8);
                                }
                            }
                        }
                    } else {
                        if (p_float && arg_in_sse[i]) {
                            sb_appendf(&out, "    movsd %d(%%rsp), %%xmm%d\n", src_off, arg_first_sse[i]);
                            if (p_float == 4) {
                                sb_appendf(&out, "    cvtsd2ss %%xmm%d, %%xmm%d\n", arg_first_sse[i], arg_first_sse[i]);
                            }
                        } else if (p_float) {
                            sb_appendf(&out, "    movsd %d(%%rsp), %%xmm15\n", src_off);
                            if (p_float == 4) {
                                sb_append(&out, "    cvtsd2ss %xmm15, %xmm15\n");
                                sb_appendf(&out, "    movss %%xmm15, %d(%%rsp)\n", arg_stack_word[i] * 8);
                            } else {
                                sb_appendf(&out, "    movsd %%xmm15, %d(%%rsp)\n", arg_stack_word[i] * 8);
                            }
                        } else if (arg_in_regs[i]) {
                            sb_appendf(&out, "    movq %d(%%rsp), %s\n", src_off, regs[arg_first_word[i]]);
                        } else {
                            sb_appendf(&out, "    movq %d(%%rsp), %%r11\n", src_off);
                            sb_appendf(&out, "    movq %%r11, %d(%%rsp)\n", arg_stack_word[i] * 8);
                        }
                    }
                }
                
                if (ret_agg_size > 16) {
                    sb_appendf(&out, "    leaq %d(%%rsp), %%rdi\n", stack_bytes);
                }

                if (inst->op == IR_ICALL) {
                    sb_appendf(&out, "    movq %d(%%rsp), %%rax\n", stack_bytes + (int)num_args * 8 + ret_val_bytes);
                    sb_append(&out, "    movq %rax, %r11\n");
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_append(&out, "    call *%r11\n");
                } else {
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_appendf(&out, "    call %s\n", ir_arg_str(inst->arg));
                }
                sb_appendf(&out, "    addq $%d, %%rsp\n", stack_bytes + (int)num_args * 8 + (inst->op == IR_ICALL ? 8 : 0));
            } else if (num_args <= 6) {
                for (long i = num_args - 1; i >= 0; --i) {
                    sb_appendf(&out, "    popq %s\n", regs[i]);
                }
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    popq %rax\n");
                    sb_append(&out, "    movq %rax, %r11\n");
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_append(&out, "    call *%r11\n");
                } else {
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_appendf(&out, "    call %s\n", ir_arg_str(inst->arg));
                }
            } else {
                long num_stack_args = num_args - 6;
                const char *temp_regs[6] = {"%r10", "%r11", "%r12", "%r13", "%r14", "%r15"};
                for (long i = num_args - 1; i >= 6; --i) {
                    int t_idx = (int)(i - 6);
                    sb_appendf(&out, "    popq %s\n", temp_regs[t_idx % 6]);
                }
                for (long i = 5; i >= 0; --i) {
                    sb_appendf(&out, "    popq %s\n", regs[i]);
                }
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    popq %rax\n");
                }
                for (long i = num_args - 1; i >= 6; --i) {
                    int t_idx = (int)(i - 6);
                    sb_appendf(&out, "    pushq %s\n", temp_regs[t_idx % 6]);
                }
                if (inst->op == IR_ICALL) {
                    sb_append(&out, "    movq %rax, %r10\n");
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_append(&out, "    call *%r10\n");
                } else {
                    sb_append(&out, "    xorl %eax, %eax\n");
                    sb_appendf(&out, "    call %s\n", ir_arg_str(inst->arg));
                }
                sb_appendf(&out, "    addq $%ld, %%rsp\n", num_stack_args * 8);
            }

            free(arg_in_regs);
            free(arg_in_sse);
            free(arg_first_word);
            free(arg_first_sse);
            free(arg_stack_word);
            free(arg_mixed);
            free(arg_eb_sse);
            free(arg_eb_idx);

            if (ret_float_for_agg) {
                sb_append(&out, "    subq $8, %rsp\n");
                if (ret_float_for_agg == 4) {
                    sb_append(&out, "    cvtss2sd %xmm0, %xmm0\n");
                }
                sb_append(&out, "    movsd %xmm0, (%rsp)\n");
            } else if (ret_agg_size <= 16) {
                if (x86_64_is_mixed(ret_agg_float)) {
                    /* Capture mixed return: eb0 then eb1, each from its class's
                     * return register (rax/rdx for INTEGER, xmm0/xmm1 for SSE). */
                    int gpr = 0, sse = 0;
                    for (int e = 0; e < 2; ++e) {
                        sb_append(&out, "    subq $8, %rsp\n");
                        if (x86_64_mixed_eb(ret_agg_float, e) == 2)
                            sb_appendf(&out, "    movq %%xmm%d, (%%rsp)\n", sse++);
                        else
                            sb_appendf(&out, "    movq %s, (%%rsp)\n", gpr++ == 0 ? "%rax" : "%rdx");
                    }
                } else if (ret_agg_float) {
                    int sse_slots = x86_64_agg_sse_slots(ret_agg_float);
                    for (int s = 0; s < sse_slots; ++s) {
                        sb_append(&out, "    subq $8, %rsp\n");
                        sb_appendf(&out, "    movq %%xmm%d, (%%rsp)\n", s);
                    }
                } else {
                    sb_append(&out, "    pushq %rax\n");
                    if (ret_agg_size > 8) {
                        sb_append(&out, "    pushq %rdx\n");
                    }
                }
            }
        } else if (inst->op == IR_ADDR) {
            sb_appendf(&out, "    leaq -%ld(%%rbp), %%rax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_GLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;
            int g_is_unsigned = ir_get_var_unsigned(ir_arg_str(inst->arg));

            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    movq %fs:0, %rcx\n");
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzbl %s@tpoff(%%rcx), %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movsbl %s@tpoff(%%rcx), %%eax\n", ir_arg_str(inst->arg));
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzwq %s@tpoff(%%rcx), %%rax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movswq %s@tpoff(%%rcx), %%rax\n", ir_arg_str(inst->arg));
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movl %s@tpoff(%%rcx), %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movslq %s@tpoff(%%rcx), %%rax\n", ir_arg_str(inst->arg));
                } else
                    sb_appendf(&out, "    movq %s@tpoff(%%rcx), %%rax\n", ir_arg_str(inst->arg));
            } else if (ir_pic_mode) {
                sb_appendf(&out, "    movq %s@GOTPCREL(%%rip), %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzbl (%rcx), %eax\n");
                    else
                        sb_append(&out, "    movsbl (%rcx), %eax\n");
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzwq (%rcx), %rax\n");
                    else
                        sb_append(&out, "    movswq (%rcx), %rax\n");
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movl (%rcx), %eax\n");
                    else
                        sb_append(&out, "    movslq (%rcx), %rax\n");
                } else
                    sb_append(&out, "    movq (%rcx), %rax\n");
            } else if (ir_code_model == 1) {
                /* Kernel model: load address first, then dereference */
                sb_appendf(&out, "    movabs $%s, %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzbl (%rcx), %eax\n");
                    else
                        sb_append(&out, "    movsbl (%rcx), %eax\n");
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzwq (%rcx), %rax\n");
                    else
                        sb_append(&out, "    movswq (%rcx), %rax\n");
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movl (%rcx), %eax\n");
                    else
                        sb_append(&out, "    movslq (%rcx), %rax\n");
                } else
                    sb_append(&out, "    movq (%rcx), %rax\n");
            } else {
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzbl %s(%%rip), %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movsbl %s(%%rip), %%eax\n", ir_arg_str(inst->arg));
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzwq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movswq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
                } else if (gsize == 4) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movl %s(%%rip), %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movslq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
                } else
                    sb_appendf(&out, "    movq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
            }
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_GSTORE) {
            sb_append(&out, "    popq %rax\n");
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;

            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    movq %fs:0, %rcx\n");
                if (gsize == 1)
                    sb_appendf(&out, "    movb %%al, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
                else if (gsize == 2)
                    sb_appendf(&out, "    movw %%ax, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
                else if (gsize == 4)
                    sb_appendf(&out, "    movl %%eax, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
                else
                    sb_appendf(&out, "    movq %%rax, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
            } else if (ir_pic_mode) {
                sb_appendf(&out, "    movq %s@GOTPCREL(%%rip), %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 1)
                    sb_append(&out, "    movb %al, (%rcx)\n");
                else if (gsize == 2)
                    sb_append(&out, "    movw %ax, (%rcx)\n");
                else if (gsize == 4)
                    sb_append(&out, "    movl %eax, (%rcx)\n");
                else
                    sb_append(&out, "    movq %rax, (%rcx)\n");
            } else if (ir_code_model == 1) {
                /* Kernel model: load address first, then store through register */
                sb_appendf(&out, "    movabs $%s, %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 1)
                    sb_append(&out, "    movb %al, (%rcx)\n");
                else if (gsize == 2)
                    sb_append(&out, "    movw %ax, (%rcx)\n");
                else if (gsize == 4)
                    sb_append(&out, "    movl %eax, (%rcx)\n");
                else
                    sb_append(&out, "    movq %rax, (%rcx)\n");
            } else {
                if (gsize == 1)
                    sb_appendf(&out, "    movb %%al, %s(%%rip)\n", ir_arg_str(inst->arg));
                else if (gsize == 2)
                    sb_appendf(&out, "    movw %%ax, %s(%%rip)\n", ir_arg_str(inst->arg));
                else if (gsize == 4)
                    sb_appendf(&out, "    movl %%eax, %s(%%rip)\n", ir_arg_str(inst->arg));
                else
                    sb_appendf(&out, "    movq %%rax, %s(%%rip)\n", ir_arg_str(inst->arg));
            }
        } else if (inst->op == IR_GADDR) {
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    movq %fs:0, %rax\n");
                sb_appendf(&out, "    leaq %s@tpoff(%%rax), %%rax\n", ir_arg_str(inst->arg));
            } else if (ir_pic_mode) {
                sb_appendf(&out, "    movq %s@GOTPCREL(%%rip), %%rax\n", ir_arg_str(inst->arg));
            } else if (ir_code_model == 1) {
                /* Kernel model: absolute address (data may be >2GB from code) */
                sb_appendf(&out, "    movabs $%s, %%rax\n", ir_arg_str(inst->arg));
            } else {
                sb_appendf(&out, "    leaq %s(%%rip), %%rax\n", ir_arg_str(inst->arg));
            }
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_STORE_INDEX || inst->op == IR_STORE_INDEX_KEEP) {
            int keep_value = inst->op == IR_STORE_INDEX_KEEP;
            sb_append(&out, "    popq %rax\n"); // dest_addr
            sb_append(&out, "    popq %rcx\n"); // offset
            if (inst->value > 8) {
                sb_appendf(&out, "    imulq $%ld, %%rcx\n", inst->value);
                sb_append(&out, "    addq %rcx, %rax\n");
                sb_append(&out, "    popq %rdx\n"); // value 1
                sb_append(&out, "    movq %rdx, 8(%rax)\n");
                sb_append(&out, "    popq %rdx\n"); // value 0
                sb_append(&out, "    movq %rdx, (%rax)\n");
            } else {
                sb_append(&out, "    popq %rdx\n"); // value 0
                if (inst->value == 1) {
                    sb_append(&out, "    movb %dl, (%rax,%rcx,1)\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    movw %dx, (%rax,%rcx,2)\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    movl %edx, (%rax,%rcx,4)\n");
                } else {
                    sb_append(&out, "    movq %rdx, (%rax,%rcx,8)\n");
                }
                if (keep_value) {
                    sb_append(&out, "    pushq %rdx\n");
                }
            }
        } else if (inst->op == IR_STORE_AGG) {
            sb_append(&out, "    popq %r10\n"); // dest_addr
            if (inst->value > 16) {
                x86_64_emit_block_copy(&out, "%rsp", "%r10", inst->value);
                int ret_bytes = ((inst->value + 15) / 16) * 16;
                sb_appendf(&out, "    addq $%d, %%rsp\n", ret_bytes);
            } else {
                if (inst->value > 8) {
                    sb_append(&out, "    popq %rax\n");
                    sb_append(&out, "    movq %rax, 8(%r10)\n");
                }
                sb_append(&out, "    popq %rax\n");
                sb_append(&out, "    movq %rax, (%r10)\n");
            }
        } else if (inst->op == IR_COPY) {
            sb_append(&out, "    popq %r10\n"); // dest_addr
            sb_append(&out, "    popq %rax\n"); // src_addr
            x86_64_emit_block_copy(&out, "%rax", "%r10", inst->value);
        } else if (inst->op == IR_RET_AGG) {
            int ret_size = inst->value;
            sb_append(&out, "    popq %r10\n"); // src_addr
            if (x86_64_is_mixed(fn->return_aggregate_float_class)) {
                /* Mixed aggregate return: INTEGER eightbytes in rax/rdx, SSE in xmm0/xmm1. */
                int cls = fn->return_aggregate_float_class;
                const char *iret[2] = {"%rax", "%rdx"};
                int gpr = 0, sse = 0;
                for (int e = 0; e < 2; ++e) {
                    if (x86_64_mixed_eb(cls, e) == 2)
                        sb_appendf(&out, "    movq %d(%%r10), %%xmm%d\n", e * 8, sse++);
                    else
                        sb_appendf(&out, "    movq %d(%%r10), %s\n", e * 8, iret[gpr++]);
                }
            } else if (fn->return_aggregate_float_class) {
                int sse_slots = x86_64_agg_sse_slots(fn->return_aggregate_float_class);
                for (int s = 0; s < sse_slots; ++s) {
                    sb_appendf(&out, "    movq %d(%%r10), %%xmm%d\n", s * 8, s);
                }
            } else if (ret_size > 16) {
                int off = -((fn->locals.size + 1) * 16);
                sb_appendf(&out, "    movq %d(%%rbp), %%r11\n", off); // dest_addr (hidden pointer)
                x86_64_emit_block_copy(&out, "%r10", "%r11", ret_size);
                sb_append(&out, "    movq %r11, %rax\n"); // Return hidden pointer in rax
            } else {
                sb_append(&out, "    movq (%r10), %rax\n");
                if (ret_size > 8) {
                    sb_append(&out, "    movq 8(%r10), %rdx\n");
                }
            }
            if (has_custom_align) {
                sb_appendf(&out, "    movq -%d(%%rbp), %%rsp\n", (save_rsp_slot + 1) * 16);
                sb_append(&out, "    popq %rbp\n");
            } else if (frame) {
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_RET) {
            sb_append(&out, "    popq %rax\n");
            if (has_custom_align) {
                sb_appendf(&out, "    movq -%d(%%rbp), %%rsp\n", (save_rsp_slot + 1) * 16);
                sb_append(&out, "    popq %rbp\n");
            } else if (frame) {
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_EXTRACT_BITS) {
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    popq %rax\n");
            if (bf_offset > 0) {
                sb_appendf(&out, "    shrq $%d, %%rax\n", bf_offset);
            }
            long mask = (bf_width < 64) ? ((1L << bf_width) - 1) : -1L;
            sb_appendf(&out, "    andq $%ld, %%rax\n", mask);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_INSERT_BITS) {
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            long mask = (bf_width < 64) ? ((1L << bf_width) - 1) : -1L;
            sb_append(&out, "    popq %rdi\n");  /* dest_addr */
            sb_append(&out, "    popq %rsi\n");  /* index (==0) */
            sb_append(&out, "    popq %rax\n");  /* new value */
            sb_appendf(&out, "    andq $%ld, %%rax\n", mask);
            if (bf_offset > 0) {
                sb_appendf(&out, "    shlq $%d, %%rax\n", bf_offset);
            }
            sb_append(&out, "    movq (%rdi), %rcx\n");
            sb_appendf(&out, "    andq $%ld, %%rcx\n", ~(mask << bf_offset));
            sb_append(&out, "    orq %rax, %rcx\n");
            sb_append(&out, "    movq %rcx, (%rdi)\n");
        } else if (inst->op == IR_FCONST) {
            sb_appendf(&out, "    movabsq $%ld, %%rax\n", inst->value);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_FADD || inst->op == IR_FSUB ||
                   inst->op == IR_FMUL || inst->op == IR_FDIV) {
            const char *fop = inst->op == IR_FADD ? "addsd" :
                              inst->op == IR_FSUB ? "subsd" :
                              inst->op == IR_FMUL ? "mulsd" : "divsd";
            sb_append(&out, "    movsd 8(%rsp), %xmm0\n");  /* lhs (deeper) */
            sb_append(&out, "    movsd (%rsp), %xmm1\n");   /* rhs (top) */
            sb_appendf(&out, "    %s %%xmm1, %%xmm0\n", fop);
            sb_append(&out, "    addq $8, %rsp\n");
            sb_append(&out, "    movsd %xmm0, (%rsp)\n");
        } else if (inst->op == IR_FNEG) {
            sb_append(&out, "    movabsq $-9223372036854775808, %rcx\n");
            sb_append(&out, "    xorq %rcx, (%rsp)\n");
        } else if (inst->op == IR_I2F) {
            sb_append(&out, "    popq %rax\n");
            sb_append(&out, "    cvtsi2sdq %rax, %xmm0\n");
            sb_append(&out, "    movq %xmm0, %rax\n");
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_F2I) {
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            sb_append(&out, "    cvttsd2si %xmm0, %rax\n");
            sb_append(&out, "    movq %rax, (%rsp)\n");
        } else if (inst->op == IR_D2F) {
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
            sb_append(&out, "    cvtss2sd %xmm0, %xmm0\n");
            sb_append(&out, "    movsd %xmm0, (%rsp)\n");
        } else if (inst->op == IR_FLT || inst->op == IR_FGT ||
                   inst->op == IR_FLTEQ || inst->op == IR_FGTEQ ||
                   inst->op == IR_FEQEQ || inst->op == IR_FNOTEQ) {
            sb_append(&out, "    movsd 8(%rsp), %xmm0\n");  /* lhs */
            sb_append(&out, "    movsd (%rsp), %xmm1\n");   /* rhs */
            sb_append(&out, "    addq $16, %rsp\n");
            sb_append(&out, "    ucomisd %xmm1, %xmm0\n");
            const char *cc = inst->op == IR_FLT ? "setb" :
                             inst->op == IR_FLTEQ ? "setbe" :
                             inst->op == IR_FGT ? "seta" :
                             inst->op == IR_FGTEQ ? "setae" :
                             inst->op == IR_FEQEQ ? "sete" : "setne";
            sb_appendf(&out, "    %s %%al\n", cc);
            sb_append(&out, "    movzbq %al, %rax\n");
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_FLOAD4) {
            /* load a 4-byte float local, widen to double on the eval stack */
            sb_appendf(&out, "    cvtss2sd -%ld(%%rbp), %%xmm0\n", (inst->value + 1) * 16);
            sb_append(&out, "    subq $8, %rsp\n");
            sb_append(&out, "    movsd %xmm0, (%rsp)\n");
        } else if (inst->op == IR_FLOAD8) {
            /* load an 8-byte double local onto the eval stack */
            sb_appendf(&out, "    movq -%ld(%%rbp), %%rax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushq %rax\n");
        } else if (inst->op == IR_FSTORE4) {
            /* store double eval value into a 4-byte float local */
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            sb_append(&out, "    addq $8, %rsp\n");
            sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
            sb_appendf(&out, "    movss %%xmm0, -%ld(%%rbp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_FSTORE8) {
            sb_append(&out, "    popq %rax\n");
            sb_appendf(&out, "    movq %%rax, -%ld(%%rbp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_FGLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    movq %fs:0, %rcx\n");
                if (gsize == 4) {
                    sb_appendf(&out, "    cvtss2sd %s@tpoff(%%rcx), %%xmm0\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    movsd %s@tpoff(%%rcx), %%xmm0\n", ir_arg_str(inst->arg));
                }
            } else if (ir_pic_mode) {
                sb_appendf(&out, "    movq %s@GOTPCREL(%%rip), %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    cvtss2sd (%rcx), %xmm0\n");
                } else {
                    sb_append(&out, "    movsd (%rcx), %xmm0\n");
                }
            } else if (ir_code_model == 1) {
                /* Kernel model: load address first, then access through register */
                sb_appendf(&out, "    movabs $%s, %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    cvtss2sd (%rcx), %xmm0\n");
                } else {
                    sb_append(&out, "    movsd (%rcx), %xmm0\n");
                }
            } else {
                if (gsize == 4) {
                    sb_appendf(&out, "    cvtss2sd %s(%%rip), %%xmm0\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    movsd %s(%%rip), %%xmm0\n", ir_arg_str(inst->arg));
                }
            }
            sb_append(&out, "    subq $8, %rsp\n");
            sb_append(&out, "    movsd %xmm0, (%rsp)\n");
        } else if (inst->op == IR_FGSTORE) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 8;
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            sb_append(&out, "    addq $8, %rsp\n");
            if (ir_is_global_var_thread_local(ir_arg_str(inst->arg))) {
                sb_append(&out, "    movq %fs:0, %rcx\n");
                if (gsize == 4) {
                    sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
                    sb_appendf(&out, "    movss %%xmm0, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    movsd %%xmm0, %s@tpoff(%%rcx)\n", ir_arg_str(inst->arg));
                }
            } else if (ir_pic_mode) {
                sb_appendf(&out, "    movq %s@GOTPCREL(%%rip), %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
                    sb_append(&out, "    movss %xmm0, (%rcx)\n");
                } else {
                    sb_append(&out, "    movsd %xmm0, (%rcx)\n");
                }
            } else if (ir_code_model == 1) {
                /* Kernel model: load address first, then store through register */
                sb_appendf(&out, "    movabs $%s, %%rcx\n", ir_arg_str(inst->arg));
                if (gsize == 4) {
                    sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
                    sb_append(&out, "    movss %xmm0, (%rcx)\n");
                } else {
                    sb_append(&out, "    movsd %xmm0, (%rcx)\n");
                }
            } else {
                if (gsize == 4) {
                    sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
                    sb_appendf(&out, "    movss %%xmm0, %s(%%rip)\n", ir_arg_str(inst->arg));
                } else {
                    sb_appendf(&out, "    movsd %%xmm0, %s(%%rip)\n", ir_arg_str(inst->arg));
                }
            }
        } else if (inst->op == IR_FLOAD_ADDR4 || inst->op == IR_FLOAD_ADDR8) {
            sb_append(&out, "    popq %rax\n");
            if (inst->op == IR_FLOAD_ADDR4) {
                sb_append(&out, "    cvtss2sd (%rax), %xmm0\n");
            } else {
                sb_append(&out, "    movsd (%rax), %xmm0\n");
            }
            sb_append(&out, "    subq $8, %rsp\n");
            sb_append(&out, "    movsd %xmm0, (%rsp)\n");
        } else if (inst->op == IR_FSTORE_ADDR4 || inst->op == IR_FSTORE_ADDR8) {
            sb_append(&out, "    popq %rax\n");
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            sb_append(&out, "    addq $8, %rsp\n");
            if (inst->op == IR_FSTORE_ADDR4) {
                sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");
                sb_append(&out, "    movss %xmm0, (%rax)\n");
            } else {
                sb_append(&out, "    movsd %xmm0, (%rax)\n");
            }
        } else if (inst->op == IR_FRET) {
            sb_append(&out, "    movsd (%rsp), %xmm0\n");
            if (inst->value == 4) {
                sb_append(&out, "    cvtsd2ss %xmm0, %xmm0\n");  /* return float as single */
            }
            sb_append(&out, "    addq $8, %rsp\n");
            if (frame) {
                sb_append(&out, "    leave\n");
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
            sb_append(&out, "    leave\n");
        }
        sb_append(&out, "    ret\n");
    }
    sb_appendf(&out, ".size %s, .-%s\n", fn->name, fn->name);

    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static void x86_64_free(TargetBackend *self) {
    free(self);
}

static int x86_64_get_target_scale(TargetBackend *self) {
    (void)self;
    return 8;
}

static int x86_64_get_stack_slot_size(TargetBackend *self) {
    (void)self;
    return 16;
}

static int x86_64_get_aggregate_slots(TargetBackend *self, int size) {
    (void)self;
    return (size + 15) / 16;
}

/* M16: type legalization – x86-64 supports 1/2/4/8-byte ops. */
static int x86_64_legalize_type_size(TargetBackend *self, int width) {
    (void)self;
    if (width <= 1) return 1;
    if (width <= 2) return 2;
    if (width <= 4) return 4;
    return 8;
}

/* M16: calling convention – System V AMD64 ABI. */
static void x86_64_get_cc_info(TargetBackend *self, BackendCCInfo *out) {
    (void)self;
    static const char *const regs[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
    };
    out->int_arg_regs      = regs;
    out->int_arg_reg_count = 6;
    out->return_reg        = "%rax";
    out->scratch_reg       = "%r10";
    out->stack_align       = 16;
}

/* M16: general-purpose integer register names (0-indexed, 64-bit forms). */
static const char *x86_64_get_int_reg_name(TargetBackend *self, int n) {
    (void)self;
    static const char *const names[] = {
        "%rax", "%rcx", "%rdx", "%rbx",
        "%rsi", "%rdi", "%r8",  "%r9",
        "%r10", "%r11", "%r12", "%r13",
        "%r14", "%r15"
    };
    if (n < 0 || n >= 14) return nullptr;
    return names[n];
}

/* M16: return register. */
static const char *x86_64_get_return_reg(TargetBackend *self) {
    (void)self;
    return "%rax";
}

TargetBackend* backend_create_x86_64(void) {
    X86_64Target *b = malloc(sizeof(X86_64Target));
    b->base.emit_globals       = x86_64_emit_globals;
    b->base.emit_function      = x86_64_emit_function;
    b->base.free               = x86_64_free;
    b->base.get_target_scale   = x86_64_get_target_scale;
    b->base.get_stack_slot_size  = x86_64_get_stack_slot_size;
    b->base.get_aggregate_slots  = x86_64_get_aggregate_slots;
    /* M16 contract hooks */
    b->base.legalize_type_size = x86_64_legalize_type_size;
    b->base.get_cc_info        = x86_64_get_cc_info;
    b->base.get_int_reg_name   = x86_64_get_int_reg_name;
    b->base.get_return_reg     = x86_64_get_return_reg;
    return &b->base;
}

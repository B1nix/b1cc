#include "backend_target.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    TargetBackend base;
} I386Target;

static void i386_emit_block_copy(StringBuilder *out, const char *src_reg, const char *dest_reg, int size) {
    int offset = 0;
    while (size >= 4) {
        sb_appendf(out, "    movl %d(%s), %%ecx\n", offset, src_reg);
        sb_appendf(out, "    movl %%ecx, %d(%s)\n", offset, dest_reg);
        offset += 4;
        size -= 4;
    }
    if (size >= 2) {
        sb_appendf(out, "    movw %d(%s), %%cx\n", offset, src_reg);
        sb_appendf(out, "    movw %%cx, %d(%s)\n", offset, dest_reg);
        offset += 2;
        size -= 2;
    }
    if (size >= 1) {
        sb_appendf(out, "    movb %d(%s), %%cl\n", offset, src_reg);
        sb_appendf(out, "    movb %%cl, %d(%s)\n", offset, dest_reg);
        offset += 1;
        size -= 1;
    }
}

static const char *i386_normalize_inline_asm(const char *asm_text, Arena *arena) {
    StringBuilder sb;
    sb_init(&sb);
    for (size_t i = 0; asm_text[i]; ++i) {
        if (asm_text[i] == '%' && asm_text[i + 1] == '%') {
            sb_append_char(&sb, '%');
            i++;
        } else {
            sb_append_char(&sb, asm_text[i]);
        }
    }
    const char *res = sb_to_string(&sb, arena);
    sb_free(&sb);
    return res;
}

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
            int k = 0;
            while (k < g->initializers.count) {
                if (g->initializer_is_string.count > k && g->initializer_is_string.data[k]) {
                    int str_idx = (int)g->initializers.data[k];
                    const char *str_lbl = g->strings.data[str_idx].first;
                    if (g->elem_size == 1) {
                        sb_appendf(&out, "    .long %s\n", str_lbl);
                        k += 4;
                    } else {
                        sb_appendf(&out, "    .long %s\n", str_lbl);
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
                sb_appendf(&out, "    .long %s\n", str_lbl);
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

static const char *get_operand_reg_i386(const char *constraint, int op_idx, const char **default_regs) {
    if (strchr(constraint, 'a')) return "%eax";
    if (strchr(constraint, 'b')) return "%ebx";
    if (strchr(constraint, 'c')) return "%ecx";
    if (strchr(constraint, 'd')) return "%edx";
    if (strchr(constraint, 'S')) return "%esi";
    if (strchr(constraint, 'D')) return "%edi";
    return default_regs[op_idx];
}

static const char *substitute_asm_operands_i386(const char *temp, int num_operands, const char **operand_reprs, Arena *arena) {
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

static const char *i386_op_regs[] = {"%eax", "%ecx", "%edx", "%ebx", "%esi", "%edi"};
static const char *i386_dest_regs[] = {"%esi", "%edi", "%ebx"};

static const char *i386_emit_function(TargetBackend *self, const IrFunction *fn, Arena *arena) {
    (void)self;
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

    int indirect_ret = (fn->return_aggregate_size > 0);
    int local_slots = fn->locals.size;
    if (indirect_ret) {
        local_slots++;
    }
    int frame = fn->has_call || (local_slots > 0) || ir_pic_mode;
    if (frame) {
        sb_append(&out, "    pushl %ebp\n");
        sb_append(&out, "    movl %esp, %ebp\n");
        if (local_slots > 0) {
            sb_appendf(&out, "    subl $%d, %%esp\n", local_slots * 16);
        }
        if (ir_pic_mode) {
            sb_append(&out, "    pushl %ebx\n");
            sb_append(&out, "    call __x86.get_pc_thunk.bx\n");
            sb_append(&out, "    addl $_GLOBAL_OFFSET_TABLE_, %ebx\n");
        }
        if (indirect_ret) {
            int off = -(local_slots * 16);
            sb_append(&out, "    movl 8(%ebp), %eax\n");
            sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", off);
        }
        int param_offset = indirect_ret ? 12 : 8;
        IntArray *prologue_floats = nullptr;
        {
            HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
            if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
        }
        IntArray *prologue_int_sizes = nullptr;
        {
            HashMapEntry *pie = hashmap_get(&ir_function_param_int_sizes, fn->name);
            if (pie) prologue_int_sizes = (IntArray *)pie->val_ptr;
        }
        int in_off = param_offset;
        for (int i = 0; i < fn->params.count; ++i) {
            int pf = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
            int isz = (prologue_int_sizes && i < prologue_int_sizes->count) ? prologue_int_sizes->data[i] : 4;
            int slot = -((i + 1) * 16);
            if (pf == 8) {
                /* double parameter: copy 8 bytes from the incoming stack */
                sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", in_off);
                sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", slot);
                sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", in_off + 4);
                sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", slot + 4);
                in_off += 8;
            } else if (pf == 0 && isz == 8) {
                sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", in_off);
                sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", slot);
                sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", in_off + 4);
                sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", slot + 4);
                in_off += 8;
            } else {
                /* int/pointer or 4-byte float parameter */
                sb_appendf(&out, "    movl %d(%%ebp), %%eax\n", in_off);
                sb_appendf(&out, "    movl %%eax, %d(%%ebp)\n", slot);
                in_off += 4;
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
                                const char *op_reg = get_operand_reg_i386(c, i, i386_op_regs);
                                sb_appendf(&out, "    popl %s\n", op_reg);
                            }
                        }
                        int out_idx = num_outs - 1;
                        for (int i = num_ops - 1; i >= 0; --i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (strchr(c, 'm')) {
                                    const char *op_reg = get_operand_reg_i386(c, i, i386_op_regs);
                                    sb_appendf(&out, "    popl %s\n", op_reg);
                                } else {
                                    sb_appendf(&out, "    popl %s\n", i386_dest_regs[out_idx]);
                                    out_idx--;
                                }
                            }
                        }
                        const char *operand_reprs[32];
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            const char *op_reg = get_operand_reg_i386(c, i, i386_op_regs);
                            if (strchr(c, 'm')) {
                                char repr[64];
                                snprintf(repr, sizeof(repr), "(%s)", op_reg);
                                operand_reprs[i] = arena_strdup(arena, repr);
                            } else {
                                operand_reprs[i] = op_reg;
                            }
                        }
                        const char *substituted = substitute_asm_operands_i386(template, num_ops, operand_reprs, arena);
                        sb_appendf(&out, "    %s\n", substituted);
                        int out_count = 0;
                        for (int i = 0; i < num_ops; ++i) {
                            const char *c = constraints[i];
                            if (c[0] == '=' || c[0] == '+') {
                                if (!strchr(c, 'm')) {
                                    const char *op_reg = get_operand_reg_i386(c, i, i386_op_regs);
                                    sb_appendf(&out, "    movl %s, (%s)\n", op_reg, i386_dest_regs[out_count]);
                                    out_count++;
                                }
                            }
                        }
                    }
                }
                free(buf);
            } else {
                const char *asm_text = i386_normalize_inline_asm(encoded, arena);
                sb_append(&out, asm_text);
                if (asm_text[0] && asm_text[strlen(asm_text) - 1] != '\n') {
                    sb_append(&out, "\n");
                }
            }
        } else if (inst->op == IR_CONST) {
            sb_appendf(&out, "    movl $%ld, %%eax\n", inst->value);
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_CONST64) {
            unsigned long long v = (unsigned long long)inst->value;
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)((v >> 32) & 0xffffffffu));
            sb_append(&out, "    pushl %eax\n");
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)(v & 0xffffffffu));
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_LOAD) {
            sb_appendf(&out, "    movl -%ld(%%ebp), %%eax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_VA_START) {
            int num_fixed = fn->params.count;
            if (num_fixed > 0 && strcmp(fn->params.data[num_fixed - 1], "...") == 0) {
                num_fixed--;
            }
            int indirect_ret = (fn->return_aggregate_size > 16);
            int param_offset = indirect_ret ? 12 : 8;
            IntArray *prologue_floats = nullptr;
            {
                HashMapEntry *pfe = hashmap_get(&ir_function_param_floats, fn->name);
                if (pfe) prologue_floats = (IntArray *)pfe->val_ptr;
            }
            IntArray *prologue_int_sizes = nullptr;
            {
                HashMapEntry *pie = hashmap_get(&ir_function_param_int_sizes, fn->name);
                if (pie) prologue_int_sizes = (IntArray *)pie->val_ptr;
            }
            int in_off = param_offset;
            for (int i = 0; i < num_fixed; ++i) {
                int pf = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
                int isz = (prologue_int_sizes && i < prologue_int_sizes->count) ? prologue_int_sizes->data[i] : 4;
                if (pf == 8 || (pf == 0 && isz == 8)) {
                    in_off += 8;
                } else {
                    in_off += 4;
                }
            }
            sb_appendf(&out, "    leal %d(%%ebp), %%eax\n", in_off);
            sb_appendf(&out, "    movl %%eax, -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_LOAD64) {
            long off = (inst->value + 1) * 16;
            sb_appendf(&out, "    movl -%ld(%%ebp), %%eax\n", off - 4);
            sb_append(&out, "    pushl %eax\n");
            sb_appendf(&out, "    movl -%ld(%%ebp), %%eax\n", off);
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_STR) {
            sb_appendf(&out, "    movl $%s, %%eax\n", ir_arg_str(inst->arg));
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_STORE) {
            sb_append(&out, "    popl %eax\n");
            sb_appendf(&out, "    movl %%eax, -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_STORE64) {
            long off = (inst->value + 1) * 16;
            sb_append(&out, "    popl %eax\n");
            sb_appendf(&out, "    movl %%eax, -%ld(%%ebp)\n", off);
            sb_append(&out, "    popl %eax\n");
            sb_appendf(&out, "    movl %%eax, -%ld(%%ebp)\n", off - 4);
        } else if (inst->op == IR_DUP) {
            sb_append(&out, "    movl (%esp), %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_POP) {
            sb_append(&out, "    addl $4, %esp\n");
        } else if (inst->op == IR_POP64) {
            sb_append(&out, "    addl $8, %esp\n");
        } else if (inst->op == IR_SEXT64 || inst->op == IR_ZEXT64) {
            sb_append(&out, "    popl %eax\n");
            if (inst->op == IR_SEXT64)
                sb_append(&out, "    cltd\n");
            else
                sb_append(&out, "    xorl %edx, %edx\n");
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_TRUNC32) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    addl $4, %esp\n");
            if (inst->value == 1)
                sb_append(&out, "    movsbl %al, %eax\n");
            else if (inst->value == 2)
                sb_append(&out, "    movswl %ax, %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_ADD64 || inst->op == IR_SUB64 ||
                   inst->op == IR_AND64 || inst->op == IR_OR64 ||
                   inst->op == IR_XOR64 || inst->op == IR_MUL64) {
            sb_append(&out, "    popl %ecx\n");  /* rhs low */
            sb_append(&out, "    popl %ebx\n");  /* rhs high */
            sb_append(&out, "    popl %eax\n");  /* lhs low */
            sb_append(&out, "    popl %edx\n");  /* lhs high */
            if (inst->op == IR_ADD64) {
                sb_append(&out, "    addl %ecx, %eax\n");
                sb_append(&out, "    adcl %ebx, %edx\n");
            } else if (inst->op == IR_SUB64) {
                sb_append(&out, "    subl %ecx, %eax\n");
                sb_append(&out, "    sbbl %ebx, %edx\n");
            } else if (inst->op == IR_AND64) {
                sb_append(&out, "    andl %ecx, %eax\n");
                sb_append(&out, "    andl %ebx, %edx\n");
            } else if (inst->op == IR_OR64) {
                sb_append(&out, "    orl %ecx, %eax\n");
                sb_append(&out, "    orl %ebx, %edx\n");
            } else if (inst->op == IR_XOR64) {
                sb_append(&out, "    xorl %ecx, %eax\n");
                sb_append(&out, "    xorl %ebx, %edx\n");
            } else {
                sb_append(&out, "    movl %eax, %esi\n");
                sb_append(&out, "    imull %ecx, %edx\n");
                sb_append(&out, "    imull %ebx, %esi\n");
                sb_append(&out, "    mull %ecx\n");
                sb_append(&out, "    addl %esi, %edx\n");
            }
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_NEG64 || inst->op == IR_TILDE64) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %edx\n");
            if (inst->op == IR_NEG64) {
                sb_append(&out, "    negl %eax\n");
                sb_append(&out, "    adcl $0, %edx\n");
                sb_append(&out, "    negl %edx\n");
            } else {
                sb_append(&out, "    notl %eax\n");
                sb_append(&out, "    notl %edx\n");
            }
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_LTLT64 || inst->op == IR_GTGT64 || inst->op == IR_UGTGT64) {
            int lid = i_i;
            sb_append(&out, "    popl %ecx\n");
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %edx\n");
            sb_append(&out, "    andl $63, %ecx\n");
            sb_append(&out, "    cmpl $32, %ecx\n");
            sb_appendf(&out, "    jae .Li386_%s_shift_ge32_%d\n", fn->name, lid);
            if (inst->op == IR_LTLT64) {
                sb_append(&out, "    shldl %cl, %eax, %edx\n");
                sb_append(&out, "    shll %cl, %eax\n");
            } else {
                sb_append(&out, "    shrdl %cl, %edx, %eax\n");
                sb_append(&out, inst->op == IR_UGTGT64 ? "    shrl %cl, %edx\n" : "    sarl %cl, %edx\n");
            }
            sb_appendf(&out, "    jmp .Li386_%s_shift_done_%d\n", fn->name, lid);
            sb_appendf(&out, ".Li386_%s_shift_ge32_%d:\n", fn->name, lid);
            sb_append(&out, "    subl $32, %ecx\n");
            if (inst->op == IR_LTLT64) {
                sb_append(&out, "    movl %eax, %edx\n");
                sb_append(&out, "    xorl %eax, %eax\n");
                sb_append(&out, "    shll %cl, %edx\n");
            } else {
                sb_append(&out, "    movl %edx, %eax\n");
                if (inst->op == IR_UGTGT64) {
                    sb_append(&out, "    xorl %edx, %edx\n");
                    sb_append(&out, "    shrl %cl, %eax\n");
                } else {
                    sb_append(&out, "    sarl $31, %edx\n");
                    sb_append(&out, "    sarl %cl, %eax\n");
                }
            }
            sb_appendf(&out, ".Li386_%s_shift_done_%d:\n", fn->name, lid);
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_EQEQ64 || inst->op == IR_NOTEQ64 ||
                   inst->op == IR_LT64 || inst->op == IR_GT64 ||
                   inst->op == IR_LTEQ64 || inst->op == IR_GTEQ64 ||
                   inst->op == IR_ULT64 || inst->op == IR_UGT64 ||
                   inst->op == IR_ULTEQ64 || inst->op == IR_UGTEQ64) {
            const char *setop = "sete";
            sb_append(&out, "    popl %ecx\n");
            sb_append(&out, "    popl %ebx\n");
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %edx\n");
            sb_append(&out, "    cmpl %ebx, %edx\n");
            sb_appendf(&out, "    jne .Li386_%s_cmp_high_%d\n", fn->name, i_i);
            sb_append(&out, "    cmpl %ecx, %eax\n");
            if (inst->op == IR_EQEQ64) setop = "sete";
            else if (inst->op == IR_NOTEQ64) setop = "setne";
            else if (inst->op == IR_LT64) setop = "setb";
            else if (inst->op == IR_GT64) setop = "seta";
            else if (inst->op == IR_LTEQ64) setop = "setbe";
            else if (inst->op == IR_GTEQ64) setop = "setae";
            else if (inst->op == IR_ULT64) setop = "setb";
            else if (inst->op == IR_UGT64) setop = "seta";
            else if (inst->op == IR_ULTEQ64) setop = "setbe";
            else if (inst->op == IR_UGTEQ64) setop = "setae";
            sb_appendf(&out, "    %s %%al\n", setop);
            sb_appendf(&out, "    jmp .Li386_%s_cmp_done_%d\n", fn->name, i_i);
            sb_appendf(&out, ".Li386_%s_cmp_high_%d:\n", fn->name, i_i);
            if (inst->op == IR_EQEQ64) setop = "sete";
            else if (inst->op == IR_NOTEQ64) setop = "setne";
            else if (inst->op == IR_ULT64) setop = "setb";
            else if (inst->op == IR_UGT64) setop = "seta";
            else if (inst->op == IR_ULTEQ64) setop = "setbe";
            else if (inst->op == IR_UGTEQ64) setop = "setae";
            else if (inst->op == IR_LT64) setop = "setl";
            else if (inst->op == IR_GT64) setop = "setg";
            else if (inst->op == IR_LTEQ64) setop = "setle";
            else if (inst->op == IR_GTEQ64) setop = "setge";
            sb_appendf(&out, "    %s %%al\n", setop);
            sb_appendf(&out, ".Li386_%s_cmp_done_%d:\n", fn->name, i_i);
            sb_append(&out, "    movzbl %al, %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_DIV64 || inst->op == IR_MOD64 ||
                   inst->op == IR_UDIV64 || inst->op == IR_UMOD64) {
            const char *helper =
                inst->op == IR_UMOD64 ? "__umoddi3" :
                inst->op == IR_UDIV64 ? "__udivdi3" :
                inst->op == IR_MOD64  ? "__moddi3" : "__divdi3";
            sb_append(&out, "    popl %ecx\n");  /* rhs low */
            sb_append(&out, "    popl %ebx\n");  /* rhs high */
            sb_append(&out, "    popl %eax\n");  /* lhs low */
            sb_append(&out, "    popl %edx\n");  /* lhs high */
            sb_append(&out, "    pushl %ebx\n");
            sb_append(&out, "    pushl %ecx\n");
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
            sb_appendf(&out, "    call %s\n", helper);
            sb_append(&out, "    addl $16, %esp\n");
            sb_append(&out, "    pushl %edx\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_LOAD_ADDR) {
            sb_append(&out, "    popl %eax\n");
            if (inst->value == 1) {
                sb_append(&out, "    movsbl (%eax), %eax\n");
            } else if (inst->value == 2) {
                sb_append(&out, "    movswl (%eax), %eax\n");
            } else {
                sb_append(&out, "    movl (%eax), %eax\n");
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL ||
                   inst->op == IR_DIV || inst->op == IR_MOD || inst->op == IR_EQEQ || inst->op == IR_NOTEQ ||
                   inst->op == IR_LT || inst->op == IR_GT || inst->op == IR_LTEQ ||
                   inst->op == IR_ULT || inst->op == IR_UGT || inst->op == IR_ULTEQ ||
                   inst->op == IR_UGTEQ || inst->op == IR_UGTGT ||
                   inst->op == IR_GTEQ || inst->op == IR_INDEX ||
                   inst->op == IR_AND || inst->op == IR_OR || inst->op == IR_XOR ||
                   inst->op == IR_LTLT || inst->op == IR_GTGT) {
            
            sb_append(&out, "    popl %ecx\n");
            sb_append(&out, "    popl %eax\n");
            if (inst->op == IR_INDEX) {
                if (inst->value == 1) {
                    sb_append(&out, "    movsbl (%eax,%ecx,1), %eax\n");
                } else if (inst->value == 2) {
                    sb_append(&out, "    movswl (%eax,%ecx,2), %eax\n");
                } else if (inst->value == 4) {
                    sb_append(&out, "    movl (%eax,%ecx,4), %eax\n");
                } else {
                    sb_append(&out, "    movl (%eax,%ecx,8), %eax\n");
                }
            } else if (inst->op == IR_ADD)
                sb_append(&out, "    addl %ecx, %eax\n");
            else if (inst->op == IR_SUB)
                sb_append(&out, "    subl %ecx, %eax\n");
            else if (inst->op == IR_MUL)
                sb_append(&out, "    imull %ecx, %eax\n");
            else if (inst->op == IR_DIV) {
                sb_append(&out, "    cltd\n");
                sb_append(&out, "    idivl %ecx\n");
            } else if (inst->op == IR_MOD) {
                sb_append(&out, "    cltd\n");
                sb_append(&out, "    idivl %ecx\n");
                sb_append(&out, "    movl %edx, %eax\n");
            } else if (inst->op == IR_AND)
                sb_append(&out, "    andl %ecx, %eax\n");
            else if (inst->op == IR_OR)
                sb_append(&out, "    orl %ecx, %eax\n");
            else if (inst->op == IR_XOR)
                sb_append(&out, "    xorl %ecx, %eax\n");
            else if (inst->op == IR_LTLT || inst->op == IR_GTGT || inst->op == IR_UGTGT) {
                if (inst->op == IR_LTLT)
                    sb_append(&out, "    shll %cl, %eax\n");
                else if (inst->op == IR_GTGT)
                    sb_append(&out, "    sarl %cl, %eax\n");
                else
                    sb_append(&out, "    shrl %cl, %eax\n");
            } else {
                sb_append(&out, "    cmpl %ecx, %eax\n");
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
                sb_append(&out, "    movzbl %al, %eax\n");
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_TILDE || inst->op == IR_NOT || inst->op == IR_NEG ||
                   inst->op == IR_CAST || inst->op == IR_UCAST) {
            sb_append(&out, "    popl %eax\n");
            if (inst->op == IR_TILDE)
                sb_append(&out, "    notl %eax\n");
            else if (inst->op == IR_NEG)
                sb_append(&out, "    negl %eax\n");
            else if (inst->op == IR_CAST) {
                if (inst->value == 1)
                    sb_append(&out, "    movsbl %al, %eax\n");
                else if (inst->value == 2)
                    sb_append(&out, "    movswl %ax, %eax\n");
            } else if (inst->op == IR_UCAST) {
                if (inst->value == 1)
                    sb_append(&out, "    movzbl %al, %eax\n");
                else if (inst->value == 2)
                    sb_append(&out, "    movzwl %ax, %eax\n");
            } else {
                sb_append(&out, "    cmpl $0, %eax\n");
                sb_append(&out, "    sete %al\n");
                sb_append(&out, "    movzbl %al, %eax\n");
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_JZ) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    cmpl $0, %eax\n");
            sb_appendf(&out, "    je %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_JZ64) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %edx\n");
            sb_append(&out, "    orl %edx, %eax\n");
            sb_appendf(&out, "    je %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_JMP) {
            sb_appendf(&out, "    jmp %s\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_LABEL) {
            sb_appendf(&out, "%s:\n", ir_arg_str(inst->arg));
        } else if (inst->op == IR_CALL || inst->op == IR_ICALL) {
            long num_args = inst->value;
            int ret_agg_size = 0;
            int ret_i64 = 0;
            if (inst->op == IR_CALL) {
                HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, ir_arg_str(inst->arg));
                if (entry) ret_agg_size = entry->val_int;
                entry = hashmap_get(&ir_function_return_int_sizes, ir_arg_str(inst->arg));
                if (entry && entry->val_int == 8) ret_i64 = 1;
            }

            /* Float-aware i386 call (System V i386: all args on the stack, float
               and double returned in st0). Float/double eval values are 8 bytes;
               int/pointer eval values are 4 bytes. */
            {
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
                int any_f = 0;
                for (long i = 0; i < num_args; ++i) {
                    if (pf && i < pf->count && pf->data[i]) any_f = 1;
                }
                if (ret_agg_size == 0 && (any_f || retf)) {
                    int *evsz = calloc(num_args + 1, sizeof(int));
                    int *abisz = calloc(num_args + 1, sizeof(int));
                    int *abioff = calloc(num_args + 1, sizeof(int));
                    int total_abi = 0, total_ev = 0;
                    for (long i = 0; i < num_args; ++i) {
                        int f = (pf && i < pf->count) ? pf->data[i] : 0;
                        evsz[i] = f ? 8 : 4;          /* float/double eval value is 8 bytes */
                        abisz[i] = (f == 8) ? 8 : 4;  /* double 8, float 4, int 4 */
                        abioff[i] = total_abi;
                        total_abi += abisz[i];
                        total_ev += evsz[i];
                    }
                    sb_appendf(&out, "    subl $%d, %%esp\n", total_abi);
                    for (long i = 0; i < num_args; ++i) {
                        int f = (pf && i < pf->count) ? pf->data[i] : 0;
                        int ev_above = 0;
                        for (long j = i + 1; j < num_args; ++j) ev_above += evsz[j];
                        int ev_src = total_abi + ev_above;  /* offset from current esp */
                        if (f == 8) {
                            sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src);
                            sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i]);
                            sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src + 4);
                            sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i] + 4);
                        } else if (f == 4) {
                            sb_appendf(&out, "    fldl %d(%%esp)\n", ev_src);
                            sb_appendf(&out, "    fstps %d(%%esp)\n", abioff[i]);
                        } else {
                            sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src);
                            sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i]);
                        }
                    }
                    if (inst->op == IR_ICALL) {
                        sb_appendf(&out, "    movl %d(%%esp), %%eax\n", total_abi + total_ev);
                        sb_append(&out, "    call *%eax\n");
                    } else {
                        sb_appendf(&out, "    call %s%s\n", ir_arg_str(inst->arg), ir_pic_mode ? "@PLT" : "");
                    }
                    sb_appendf(&out, "    addl $%d, %%esp\n", total_abi + total_ev + (inst->op == IR_ICALL ? 4 : 0));
                    if (retf) {
                        sb_append(&out, "    subl $8, %esp\n");
                        sb_append(&out, "    fstpl (%esp)\n");
                    } else {
                        sb_append(&out, "    pushl %eax\n");
                    }
                    free(evsz); free(abisz); free(abioff);
                    continue;
                }
            }

            if (inst->op == IR_ICALL) {
                sb_appendf(&out, "    subl $%ld, %%esp\n", num_args * 4);
                for (long i = 0; i < num_args; ++i) {
                    sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args - 1 - i) * 4);
                    sb_appendf(&out, "    movl %%eax, %ld(%%esp)\n", i * 4);
                }
                sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args) * 4);
                sb_append(&out, "    call *%eax\n");
                sb_appendf(&out, "    addl $%ld, %%esp\n", (2 * num_args + 1) * 4);
                sb_append(&out, "    pushl %eax\n");
            } else if (ret_agg_size > 0) {
                int ret_val_bytes = ((ret_agg_size + 15) / 16) * 16;
                sb_appendf(&out, "    subl $%d, %%esp\n", ret_val_bytes);
                int stack_bytes = (num_args + 1) * 4;
                sb_appendf(&out, "    subl $%d, %%esp\n", stack_bytes);
                
                // Compute return buffer address and store in first slot
                sb_appendf(&out, "    leal %d(%%esp), %%eax\n", stack_bytes);
                sb_append(&out, "    movl %eax, (%esp)\n");

                // Copy actual arguments
                for (long i = 0; i < num_args; ++i) {
                    sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", stack_bytes + ret_val_bytes + (num_args - 1 - i) * 4);
                    sb_appendf(&out, "    movl %%eax, %ld(%%esp)\n", (i + 1) * 4);
                }
                sb_appendf(&out, "    call %s%s\n", ir_arg_str(inst->arg), ir_pic_mode ? "@PLT" : "");
                // Callee pops hidden pointer, so we only pop num_args * 4
                sb_appendf(&out, "    addl $%ld, %%esp\n", num_args * 4);
                // The return value buffer is now at the top of the stack. We do not push %eax.
            } else {
                IntArray *pis = nullptr;
                HashMapEntry *pie = hashmap_get(&ir_function_param_int_sizes, ir_arg_str(inst->arg));
                if (pie) pis = (IntArray *)pie->val_ptr;
                int any_i64_arg = 0;
                int total_ev = 0;
                int total_abi = 0;
                int *evsz = calloc(num_args + 1, sizeof(int));
                int *abisz = calloc(num_args + 1, sizeof(int));
                int *abioff = calloc(num_args + 1, sizeof(int));
                for (long i = 0; i < num_args; ++i) {
                    int sz = (pis && i < pis->count) ? pis->data[i] : 4;
                    evsz[i] = (sz == 8) ? 8 : 4;
                    abisz[i] = (sz == 8) ? 8 : 4;
                    abioff[i] = total_abi;
                    total_ev += evsz[i];
                    total_abi += abisz[i];
                    if (sz == 8) any_i64_arg = 1;
                }
                sb_appendf(&out, "    subl $%d, %%esp\n", total_abi);
                for (long i = 0; i < num_args; ++i) {
                    int ev_above = 0;
                    for (long j = i + 1; j < num_args; ++j) ev_above += evsz[j];
                    int ev_src = total_abi + ev_above;
                    if (abisz[i] == 8) {
                        sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src);
                        sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i]);
                        sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src + 4);
                        sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i] + 4);
                    } else {
                        sb_appendf(&out, "    movl %d(%%esp), %%eax\n", ev_src);
                        sb_appendf(&out, "    movl %%eax, %d(%%esp)\n", abioff[i]);
                    }
                }
                sb_appendf(&out, "    call %s%s\n", ir_arg_str(inst->arg), ir_pic_mode ? "@PLT" : "");
                sb_appendf(&out, "    addl $%d, %%esp\n", total_abi + total_ev);
                if (ret_i64) {
                    sb_append(&out, "    pushl %edx\n");
                    sb_append(&out, "    pushl %eax\n");
                } else {
                    sb_append(&out, "    pushl %eax\n");
                }
                (void)any_i64_arg;
                free(evsz); free(abisz); free(abioff);
            }
        } else if (inst->op == IR_ADDR) {
            sb_appendf(&out, "    leal -%ld(%%ebp), %%eax\n", (inst->value + 1) * 16);
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_GLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 4;
            int g_is_unsigned = ir_get_var_unsigned(ir_arg_str(inst->arg));
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    movl (%eax), %eax\n");
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzbl %al, %eax\n");
                    else
                        sb_append(&out, "    movsbl %al, %eax\n");
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_append(&out, "    movzwl %ax, %eax\n");
                    else
                        sb_append(&out, "    movswl %ax, %eax\n");
                }
                sb_append(&out, "    pushl %eax\n");
            } else {
                if (gsize == 1) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzbl %s, %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movsbl %s, %%eax\n", ir_arg_str(inst->arg));
                } else if (gsize == 2) {
                    if (g_is_unsigned)
                        sb_appendf(&out, "    movzwl %s, %%eax\n", ir_arg_str(inst->arg));
                    else
                        sb_appendf(&out, "    movswl %s, %%eax\n", ir_arg_str(inst->arg));
                } else
                    sb_appendf(&out, "    movl %s, %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    pushl %eax\n");
            }
        } else if (inst->op == IR_GLOAD64) {
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    movl 4(%eax), %eax\n");
                sb_append(&out, "    pushl %eax\n");
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    movl (%eax), %eax\n");
                sb_append(&out, "    pushl %eax\n");
            } else {
                sb_appendf(&out, "    movl %s+4, %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    pushl %eax\n");
                sb_appendf(&out, "    movl %s, %%eax\n", ir_arg_str(inst->arg));
                sb_append(&out, "    pushl %eax\n");
            }
        } else if (inst->op == IR_GSTORE) {
            sb_append(&out, "    popl %eax\n");
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 4;
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%ecx\n", ir_arg_str(inst->arg));
                if (gsize == 1)
                    sb_append(&out, "    movb %al, (%ecx)\n");
                else if (gsize == 2)
                    sb_append(&out, "    movw %ax, (%ecx)\n");
                else
                    sb_append(&out, "    movl %eax, (%ecx)\n");
            } else {
                if (gsize == 1)
                    sb_appendf(&out, "    movb %%al, %s\n", ir_arg_str(inst->arg));
                else if (gsize == 2)
                    sb_appendf(&out, "    movw %%ax, %s\n", ir_arg_str(inst->arg));
                else
                    sb_appendf(&out, "    movl %%eax, %s\n", ir_arg_str(inst->arg));
            }
        } else if (inst->op == IR_GSTORE64) {
            sb_append(&out, "    popl %eax\n");
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%ecx\n", ir_arg_str(inst->arg));
                sb_append(&out, "    movl %eax, (%ecx)\n");
                sb_append(&out, "    popl %eax\n");
                sb_append(&out, "    movl %eax, 4(%ecx)\n");
            } else {
                sb_appendf(&out, "    movl %%eax, %s\n", ir_arg_str(inst->arg));
                sb_append(&out, "    popl %eax\n");
                sb_appendf(&out, "    movl %%eax, %s+4\n", ir_arg_str(inst->arg));
            }
        } else if (inst->op == IR_GADDR) {
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
            } else {
                sb_appendf(&out, "    movl $%s, %%eax\n", ir_arg_str(inst->arg));
            }
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_STORE_INDEX || inst->op == IR_STORE_INDEX_KEEP) {
            int keep_value = inst->op == IR_STORE_INDEX_KEEP;
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
            if (keep_value) {
                sb_append(&out, "    pushl %edx\n");
            }
        } else if (inst->op == IR_STORE_AGG) {
            sb_append(&out, "    popl %edx\n"); // dest_addr
            i386_emit_block_copy(&out, "%esp", "%edx", inst->value);
            int ret_bytes = ((inst->value + 15) / 16) * 16;
            sb_appendf(&out, "    addl $%d, %%esp\n", ret_bytes);
        } else if (inst->op == IR_COPY) {
            sb_append(&out, "    popl %edx\n"); // dest_addr
            sb_append(&out, "    popl %eax\n"); // src_addr
            i386_emit_block_copy(&out, "%eax", "%edx", inst->value);
        } else if (inst->op == IR_RET_AGG) {
            int ret_size = inst->value;
            sb_append(&out, "    popl %ecx\n"); // src_addr
            int off = -((fn->locals.size + 1) * 16);
            sb_appendf(&out, "    movl %d(%%ebp), %%edx\n", off); // dest_addr (hidden pointer)
            i386_emit_block_copy(&out, "%ecx", "%edx", ret_size);
            sb_append(&out, "    movl %edx, %eax\n"); // Return hidden pointer in eax
            if (frame) {
                if (ir_pic_mode)
                    sb_append(&out, "    popl %ebx\n");
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret $4\n");
        } else if (inst->op == IR_RET) {
            sb_append(&out, "    popl %eax\n");
            if (frame) {
                if (ir_pic_mode)
                    sb_append(&out, "    popl %ebx\n");
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_RET64) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    popl %edx\n");
            if (frame) {
                if (ir_pic_mode)
                    sb_append(&out, "    popl %ebx\n");
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret\n");
        } else if (inst->op == IR_EXTRACT_BITS) {
            int bf_offset = (int)(inst->value & 0xFFFF);
            int bf_width  = (int)((inst->value >> 16) & 0xFFFF);
            sb_append(&out, "    popl %eax\n");
            if (bf_offset > 0) {
                sb_appendf(&out, "    shrl $%d, %%eax\n", bf_offset);
            }
            long mask = (bf_width < 32) ? ((1L << bf_width) - 1) : 0xFFFFFFFFL;
            sb_appendf(&out, "    andl $%ld, %%eax\n", mask);
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_INSERT_BITS) {
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
        } else if (inst->op == IR_FCONST) {
            /* push the 8-byte double bit pattern: high word first so the low
               word ends up at the lower (top-of-stack) address. */
            unsigned long long bits = (unsigned long long)inst->value;
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)(bits >> 32));
            sb_append(&out, "    pushl %eax\n");
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)(bits & 0xffffffffU));
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_FADD || inst->op == IR_FSUB ||
                   inst->op == IR_FMUL || inst->op == IR_FDIV) {
            /* GAS AT&T swaps the operands of the non-commutative fsubp/fdivp, so
               use the reverse forms to get lhs OP rhs = st(1) OP st(0). */
            const char *fop = inst->op == IR_FADD ? "faddp" :
                              inst->op == IR_FSUB ? "fsubrp" :
                              inst->op == IR_FMUL ? "fmulp" : "fdivrp";
            sb_append(&out, "    fldl 8(%esp)\n");   /* lhs -> st0 */
            sb_append(&out, "    fldl (%esp)\n");    /* rhs -> st0, lhs -> st1 */
            sb_appendf(&out, "    %s %%st, %%st(1)\n", fop); /* st1 = lhs OP rhs, pop */
            sb_append(&out, "    addl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FNEG) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    fchs\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_I2F) {
            sb_append(&out, "    fildl (%esp)\n");
            sb_append(&out, "    subl $4, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_F2I) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $4, %esp\n");
            sb_append(&out, "    fisttpl (%esp)\n");  /* truncating store (SSE3) */
        } else if (inst->op == IR_D2F) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    fstps (%esp)\n");
            sb_append(&out, "    flds (%esp)\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FLT || inst->op == IR_FGT ||
                   inst->op == IR_FLTEQ || inst->op == IR_FGTEQ ||
                   inst->op == IR_FEQEQ || inst->op == IR_FNOTEQ) {
            sb_append(&out, "    fldl (%esp)\n");     /* rhs -> st0 */
            sb_append(&out, "    fldl 8(%esp)\n");    /* lhs -> st0, rhs -> st1 */
            sb_append(&out, "    addl $16, %esp\n");
            sb_append(&out, "    fucomip %st(1), %st\n"); /* compare lhs vs rhs, pop lhs */
            sb_append(&out, "    fstp %st(0)\n");          /* pop rhs */
            const char *cc = inst->op == IR_FLT ? "setb" :
                             inst->op == IR_FLTEQ ? "setbe" :
                             inst->op == IR_FGT ? "seta" :
                             inst->op == IR_FGTEQ ? "setae" :
                             inst->op == IR_FEQEQ ? "sete" : "setne";
            sb_appendf(&out, "    %s %%al\n", cc);
            sb_append(&out, "    movzbl %al, %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (inst->op == IR_FLOAD4) {
            sb_appendf(&out, "    flds -%ld(%%ebp)\n", (inst->value + 1) * 16);
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FLOAD8) {
            sb_appendf(&out, "    fldl -%ld(%%ebp)\n", (inst->value + 1) * 16);
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FSTORE4) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            sb_appendf(&out, "    fstps -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_FSTORE8) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            sb_appendf(&out, "    fstpl -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (inst->op == IR_FGLOAD) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 4;
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
                if (gsize == 4) sb_append(&out, "    flds (%eax)\n");
                else sb_append(&out, "    fldl (%eax)\n");
            } else {
                if (gsize == 4) sb_appendf(&out, "    flds %s\n", ir_arg_str(inst->arg));
                else sb_appendf(&out, "    fldl %s\n", ir_arg_str(inst->arg));
            }
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FGSTORE) {
            int gsize = ir_get_global_storage_size(ir_arg_str(inst->arg));
            if (gsize == 0) gsize = 4;
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            if (ir_pic_mode) {
                sb_appendf(&out, "    movl %s@GOT(%%ebx), %%eax\n", ir_arg_str(inst->arg));
                if (gsize == 4) sb_append(&out, "    fstps (%eax)\n");
                else sb_append(&out, "    fstpl (%eax)\n");
            } else {
                if (gsize == 4) sb_appendf(&out, "    fstps %s\n", ir_arg_str(inst->arg));
                else sb_appendf(&out, "    fstpl %s\n", ir_arg_str(inst->arg));
            }
        } else if (inst->op == IR_FLOAD_ADDR4 || inst->op == IR_FLOAD_ADDR8) {
            sb_append(&out, "    popl %eax\n");
            if (inst->op == IR_FLOAD_ADDR4) sb_append(&out, "    flds (%eax)\n");
            else sb_append(&out, "    fldl (%eax)\n");
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (inst->op == IR_FSTORE_ADDR4 || inst->op == IR_FSTORE_ADDR8) {
            sb_append(&out, "    popl %eax\n");
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            if (inst->op == IR_FSTORE_ADDR4) sb_append(&out, "    fstps (%eax)\n");
            else sb_append(&out, "    fstpl (%eax)\n");
        } else if (inst->op == IR_FRET) {
            /* return value in st0 (i386 returns float and double in st0) */
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            if (frame) {
                if (ir_pic_mode)
                    sb_append(&out, "    popl %ebx\n");
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
            if (ir_pic_mode)
                sb_append(&out, "    popl %ebx\n");
            sb_append(&out, "    leave\n");
        }
        if (fn->return_aggregate_size > 0) {
            sb_append(&out, "    ret $4\n");
        } else {
            sb_append(&out, "    ret\n");
        }
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

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

    int indirect_ret = (fn->return_aggregate_size > 0);
    int local_slots = fn->locals.size;
    if (indirect_ret) {
        local_slots++;
    }
    int frame = fn->has_call || (local_slots > 0);
    if (frame) {
        sb_append(&out, "    pushl %ebp\n");
        sb_append(&out, "    movl %esp, %ebp\n");
        if (local_slots > 0) {
            sb_appendf(&out, "    subl $%d, %%esp\n", local_slots * 16);
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
        int in_off = param_offset;
        for (int i = 0; i < fn->params.count; ++i) {
            int pf = (prologue_floats && i < prologue_floats->count) ? prologue_floats->data[i] : 0;
            int slot = -((i + 1) * 16);
            if (pf == 8) {
                /* double parameter: copy 8 bytes from the incoming stack */
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
            int ret_agg_size = 0;
            HashMapEntry *entry = hashmap_get(&ir_function_return_aggregate_sizes, inst->arg);
            if (entry) ret_agg_size = entry->val_int;

            /* Float-aware i386 call (System V i386: all args on the stack, float
               and double returned in st0). Float/double eval values are 8 bytes;
               int/pointer eval values are 4 bytes. */
            {
                IntArray *pf = nullptr;
                int retf = 0;
                HashMapEntry *pe = hashmap_get(&ir_function_param_floats, inst->arg);
                if (pe) pf = (IntArray *)pe->val_ptr;
                HashMapEntry *re = hashmap_get(&ir_function_return_floats, inst->arg);
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
                    sb_appendf(&out, "    call %s\n", inst->arg);
                    sb_appendf(&out, "    addl $%d, %%esp\n", total_abi + total_ev);
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

            if (ret_agg_size > 0) {
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
                sb_appendf(&out, "    call %s\n", inst->arg);
                // Callee pops hidden pointer, so we only pop num_args * 4
                sb_appendf(&out, "    addl $%ld, %%esp\n", num_args * 4);
                // The return value buffer is now at the top of the stack. We do not push %eax.
            } else {
                sb_appendf(&out, "    subl $%ld, %%esp\n", num_args * 4);
                for (long i = 0; i < num_args; ++i) {
                    sb_appendf(&out, "    movl %ld(%%esp), %%eax\n", (2 * num_args - 1 - i) * 4);
                    sb_appendf(&out, "    movl %%eax, %ld(%%esp)\n", i * 4);
                }
                sb_appendf(&out, "    call %s\n", inst->arg);
                sb_appendf(&out, "    addl $%ld, %%esp\n", 2 * num_args * 4);
                sb_append(&out, "    pushl %eax\n");
            }
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
        } else if (strcmp(inst->op, "store_agg") == 0) {
            sb_append(&out, "    popl %edx\n"); // dest_addr
            i386_emit_block_copy(&out, "%esp", "%edx", inst->value);
            int ret_bytes = ((inst->value + 15) / 16) * 16;
            sb_appendf(&out, "    addl $%d, %%esp\n", ret_bytes);
        } else if (strcmp(inst->op, "copy") == 0) {
            sb_append(&out, "    popl %edx\n"); // dest_addr
            sb_append(&out, "    popl %eax\n"); // src_addr
            i386_emit_block_copy(&out, "%eax", "%edx", inst->value);
        } else if (strcmp(inst->op, "ret_agg") == 0) {
            int ret_size = inst->value;
            sb_append(&out, "    popl %ecx\n"); // src_addr
            int off = -((fn->locals.size + 1) * 16);
            sb_appendf(&out, "    movl %d(%%ebp), %%edx\n", off); // dest_addr (hidden pointer)
            i386_emit_block_copy(&out, "%ecx", "%edx", ret_size);
            sb_append(&out, "    movl %%edx, %%eax\n"); // Return hidden pointer in eax
            if (frame) {
                sb_append(&out, "    leave\n");
            }
            sb_append(&out, "    ret $4\n");
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
        } else if (strcmp(inst->op, "fconst") == 0) {
            /* push the 8-byte double bit pattern: high word first so the low
               word ends up at the lower (top-of-stack) address. */
            unsigned long long bits = (unsigned long long)inst->value;
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)(bits >> 32));
            sb_append(&out, "    pushl %eax\n");
            sb_appendf(&out, "    movl $%u, %%eax\n", (unsigned)(bits & 0xffffffffU));
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "fadd") == 0 || strcmp(inst->op, "fsub") == 0 ||
                   strcmp(inst->op, "fmul") == 0 || strcmp(inst->op, "fdiv") == 0) {
            /* GAS AT&T swaps the operands of the non-commutative fsubp/fdivp, so
               use the reverse forms to get lhs OP rhs = st(1) OP st(0). */
            const char *fop = strcmp(inst->op, "fadd") == 0 ? "faddp" :
                              strcmp(inst->op, "fsub") == 0 ? "fsubrp" :
                              strcmp(inst->op, "fmul") == 0 ? "fmulp" : "fdivrp";
            sb_append(&out, "    fldl 8(%esp)\n");   /* lhs -> st0 */
            sb_append(&out, "    fldl (%esp)\n");    /* rhs -> st0, lhs -> st1 */
            sb_appendf(&out, "    %s %%st, %%st(1)\n", fop); /* st1 = lhs OP rhs, pop */
            sb_append(&out, "    addl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "fneg") == 0) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    fchs\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "i2f") == 0) {
            sb_append(&out, "    fildl (%esp)\n");
            sb_append(&out, "    subl $4, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "f2i") == 0) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $4, %esp\n");
            sb_append(&out, "    fisttpl (%esp)\n");  /* truncating store (SSE3) */
        } else if (strcmp(inst->op, "d2f") == 0) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    fstps (%esp)\n");
            sb_append(&out, "    flds (%esp)\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "f<") == 0 || strcmp(inst->op, "f>") == 0 ||
                   strcmp(inst->op, "f<=") == 0 || strcmp(inst->op, "f>=") == 0 ||
                   strcmp(inst->op, "f==") == 0 || strcmp(inst->op, "f!=") == 0) {
            sb_append(&out, "    fldl (%esp)\n");     /* rhs -> st0 */
            sb_append(&out, "    fldl 8(%esp)\n");    /* lhs -> st0, rhs -> st1 */
            sb_append(&out, "    addl $16, %esp\n");
            sb_append(&out, "    fucomip %st(1), %st\n"); /* compare lhs vs rhs, pop lhs */
            sb_append(&out, "    fstp %st(0)\n");          /* pop rhs */
            const char *cc = strcmp(inst->op, "f<") == 0 ? "setb" :
                             strcmp(inst->op, "f<=") == 0 ? "setbe" :
                             strcmp(inst->op, "f>") == 0 ? "seta" :
                             strcmp(inst->op, "f>=") == 0 ? "setae" :
                             strcmp(inst->op, "f==") == 0 ? "sete" : "setne";
            sb_appendf(&out, "    %s %%al\n", cc);
            sb_append(&out, "    movzbl %al, %eax\n");
            sb_append(&out, "    pushl %eax\n");
        } else if (strcmp(inst->op, "fload4") == 0) {
            sb_appendf(&out, "    flds -%ld(%%ebp)\n", (inst->value + 1) * 16);
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "fload8") == 0) {
            sb_appendf(&out, "    fldl -%ld(%%ebp)\n", (inst->value + 1) * 16);
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "fstore4") == 0) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            sb_appendf(&out, "    fstps -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (strcmp(inst->op, "fstore8") == 0) {
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            sb_appendf(&out, "    fstpl -%ld(%%ebp)\n", (inst->value + 1) * 16);
        } else if (strcmp(inst->op, "fgload") == 0) {
            int gsize = 4;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;
            if (gsize == 4) sb_appendf(&out, "    flds %s\n", inst->arg);
            else sb_appendf(&out, "    fldl %s\n", inst->arg);
            sb_append(&out, "    subl $8, %esp\n");
            sb_append(&out, "    fstpl (%esp)\n");
        } else if (strcmp(inst->op, "fgstore") == 0) {
            int gsize = 4;
            HashMapEntry *ge = hashmap_get(&ir_global_var_elem_scales, inst->arg);
            if (ge) gsize = ge->val_int;
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            if (gsize == 4) sb_appendf(&out, "    fstps %s\n", inst->arg);
            else sb_appendf(&out, "    fstpl %s\n", inst->arg);
        } else if (strcmp(inst->op, "fret") == 0) {
            /* return value in st0 (i386 returns float and double in st0) */
            sb_append(&out, "    fldl (%esp)\n");
            sb_append(&out, "    addl $8, %esp\n");
            if (frame)
                sb_append(&out, "    leave\n");
            sb_append(&out, "    ret\n");
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

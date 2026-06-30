#include "backend.h"
#include "backend_target.h"
#include "lexer.h"
#include "parser.h"
#include "preprocessor.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump_ast_node(const Node *node, int indent) {
    for (int k = 0; k < indent * 2; ++k) fprintf(stderr, " ");
    fprintf(stderr, "Node(op=%s", node->op);
    if (node->name && node->name[0]) fprintf(stderr, ", name=%s", node->name);
    if (node->value != 0) fprintf(stderr, ", value=%ld", node->value);
    fprintf(stderr, ")\n");
    if (node->lhs) dump_ast_node(node->lhs, indent + 1);
    if (node->rhs) dump_ast_node(node->rhs, indent + 1);
    for (int k = 0; k < node->body.count; ++k) {
        if (node->body.data[k]) dump_ast_node(node->body.data[k], indent + 1);
    }
}

static void dump_ast(NodeArray ast) {
    fprintf(stderr, "=== AST DUMP ===\n");
    for (int k = 0; k < ast.count; ++k) {
        if (ast.data[k]) dump_ast_node(ast.data[k], 0);
    }
    fprintf(stderr, "================\n");
}

static void dump_ir(IrFunctionArray funcs) {
    fprintf(stderr, "=== IR DUMP ===\n");
    for (int f_i = 0; f_i < funcs.count; ++f_i) {
        IrFunction fn = funcs.data[f_i];
        fprintf(stderr, "Function %s:\n", fn.name);
        for (int i_i = 0; i_i < fn.code.count; ++i_i) {
            IrInst inst = fn.code.data[i_i];
            fprintf(stderr, "  %s", inst.op);
            if (inst.arg && inst.arg[0]) fprintf(stderr, " %s", inst.arg);
            if (inst.value != 0) fprintf(stderr, " %ld", inst.value);
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "================\n");
}

static const char *peephole_optimize(const char *asm_text, const char *target, Arena *arena) {
    StringArray lines;
    string_array_init(&lines);
    
    size_t len = strlen(asm_text);
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && asm_text[i] != '\n') {
            i++;
        }
        size_t end = i;
        while (end > start && (asm_text[end - 1] == '\r' || asm_text[end - 1] == ' ')) {
            end--;
        }
        string_array_push(&lines, arena_strndup(arena, asm_text + start, end - start));
        if (i < len && asm_text[i] == '\n') {
            i++;
        }
    }

    StringBuilder out;
    sb_init(&out);
    
    for (int idx = 0; idx < lines.count; ++idx) {
        const char *cur = lines.data[idx];
        if (!cur[0]) {
            sb_append(&out, "\n");
            continue;
        }

        int optimized = 0;
        if (idx + 1 < lines.count) {
            const char *next = lines.data[idx + 1];
            
            const char *cur_trim = cur;
            while (*cur_trim == ' ' || *cur_trim == '\t') cur_trim++;
            const char *next_trim = next;
            while (*next_trim == ' ' || *next_trim == '\t') next_trim++;

            if (strcmp(target, "arm64-darwin") == 0) {
                if (strncmp(cur_trim, "str ", 4) == 0 && strstr(cur_trim, ", [sp, #-16]!") != nullptr &&
                    strncmp(next_trim, "ldr ", 4) == 0 && strstr(next_trim, ", [sp], #16") != nullptr) {
                    
                    const char *comma1 = strchr(cur_trim, ',');
                    const char *comma2 = strchr(next_trim, ',');
                    if (comma1 && comma2) {
                        size_t len1 = comma1 - (cur_trim + 4);
                        size_t len2 = comma2 - (next_trim + 4);
                        if (len1 == len2 && strncmp(cur_trim + 4, next_trim + 4, len1) == 0) {
                            idx++;
                            optimized = 1;
                        }
                    }
                }
            } else if (strcmp(target, "x86_64-b1nix") == 0) {
                if (strncmp(cur_trim, "pushq ", 6) == 0 && strncmp(next_trim, "popq ", 5) == 0) {
                    const char *cur_reg = cur_trim + 6;
                    const char *next_reg = next_trim + 5;
                    if (strcmp(cur_reg, next_reg) == 0) {
                        idx++;
                        optimized = 1;
                    }
                }
            } else if (strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0) {
                if (strncmp(cur_trim, "pushl ", 6) == 0 && strncmp(next_trim, "popl ", 5) == 0) {
                    const char *cur_reg = cur_trim + 6;
                    const char *next_reg = next_trim + 5;
                    if (strcmp(cur_reg, next_reg) == 0) {
                        idx++;
                        optimized = 1;
                    }
                }
            }
        }

        if (!optimized) {
            sb_append(&out, cur);
            sb_append(&out, "\n");
        }
    }

    string_array_free(&lines);
    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

const char *backend_compile_asm(const char *src, const char *target, int dump_ast_flag, int dump_ir_flag, Arena *arena) {
    ir_reset_state();

    int target_scale = (strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0) ? 4 : 8;

    HashMap macros;
    hashmap_init(&macros, 64);
    for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
        HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
        while (curr) {
            hashmap_put(&macros, curr->key, curr->val_ptr, curr->val_int);
            curr = curr->next;
        }
    }

    HashMap included_files;
    hashmap_init(&included_files, 32);
    hashmap_put(&included_files, diagnostics_filepath, nullptr, 1);

    const char *preprocessed_src = preprocessor_preprocess(src, diagnostics_filepath, &preprocessor_driver_include_dirs, &macros, &included_files, arena);
    hashmap_free(&included_files);

    TokenArray tokens = lex(preprocessed_src, &macros, nullptr, arena);
    hashmap_free(&macros);

    NodeArray ast = parser_parse(tokens, target_scale, arena);
    token_array_free(&tokens);

    if (dump_ast_flag) {
        dump_ast(ast);
    }

    IrFunctionArray ir_functions = ir_lower_program(ast, target, arena);
    // free AST nodes array (since they are in arena we don't free individual nodes)
    node_array_free(&ast);

    if (dump_ir_flag) {
        dump_ir(ir_functions);
    }

    TargetBackend *backend = nullptr;
    if (strcmp(target, "arm64-darwin") == 0) {
        backend = backend_create_arm64();
    } else if (strcmp(target, "x86_64-b1nix") == 0) {
        backend = backend_create_x86_64();
    } else if (strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0) {
        backend = backend_create_i386();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown target %s", target);
        diagnostics_fatal(msg);
    }

    StringBuilder out;
    sb_init(&out);

    sb_append(&out, backend->emit_globals(backend, &ir_global_decls, arena));

    for (int k = 0; k < ir_functions.count; ++k) {
        sb_append(&out, backend->emit_function(backend, &ir_functions.data[k], arena));
    }

    // Free ir functions
    for (int k = 0; k < ir_functions.count; ++k) {
        string_array_free(&ir_functions.data[k].params);
        int_array_free(&ir_functions.data[k].param_aggregate_sizes);
        ir_inst_array_free(&ir_functions.data[k].code);
        hashmap_free(&ir_functions.data[k].locals);
        string_pair_array_free(&ir_functions.data[k].strings);
    }
    ir_function_array_free(&ir_functions);

    const char *final_asm = peephole_optimize(sb_to_string(&out, arena), target, arena);
    sb_free(&out);

    backend->free(backend);

    return final_asm;
}

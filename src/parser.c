#include "parser.h"
#include "diagnostics.h"
#include "ir.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *expr(ParserState *p);
static Node *block_stmt(ParserState *p);
static Node *stmt(ParserState *p);
static void parser_error(const ParserState *p, const char *msg);

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static const char *peek(const ParserState *p) {
    if (p->pos >= p->tokens.count) return "";
    return p->tokens.data[p->pos].text;
}

static const char *take(ParserState *p, const char *want) {
    const char *t = peek(p);
    if (want && strcmp(t, want) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected '%s' but got '%s'", want, t);
        parser_error(p, msg);
    }
    p->pos++;
    return t;
}

static void parser_error(const ParserState *p, const char *msg) {
    int line = 1, col = 1;
    if (p->pos < p->tokens.count) {
        line = p->tokens.data[p->pos].line;
        col = p->tokens.data[p->pos].col;
        fprintf(stderr, "[DEBUG] Error at token %d: '%s'\n", p->pos, p->tokens.data[p->pos].text);
        fprintf(stderr, "Context:");
        for (int idx = (p->pos > 5 ? p->pos - 5 : 0); idx < (p->pos + 5 < p->tokens.count ? p->pos + 5 : p->tokens.count); ++idx) {
            fprintf(stderr, " %s", p->tokens.data[idx].text);
        }
        fprintf(stderr, "\n");
    }
    diagnostics_error(line, col, msg);
}

static void parser_enter_scope(ParserState *p) {
    if (p->scope_count >= p->scope_capacity) {
        p->scope_capacity = p->scope_capacity * 2 + 4;
        p->scopes = realloc(p->scopes, p->scope_capacity * sizeof(HashMap));
    }
    hashmap_init(&p->scopes[p->scope_count++], 16);
}

static void parser_exit_scope(ParserState *p) {
    if (p->scope_count > 0) {
        p->scope_count--;
        hashmap_free(&p->scopes[p->scope_count]);
    }
}

static const char *resolve_name(const ParserState *p, const char *name) {
    for (int idx = p->scope_count - 1; idx >= 0; --idx) {
        HashMapEntry *entry = hashmap_get(&p->scopes[idx], name);
        if (entry) {
            return (const char *)entry->val_ptr;
        }
    }
    return name;
}

static Node *create_node(ParserState *p, const char *op, int line, int col) {
    Node *node = arena_alloc(p->arena, sizeof(struct Node));
    node->op = op;
    node->name = "";
    node->value = 0;
    node->is_static = 0;
    node->lhs = nullptr;
    node->rhs = nullptr;
    node_array_init(&node->body);
    string_array_init(&node->params);
    int_array_init(&node->param_aggregate_sizes);
    int_array_init(&node->param_floats);
    node->aggregate_size = 0;
    node->type_tag = "";
    long_array_init(&node->array_dims);
    node->elem_size = 0;
    node->pointee_size = 0;
    node->is_unsigned = 0;
    node->type_size = 8;
    node->is_bool = 0;
    node->is_float = 0;
    node->fvalue = 0.0;
    node->bit_offset = 0;
    node->bit_width = 0;
    node->line = line;
    node->col = col;
    return node;
}

static const char *infer_struct_tag(const ParserState *p, const Node *node) {
    if (!node) return "";
    if (strcmp(node->op, "var") == 0) {
        HashMapEntry *entry = hashmap_get((HashMap *)&p->var_struct_tags, node->name);
        return entry ? (const char *)entry->val_ptr : "";
    }
    if (strcmp(node->op, "index") == 0 && node->type_tag && node->type_tag[0]) {
        return node->type_tag;
    }
    if (strcmp(node->op, "unary_*") == 0 && node->lhs) {
        return infer_struct_tag(p, node->lhs);
    }
    return "";
}

static long eval_const(ParserState *p, const Node *node);
static long sizeof_expr(const ParserState *p, const Node *node);

static long eval_const(ParserState *p, const Node *node) {
    if (strcmp(node->op, "num") == 0) return node->value;
    if (strcmp(node->op, "char") == 0) return node->value;
    if (strcmp(node->op, "unary_-") == 0) return -eval_const(p, node->lhs);
    if (strcmp(node->op, "unary_~") == 0) return ~eval_const(p, node->lhs);
    if (strcmp(node->op, "unary_!") == 0) return !eval_const(p, node->lhs);
    if (strcmp(node->op, "+") == 0) return eval_const(p, node->lhs) + eval_const(p, node->rhs);
    if (strcmp(node->op, "-") == 0) {
        if (node->rhs) return eval_const(p, node->lhs) - eval_const(p, node->rhs);
        return -eval_const(p, node->lhs);
    }
    if (strcmp(node->op, "*") == 0) return eval_const(p, node->lhs) * eval_const(p, node->rhs);
    if (strcmp(node->op, "/") == 0) {
        long divisor = eval_const(p, node->rhs);
        if (divisor == 0) parser_error(p, "division by zero in constant expression");
        return eval_const(p, node->lhs) / divisor;
    }
    if (strcmp(node->op, "==") == 0) return eval_const(p, node->lhs) == eval_const(p, node->rhs);
    if (strcmp(node->op, "!=") == 0) return eval_const(p, node->lhs) != eval_const(p, node->rhs);
    if (strcmp(node->op, "<") == 0) return eval_const(p, node->lhs) < eval_const(p, node->rhs);
    if (strcmp(node->op, ">") == 0) return eval_const(p, node->lhs) > eval_const(p, node->rhs);
    if (strcmp(node->op, "<=") == 0) return eval_const(p, node->lhs) <= eval_const(p, node->rhs);
    if (strcmp(node->op, ">=") == 0) return eval_const(p, node->lhs) >= eval_const(p, node->rhs);
    if (strcmp(node->op, "&&") == 0) return eval_const(p, node->lhs) && eval_const(p, node->rhs);
    if (strcmp(node->op, "||") == 0) return eval_const(p, node->lhs) || eval_const(p, node->rhs);
    if (strcmp(node->op, "&") == 0) return eval_const(p, node->lhs) & eval_const(p, node->rhs);
    if (strcmp(node->op, "|") == 0) return eval_const(p, node->lhs) | eval_const(p, node->rhs);
    if (strcmp(node->op, "^") == 0) return eval_const(p, node->lhs) ^ eval_const(p, node->rhs);
    if (strcmp(node->op, "<<") == 0) return eval_const(p, node->lhs) << eval_const(p, node->rhs);
    if (strcmp(node->op, ">>") == 0) return eval_const(p, node->lhs) >> eval_const(p, node->rhs);
    if (strcmp(node->op, "sizeof") == 0) {
        return sizeof_expr(p, node->lhs);
    }
    if (strcmp(node->op, "var") == 0) {
        HashMapEntry *entry = hashmap_get(&p->global_enums, node->name);
        if (entry) return entry->val_int;
        parser_error(p, "non-constant variable in constant expression");
    }
    parser_error(p, "invalid constant expression");
    return 0;
}

static int alignof_type(const ParserState *p, int base_size, const char *struct_tag, int stars) {
    if (stars > 0) return p->target_scale;
    if (struct_tag && struct_tag[0]) {
        HashMapEntry *ae = hashmap_get((HashMap *)&p->global_struct_alignments, struct_tag);
        if (ae) return ae->val_int;
        return p->target_scale;
    }
    if (base_size >= 8) return 8;
    if (base_size == 4) return 4;
    if (base_size == 2) return 2;
    return 1;
}

static long sizeof_expr(const ParserState *p, const Node *node);

static int alignof_expr(const ParserState *p, const Node *node) {
    long sz = sizeof_expr(p, node);
    if (sz >= 8) return 8;
    if (sz == 4) return 4;
    if (sz == 2) return 2;
    return 1;
}

static void get_expr_type_info(const ParserState *p, const Node *node, int *type_size, int *is_unsigned, int *is_pointer, const char **struct_tag, int *elem_size) {
    *type_size = 4;
    *is_unsigned = 0;
    *is_pointer = 0;
    *struct_tag = "";
    *elem_size = 0;
    if (!node) return;
    if (strcmp(node->op, "num") == 0) {
        *type_size = node->type_size ? node->type_size : 4;
        *is_unsigned = node->is_unsigned;
        return;
    }
    if (strcmp(node->op, "str") == 0) {
        *type_size = p->target_scale;
        *is_unsigned = 1;
        *is_pointer = 1;
        *elem_size = 1;
        return;
    }
    if (strcmp(node->op, "var") == 0) {
        HashMapEntry *entry = hashmap_get((HashMap *)&p->value_sizes, node->name);
        *type_size = entry ? entry->val_int : p->target_scale;
        entry = hashmap_get((HashMap *)&p->unsigned_vars, node->name);
        *is_unsigned = entry ? entry->val_int : 0;
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", node->name);
        HashMapEntry *is_ptr_entry = hashmap_get((HashMap *)&ir_local_var_is_pointer, local_key);
        if (is_ptr_entry && is_ptr_entry->val_int) {
            *is_pointer = 1;
            *elem_size = ir_get_local_var_elem_scale(local_key);
        } else {
            is_ptr_entry = hashmap_get((HashMap *)&ir_global_var_is_pointer, node->name);
            if (is_ptr_entry && is_ptr_entry->val_int) {
                *is_pointer = 1;
                *elem_size = ir_get_global_var_elem_scale(node->name);
            }
        }
        HashMapEntry *tag_entry = hashmap_get((HashMap *)&p->var_struct_tags, node->name);
        if (tag_entry) {
            *struct_tag = (const char *)tag_entry->val_ptr;
        }
        return;
    }
    if (strcmp(node->op, "cast") == 0) {
        *type_size = node->type_size;
        *is_unsigned = node->is_unsigned;
        return;
    }
    if (strcmp(node->op, "unary_*") == 0) {
        const char *lhs_tag = infer_struct_tag(p, node->lhs);
        if (lhs_tag && lhs_tag[0]) {
            *struct_tag = lhs_tag;
            HashMapEntry *se = hashmap_get((HashMap *)&p->global_struct_sizes, lhs_tag);
            if (se) {
                *type_size = se->val_int;
            }
        }
        return;
    }
    if (strcmp(node->op, "index") == 0) {
        *type_size = node->type_size ? node->type_size : p->target_scale;
        *struct_tag = node->type_tag ? node->type_tag : "";
        return;
    }
}

static int match_generic_type(int ctrl_size, int ctrl_unsigned, int ctrl_pointer, const char *ctrl_tag, int ctrl_elem_size,
                              int assoc_size, int assoc_unsigned, int assoc_pointer, const char *assoc_tag, int assoc_elem_size) {
    if (ctrl_pointer != assoc_pointer) return 0;
    if (ctrl_pointer) {
        if (ctrl_tag[0] || assoc_tag[0]) {
            return strcmp(ctrl_tag, assoc_tag) == 0;
        }
        return ctrl_elem_size == assoc_elem_size;
    }
    if (ctrl_size != assoc_size) return 0;
    if (ctrl_unsigned != assoc_unsigned) return 0;
    return strcmp(ctrl_tag, assoc_tag) == 0;
}

static long sizeof_expr(const ParserState *p, const Node *node) {
    if (strcmp(node->op, "num") == 0) return 8;
    if (strcmp(node->op, "char") == 0) return 1;
    if (strcmp(node->op, "var") == 0) {
        HashMapEntry *entry = hashmap_get((HashMap *)&p->value_sizes, node->name);
        if (entry) return entry->val_int;
        return p->target_scale;
    }
    if (strcmp(node->op, "index") == 0) {
        if (node->elem_size > 0) return node->elem_size;
        if (node->lhs && strcmp(node->lhs->op, "var") == 0) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", node->lhs->name);
            int base = ir_get_local_array_base_size(local_key);
            if (base > 0) return base;
            base = ir_get_global_array_base_size(node->lhs->name);
            if (base > 0) return base;
        }
        return p->target_scale;
    }
    if (strcmp(node->op, "cast") == 0) {
        return node->type_size;
    }
    if (strcmp(node->op, "unary_*") == 0) {
        const char *tag = infer_struct_tag(p, node->lhs);
        if (tag && tag[0]) {
            HashMapEntry *entry = hashmap_get((HashMap *)&p->global_struct_sizes, tag);
            if (entry) return entry->val_int;
        }
        const Node *base = node->lhs;
        if (base && strcmp(base->op, "index") == 0) {
            base = base->lhs;
        }
        if (base && strcmp(base->op, "var") == 0) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", base->name);
            int scale = ir_get_local_var_elem_scale(local_key);
            if (scale > 0) return scale;
            scale = ir_get_global_var_elem_scale(base->name);
            if (scale > 0) return scale;
        }
        return p->target_scale;
    }
    if (strcmp(node->op, "member") == 0 || strcmp(node->op, "member_ptr") == 0) {
        const char *tag = "";
        if (strcmp(node->op, "member") == 0) {
            tag = infer_struct_tag(p, node->lhs);
        } else {
            tag = infer_struct_tag(p, node->lhs);
            if (tag && tag[0]) {
                HashMapEntry *entry = hashmap_get((HashMap *)&p->global_struct_field_tags, tag);
                if (entry) tag = (const char *)entry->val_ptr;
            }
        }
        if (tag && tag[0]) {
            char key[256];
            snprintf(key, sizeof(key), "%s.%s", tag, node->name);
            HashMapEntry *entry = hashmap_get((HashMap *)&p->global_struct_field_sizes_by_tag, key);
            if (entry) return entry->val_int;
        }
        return p->target_scale;
    }
    /* Fallback for binary/unary expression nodes: use stored type_size if set */
    if (node->type_size > 0 && node->type_size < (long)p->target_scale * 4) {
        return node->type_size;
    }
    return p->target_scale;
}

static void skip_attribute(ParserState *p) {
    /* NOTE: type qualifiers (const/volatile/restrict) are intentionally NOT
       skipped here. They carry semantics (M14) and are consumed and recorded
       by parse_base_type / skip_type_qualifiers so the object can be tracked. */
    while (strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0 ||
           (strcmp(peek(p), "[") == 0 && p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "[") == 0)) {
        if (strcmp(peek(p), "[") == 0) {
            take(p, "[");
            take(p, "[");
            int depth = 1;
            while (depth > 0 && p->pos < p->tokens.count) {
                if (strcmp(peek(p), "[") == 0) depth++;
                else if (strcmp(peek(p), "]") == 0) {
                    depth--;
                    if (depth == 0) {
                        take(p, "]");
                        take(p, "]");
                        break;
                    }
                }
                take(p, nullptr);
            }
            continue;
        }
        const char *attr = take(p, nullptr);
        if ((strcmp(attr, "__attribute__") == 0 || strcmp(attr, "__attribute") == 0) && strcmp(peek(p), "(") == 0) {
            take(p, "(");
            int depth = 1;
            while (depth > 0 && p->pos < p->tokens.count) {
                if (strcmp(peek(p), "(") == 0) depth++;
                else if (strcmp(peek(p), ")") == 0) depth--;
                take(p, nullptr);
            }
        }
    }
}

static void skip_attribute_and_asm(ParserState *p) {
    skip_attribute(p);
    if (strcmp(peek(p), "__asm__") == 0 || strcmp(peek(p), "__asm") == 0 || strcmp(peek(p), "asm") == 0) {
        take(p, nullptr);
        take(p, "(");
        take(p, nullptr);
        take(p, ")");
    }
}

static void note_type_qualifier(ParserState *p, const char *t) {
    if (strcmp(t, "const") == 0) p->last_type_const = 1;
    else if (strcmp(t, "volatile") == 0) p->last_type_volatile = 1;
}

static void skip_type_qualifiers(ParserState *p) {
    while (strcmp(peek(p), "const") == 0 || strcmp(peek(p), "volatile") == 0 ||
           strcmp(peek(p), "restrict") == 0 || strcmp(peek(p), "__restrict") == 0 ||
           strcmp(peek(p), "__restrict__") == 0 || strcmp(peek(p), "_Atomic") == 0) {
        note_type_qualifier(p, peek(p));
        take(p, nullptr);
    }
}

static int parse_base_type(ParserState *p);

static int parse_base_type(ParserState *p) {
    p->last_parsed_struct_tag = "";
    skip_attribute(p);
    int base_size = p->target_scale;
    p->last_type_unsigned = 0;
    p->last_type_bool = 0;
    p->last_type_const = 0;
    p->last_type_volatile = 0;
    p->last_type_float = 0;
    while (strcmp(peek(p), "const") == 0 || strcmp(peek(p), "volatile") == 0 || strcmp(peek(p), "restrict") == 0 || strcmp(peek(p), "__restrict") == 0 || strcmp(peek(p), "__restrict__") == 0 || strcmp(peek(p), "register") == 0 || strcmp(peek(p), "_Atomic") == 0 || strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
        if (strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
            skip_attribute(p);
        } else {
            note_type_qualifier(p, peek(p));
            take(p, nullptr);
        }
    }
    if (strcmp(peek(p), "unsigned") == 0 || strcmp(peek(p), "signed") == 0) {
        p->last_type_unsigned = (strcmp(peek(p), "unsigned") == 0);
        take(p, nullptr);
        while (strcmp(peek(p), "const") == 0 || strcmp(peek(p), "volatile") == 0 || strcmp(peek(p), "restrict") == 0 || strcmp(peek(p), "__restrict") == 0 || strcmp(peek(p), "__restrict__") == 0 || strcmp(peek(p), "register") == 0 || strcmp(peek(p), "_Atomic") == 0 || strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
            if (strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
                skip_attribute(p);
            } else {
                note_type_qualifier(p, peek(p));
                take(p, nullptr);
            }
        }
        if (strcmp(peek(p), "struct") != 0 && strcmp(peek(p), "enum") != 0 &&
            (strcmp(peek(p), "char") == 0 || strcmp(peek(p), "int") == 0 || strcmp(peek(p), "long") == 0 || strcmp(peek(p), "short") == 0 || strcmp(peek(p), "void") == 0 || strcmp(peek(p), "_Bool") == 0 || strcmp(peek(p), "bool") == 0 ||
             strcmp(peek(p), "float") == 0 || strcmp(peek(p), "double") == 0 || hashmap_has(&p->global_typedefs, peek(p)))) {
            // continue
        } else {
            return p->target_scale;
        }
    }
    while (strcmp(peek(p), "const") == 0 || strcmp(peek(p), "volatile") == 0 || strcmp(peek(p), "restrict") == 0 || strcmp(peek(p), "__restrict") == 0 || strcmp(peek(p), "__restrict__") == 0 || strcmp(peek(p), "register") == 0 || strcmp(peek(p), "_Atomic") == 0 || strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
        if (strcmp(peek(p), "__attribute__") == 0 || strcmp(peek(p), "__attribute") == 0) {
            skip_attribute(p);
        } else {
            note_type_qualifier(p, peek(p));
            take(p, nullptr);
        }
    }
    if (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) {
        int is_union = (strcmp(peek(p), "union") == 0);
        take(p, nullptr);
        const char *tag = "";
        if (strcmp(peek(p), "{") != 0) {
            tag = take(p, nullptr);
        }
        if (strcmp(peek(p), "{") == 0) {
            if (!tag || !tag[0]) {
                static _Atomic int anon_counter = 0;
                char anon_tag[64];
                int anon_id = atomic_fetch_add_explicit(&anon_counter, 1, memory_order_relaxed);
                snprintf(anon_tag, sizeof(anon_tag), "anon.struct.%d", anon_id);
                tag = arena_strdup(p->arena, anon_tag);
            }
            take(p, "{");
            int offset = 0;
            int union_max_size = 0;
            int struct_alignment = 1;
            int bit_offset = 0;
            int current_unit_size = 0;
            HashMap *fields = malloc(sizeof(HashMap));
            hashmap_init(fields, 16);
            while (strcmp(peek(p), "}") != 0) {
                int field_base_size = parse_base_type(p);
                const char *f_tag = p->last_parsed_struct_tag;
                while (1) {
                    int stars = 0;
                    while (strcmp(peek(p), "*") == 0) {
                        take(p, "*");
                        stars++;
                    }
                    int is_func_ptr_field = 0;
                    const char *func_ptr_field_name = "";
                    if (strcmp(peek(p), "(") == 0 &&
                        p->pos + 1 < p->tokens.count &&
                        strcmp(p->tokens.data[p->pos + 1].text, "*") == 0) {
                        take(p, "(");
                        take(p, "*");
                        skip_type_qualifiers(p);
                        func_ptr_field_name = take(p, nullptr);
                        take(p, ")");
                        if (strcmp(peek(p), "(") == 0) {
                            int depth = 0;
                            int keep_scanning = 1;
                            while (keep_scanning && p->pos < p->tokens.count) {
                                if (strcmp(peek(p), "(") == 0) depth++;
                                else if (strcmp(peek(p), ")") == 0) depth--;
                                take(p, nullptr);
                                if (depth <= 0) {
                                    keep_scanning = 0;
                                }
                            }
                        }
                        stars = 1;
                        is_func_ptr_field = 1;
                    }
                    int field_size = (stars > 0) ? p->target_scale : field_base_size;
                    
                    int field_align = (stars > 0) ? p->target_scale : field_base_size;
                    if (stars == 0 && f_tag[0]) {
                        HashMapEntry *ae = hashmap_get(&p->global_struct_alignments, f_tag);
                        if (ae) {
                            field_align = ae->val_int;
                        } else {
                            field_align = p->target_scale;
                        }
                    }
                    if (field_align > 8) field_align = 8;
                    if (field_align < 1) field_align = 1;
                    if (field_align > struct_alignment) {
                        struct_alignment = field_align;
                    }
                    
                    int is_anon = (strcmp(peek(p), ";") == 0 || strcmp(peek(p), ",") == 0);
                    if (is_anon && f_tag[0]) {
                        // Anonymous struct/union flattening!
                        HashMapEntry *f_entry = hashmap_get(&p->global_structs, f_tag);
                        if (f_entry) {
                            HashMap *sub_fields = (HashMap *)f_entry->val_ptr;
                            for (int b = 0; b < sub_fields->bucket_count; ++b) {
                                HashMapEntry *curr = sub_fields->buckets[b];
                                while (curr) {
                                    const char *sub_field_name = curr->key;
                                    int sub_offset = curr->val_int;
                                    
                                    hashmap_put(fields, sub_field_name, nullptr, offset + sub_offset);
                                    hashmap_put(&p->global_field_offsets, sub_field_name, nullptr, offset + sub_offset);
                                    
                                    char f_key[512];
                                    snprintf(f_key, sizeof(f_key), "%s.%s", f_tag, sub_field_name);
                                    
                                    HashMapEntry *sz_e = hashmap_get(&p->global_struct_field_sizes_by_tag, f_key);
                                    int sub_size = sz_e ? sz_e->val_int : p->target_scale;
                                    HashMapEntry *tot_e = hashmap_get(&p->global_struct_field_total_sizes_by_tag, f_key);
                                    int sub_total = tot_e ? tot_e->val_int : sub_size;
                                    HashMapEntry *tag_e = hashmap_get(&p->global_struct_field_tags, f_key);
                                    const char *sub_tag = tag_e ? (const char *)tag_e->val_ptr : "";
                                    LongArray *sub_dims = ir_get_struct_field_dims(f_key);
                                    
                                    hashmap_put(&p->global_field_sizes, sub_field_name, nullptr, sub_size);
                                    
                                    if (tag[0]) {
                                        char p_key[512];
                                        snprintf(p_key, sizeof(p_key), "%s.%s", tag, sub_field_name);
                                        const char *p_key_dup = arena_strdup(p->arena, p_key);
                                        
                                        hashmap_put(&p->global_struct_field_offsets_by_tag, p_key_dup, nullptr, offset + sub_offset);
                                        hashmap_put(&p->global_struct_field_sizes_by_tag, p_key_dup, nullptr, sub_size);
                                        hashmap_put(&p->global_struct_field_total_sizes_by_tag, p_key_dup, nullptr, sub_total);
                                        if (sub_tag && sub_tag[0]) {
                                            hashmap_put(&p->global_struct_field_tags, p_key_dup, (void *)sub_tag, 0);
                                        }
                                        if (sub_dims) {
                                            LongArray *dims_copy = malloc(sizeof(LongArray));
                                            long_array_init(dims_copy);
                                            for (int d_i = 0; d_i < sub_dims->count; ++d_i) {
                                                long_array_push(dims_copy, sub_dims->data[d_i]);
                                            }
                                            hashmap_put(&p->global_struct_field_dims_by_tag, p_key_dup, dims_copy, 0);
                                        }
                                    }
                                    curr = curr->next;
                                }
                            }
                        }
                        if (is_union) {
                            if (field_base_size > union_max_size) union_max_size = field_base_size;
                        } else {
                            offset += field_base_size;
                        }
                        break;
                    }
                    
                    const char *field_name = "";
                    int is_bitfield = 0;
                    int bit_width = 0;
                    if (strcmp(peek(p), ":") == 0) {
                        is_bitfield = 1;
                        char anon_bf_name[128];
                        static int anon_bf_id = 0;
                        snprintf(anon_bf_name, sizeof(anon_bf_name), "$anon_bf_%d", anon_bf_id++);
                        field_name = arena_strdup(p->arena, anon_bf_name);
                    } else {
                        field_name = is_func_ptr_field ? func_ptr_field_name : take(p, nullptr);
                        if (strcmp(peek(p), ":") == 0) {
                            is_bitfield = 1;
                        }
                    }

                    if (is_bitfield) {
                        take(p, ":");
                        Node *bf_expr = expr(p);
                        bit_width = (int)eval_const(p, bf_expr);
                    }

                    long arr_count = 1;
                    LongArray *field_dims = malloc(sizeof(LongArray));
                    long_array_init(field_dims);
                    if (!is_bitfield && strcmp(peek(p), "[") == 0) {
                        while (strcmp(peek(p), "[") == 0) {
                            take(p, "[");
                            long dim = 0;
                            if (strcmp(peek(p), "]") != 0) {
                                Node *sz_expr = expr(p);
                                dim = eval_const(p, sz_expr);
                                arr_count *= dim;
                            }
                            take(p, "]");
                            long_array_push(field_dims, dim);
                        }
                    }
                    int align = field_align;
                    if (align > 8) align = 8;

                    if (is_bitfield) {
                        long_array_free(field_dims);
                        free(field_dims);
                        int w = bit_width;
                        if (current_unit_size != field_size || bit_offset + w > field_size * 8 || w == 0) {
                            if (bit_offset > 0) {
                                offset += current_unit_size;
                            }
                            if (align > 1 && (offset % align) != 0) {
                                offset += align - (offset % align);
                            }
                            bit_offset = 0;
                            current_unit_size = field_size;
                        }

                        if (tag[0]) {
                            char key[256];
                            snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                            const char *key_dup = arena_strdup(p->arena, key);
                            hashmap_put(&p->global_struct_field_offsets_by_tag, key_dup, nullptr, offset);
                            hashmap_put(&p->global_struct_field_sizes_by_tag, key_dup, nullptr, field_size);
                            hashmap_put(&p->global_struct_field_bit_offsets_by_tag, key_dup, nullptr, bit_offset);
                            hashmap_put(&p->global_struct_field_bit_widths_by_tag, key_dup, nullptr, w);
                        }
                        hashmap_put(fields, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_offsets, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_sizes, field_name, nullptr, field_size);

                        bit_offset += w;
                    } else {
                        if (bit_offset > 0) {
                            offset += current_unit_size;
                            bit_offset = 0;
                            current_unit_size = 0;
                        }
                        if (!is_union && align > 1 && (offset % align) != 0) {
                            offset += align - (offset % align);
                        }
                        if (tag[0] && f_tag[0]) {
                            char key[256];
                            snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                            hashmap_put(&p->global_struct_field_tags, arena_strdup(p->arena, key), (void *)f_tag, 0);
                        }
                        hashmap_put(fields, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_offsets, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_sizes, field_name, nullptr, field_size);
                        int this_field_total = (int)(field_size * arr_count);
                        if (tag[0]) {
                            char key[256];
                            snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                            const char *key_dup = arena_strdup(p->arena, key);
                            hashmap_put(&p->global_struct_field_offsets_by_tag, key_dup, nullptr, offset);
                            hashmap_put(&p->global_struct_field_sizes_by_tag, key_dup, nullptr, field_size);
                            if (stars > 0) {
                                int field_elem_size = (stars > 1) ? p->target_scale : field_base_size;
                                hashmap_put(&p->global_struct_field_elem_sizes_by_tag, key_dup, nullptr, field_elem_size);
                            }
                            hashmap_put(&p->global_struct_field_total_sizes_by_tag, key_dup, nullptr, this_field_total);
                            hashmap_put(&p->global_struct_field_dims_by_tag, key_dup, field_dims, 0);
                        } else {
                            long_array_free(field_dims);
                            free(field_dims);
                        }
                        if (is_union) {
                            if (this_field_total > union_max_size) union_max_size = this_field_total;
                        } else {
                            offset += this_field_total;
                        }
                    }

                    if (strcmp(peek(p), ",") == 0) {
                        take(p, ",");
                    } else {
                        break;
                    }
                }
                take(p, ";");
            }
            take(p, "}");
            if (is_union) {
                base_size = union_max_size;
            } else {
                if (bit_offset > 0) {
                    offset += current_unit_size;
                }
                if (offset > 0 && (offset % p->target_scale) != 0) {
                    offset += p->target_scale - (offset % p->target_scale);
                }
                base_size = offset;
            }
            if (tag[0]) {
                hashmap_put(&p->global_structs, tag, fields, 0);
                hashmap_put(&p->global_struct_sizes, tag, nullptr, base_size);
                hashmap_put(&p->global_struct_alignments, tag, nullptr, struct_alignment);
            } else {
                hashmap_free(fields);
                free(fields);
            }
        } else {
            HashMapEntry *ge = hashmap_get((HashMap *)&p->global_struct_sizes, tag);
            if (ge) {
                base_size = ge->val_int;
            } else {
                HashMapEntry *gstruct = hashmap_get((HashMap *)&p->global_structs, tag);
                if (gstruct) {
                    HashMap *fields = (HashMap *)gstruct->val_ptr;
                    base_size = fields->size * p->target_scale;
                } else {
                    base_size = p->target_scale;
                }
            }
        }
        p->last_parsed_struct_tag = tag;
    } else if (strcmp(peek(p), "enum") == 0) {
        take(p, "enum");
        if (strcmp(peek(p), "{") != 0) {
            take(p, nullptr);
        }
        if (strcmp(peek(p), "{") == 0) {
            take(p, "{");
            int val = 0;
            while (1) {
                if (strcmp(peek(p), "}") == 0) {
                    break;
                }
                const char *name = take(p, nullptr);
                if (strcmp(peek(p), "=") == 0) {
                    take(p, "=");
                    val = atoi(take(p, nullptr));
                }
                hashmap_put(&p->global_enums, name, nullptr, val++);
                if (strcmp(peek(p), ",") == 0)
                    take(p, ",");
                else
                    break;
            }
            take(p, "}");
        }
        base_size = p->target_scale;
    } else {
        const char *t = take(p, nullptr);
        if (strcmp(t, "char") == 0) {
            base_size = 1;
        } else if (strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0) {
            base_size = 1;
            p->last_type_bool = 1;
        } else if (strcmp(t, "short") == 0) {
            base_size = 2;
        } else if (strcmp(t, "int") == 0) {
            base_size = 4;
        } else if (strcmp(t, "float") == 0) {
            base_size = 4;
            p->last_type_float = 1;
        } else if (strcmp(t, "double") == 0) {
            base_size = 8;
            p->last_type_float = 1;
        } else if (strcmp(t, "long") == 0) {
            if (strcmp(peek(p), "double") == 0) {
                take(p, nullptr);
                base_size = 8;
                p->last_type_float = 1;
            } else if (strcmp(peek(p), "long") == 0) {
                take(p, nullptr);
                if (strcmp(peek(p), "int") == 0) take(p, nullptr);
                base_size = 8;
            } else {
                if (strcmp(peek(p), "int") == 0) take(p, nullptr);
                base_size = 8;
            }
        } else if (strcmp(t, "void") == 0) {
            base_size = 8;
        } else {
            HashMapEntry *typedef_size = hashmap_get(&p->global_typedef_sizes, t);
            if (typedef_size) {
                base_size = typedef_size->val_int;
                HashMapEntry *typedef_tag = hashmap_get(&p->global_typedef_struct_tags, t);
                if (typedef_tag) {
                    p->last_parsed_struct_tag = (const char *)typedef_tag->val_ptr;
                }
            } else {
                base_size = p->target_scale;
            }
        }
    }
    return base_size;
}

static void parser_type(ParserState *p) {
    parse_base_type(p);
    while (strcmp(peek(p), "*") == 0) {
        take(p, "*");
    }
    skip_attribute(p);
}

static int is_type_start_token(ParserState *p, const char *t) {
    return strcmp(t, "int") == 0 || strcmp(t, "char") == 0 ||
           strcmp(t, "short") == 0 || strcmp(t, "long") == 0 || strcmp(t, "_Atomic") == 0 ||
           strcmp(t, "void") == 0 || strcmp(t, "struct") == 0 ||
           strcmp(t, "union") == 0 || strcmp(t, "unsigned") == 0 ||
           strcmp(t, "signed") == 0 || strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0 ||
           strcmp(t, "float") == 0 || strcmp(t, "double") == 0 ||
           hashmap_has(&p->global_typedefs, t);
}

static int paren_starts_type_name(ParserState *p) {
    if (strcmp(peek(p), "(") != 0) return 0;
    int pos = p->pos + 1;
    while (pos < p->tokens.count) {
        const char *t = p->tokens.data[pos].text;
        if (strcmp(t, "const") == 0 || strcmp(t, "volatile") == 0 ||
            strcmp(t, "restrict") == 0 || strcmp(t, "__restrict") == 0 ||
            strcmp(t, "__restrict__") == 0 || strcmp(t, "register") == 0) {
            pos++;
        } else {
            return is_type_start_token(p, t);
        }
    }
    return 0;
}

static int is_function_decl(ParserState *p) {
    int pos = p->pos;
    while (pos < p->tokens.count) {
        const char *t = p->tokens.data[pos].text;
        if (strcmp(t, "const") == 0 || strcmp(t, "volatile") == 0 || strcmp(t, "restrict") == 0 || strcmp(t, "__restrict") == 0 || strcmp(t, "__restrict__") == 0 ||
            strcmp(t, "inline") == 0 || strcmp(t, "__inline") == 0 || strcmp(t, "__inline__") == 0 || strcmp(t, "register") == 0 || strcmp(t, "_Noreturn") == 0 || strcmp(t, "noreturn") == 0) {
            pos++;
        } else if (strcmp(t, "struct") == 0 || strcmp(t, "enum") == 0) {
            pos++;
            if (pos < p->tokens.count) pos++;
        } else if (strcmp(t, "int") == 0 || strcmp(t, "char") == 0 || strcmp(t, "short") == 0 || strcmp(t, "long") == 0 || strcmp(t, "void") == 0 || strcmp(t, "unsigned") == 0 || strcmp(t, "signed") == 0 || strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0 || strcmp(t, "float") == 0 || strcmp(t, "double") == 0 || hashmap_has(&p->global_typedefs, t)) {
            pos++;
        } else if (strcmp(t, "*") == 0) {
            pos++;
        } else {
            break;
        }
    }
    if (pos + 3 < p->tokens.count &&
        strcmp(p->tokens.data[pos].text, "(") == 0 &&
        strcmp(p->tokens.data[pos + 2].text, ")") == 0 &&
        strcmp(p->tokens.data[pos + 3].text, "(") == 0) {
        return 1;
    }
    pos++;
    if (pos < p->tokens.count && strcmp(p->tokens.data[pos].text, "(") == 0) {
        return 1;
    }
    return 0;
}

static void apply_integer_conversion(ParserState *p, Node *node) {
    (void)p;
    if (strcmp(node->op, "num") == 0) {
        if (node->value > 2147483647L || node->value < -2147483648L) {
            node->type_size = 8;
        } else {
            node->type_size = 4;
        }
    }
}

static Node *bool_normalize(ParserState *p, Node *node) {
    Node *norm = create_node(p, "!=", node->line, node->col);
    norm->lhs = node;
    norm->rhs = create_node(p, "num", node->line, node->col);
    norm->rhs->value = 0;
    norm->type_size = 4;
    return norm;
}

static Node *parse_generic_selection(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    take(p, "_Generic");
    take(p, "(");
    Node *ctrl_expr = expr(p);
    take(p, ",");
    
    int ctrl_size = 4;
    int ctrl_unsigned = 0;
    int ctrl_pointer = 0;
    const char *ctrl_tag = "";
    int ctrl_elem_size = 0;
    get_expr_type_info(p, ctrl_expr, &ctrl_size, &ctrl_unsigned, &ctrl_pointer, &ctrl_tag, &ctrl_elem_size);
    
    Node *selected_node = nullptr;
    Node *default_node = nullptr;
    
    while (strcmp(peek(p), ")") != 0) {
        if (strcmp(peek(p), "default") == 0) {
            take(p, "default");
            take(p, ":");
            Node *assoc_expr = expr(p);
            default_node = assoc_expr;
        } else {
            const char *assoc_tag = "";
            if (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) {
                if (p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "*") != 0 && strcmp(p->tokens.data[p->pos + 1].text, ":") != 0) {
                    assoc_tag = p->tokens.data[p->pos + 1].text;
                }
            }
            int assoc_unsigned = 0;
            int assoc_size = parse_base_type(p);
            assoc_unsigned = p->last_type_unsigned;
            int assoc_pointer = 0;
            int assoc_elem_size = 0;
            while (strcmp(peek(p), "*") == 0) {
                take(p, "*");
                assoc_pointer = 1;
                assoc_elem_size = assoc_size;
                assoc_size = p->target_scale;
            }
            take(p, ":");
            Node *assoc_expr = expr(p);
            
            if (match_generic_type(ctrl_size, ctrl_unsigned, ctrl_pointer, ctrl_tag, ctrl_elem_size,
                                   assoc_size, assoc_unsigned, assoc_pointer, assoc_tag, assoc_elem_size)) {
                selected_node = assoc_expr;
            }
        }
        if (strcmp(peek(p), ",") == 0) {
            take(p, ",");
        } else {
            break;
        }
    }
    take(p, ")");
    
    if (selected_node) {
        return selected_node;
    }
    if (default_node) {
        return default_node;
    }
    diagnostics_error(tok_line, tok_col, "controlling expression type not matched in generic selection");
    return create_node(p, "num", tok_line, tok_col);
}

static Node *primary(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    if (strcmp(peek(p), "_Generic") == 0) {
        return parse_generic_selection(p);
    }
    if (strcmp(peek(p), "nullptr") == 0) {
        take(p, "nullptr");
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = 0;
        n->type_size = p->target_scale;
        n->is_unsigned = 1;
        return n;
    }
    if (strcmp(peek(p), "true") == 0 || strcmp(peek(p), "false") == 0) {
        const char *lit = take(p, nullptr);
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = strcmp(lit, "true") == 0;
        n->type_size = 1;
        n->is_bool = 1;
        return n;
    }
    if (strcmp(peek(p), "__builtin_constant_p") == 0) {
        take(p, "__builtin_constant_p");
        take(p, "(");
        expr(p);
        take(p, ")");
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = 0;
        n->type_size = 4;
        return n;
    }
    if (strcmp(peek(p), "(") == 0) {
        if (p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, ")") == 0) {
            take(p, "(");
            take(p, ")");
            Node *n = create_node(p, "num", tok_line, tok_col);
            n->value = 0;
            n->type_size = 4;
            return n;
        }
        take(p, "(");
        Node *n = expr(p);
        take(p, ")");
        return n;
    }
    if (p->pos < p->tokens.count && p->tokens.data[p->pos].text[0] == '"') {
        const char *t = take(p, nullptr);
        Node *n = create_node(p, "str", tok_line, tok_col);
        // Strip quotes
        size_t len = strlen(t);
        if (len >= 2) {
            n->name = arena_strndup(p->arena, t + 1, len - 2);
        } else {
            n->name = "";
        }
        n->type_size = p->target_scale;
        return n;
    }
    if (p->pos < p->tokens.count && p->tokens.data[p->pos].text[0] == '\'') {
        const char *t = take(p, nullptr);
        Node *n = create_node(p, "num", tok_line, tok_col);
        if (t[1] == '\\') {
            if (t[2] == 'n') n->value = '\n';
            else if (t[2] == 't') n->value = '\t';
            else if (t[2] == 'r') n->value = '\r';
            else if (t[2] == 'v') n->value = '\v';
            else if (t[2] == 'f') n->value = '\f';
            else if (t[2] == 'a') n->value = '\a';
            else if (t[2] == 'b') n->value = '\b';
            else if (t[2] == '0') n->value = '\0';
            else if (t[2] == '\\') n->value = '\\';
            else if (t[2] == '\'') n->value = '\'';
            else n->value = t[2];
        } else {
            n->value = t[1];
        }
        n->type_size = 4;
        return n;
    }
    if (p->pos < p->tokens.count &&
        (is_digit(p->tokens.data[p->pos].text[0]) ||
         (p->tokens.data[p->pos].text[0] == '.' && is_digit(p->tokens.data[p->pos].text[1])))) {
        const char *t = take(p, nullptr);
        int is_hex = (strncmp(t, "0x", 2) == 0 || strncmp(t, "0X", 2) == 0);
        int is_fp = 0;
        if (!is_hex) {
            for (const char *s = t; *s; ++s) {
                if (*s == '.' || *s == 'e' || *s == 'E' || *s == 'f' || *s == 'F') { is_fp = 1; break; }
            }
        }
        if (is_fp) {
            Node *n = create_node(p, "fnum", tok_line, tok_col);
            n->fvalue = strtod(t, nullptr);
            n->is_float = 1;
            /* an 'f'/'F' suffix denotes a 4-byte float, otherwise double */
            size_t len = strlen(t);
            n->type_size = (len > 0 && (t[len - 1] == 'f' || t[len - 1] == 'F')) ? 4 : 8;
            return n;
        }
        Node *n = create_node(p, "num", tok_line, tok_col);
        if (is_hex) {
            n->value = strtol(t, nullptr, 16);
        } else {
            n->value = strtol(t, nullptr, 10);
        }
        apply_integer_conversion(p, n);
        return n;
    }
    const char *name = take(p, nullptr);
    HashMapEntry *enum_entry = hashmap_get(&p->global_enums, name);
    if (enum_entry) {
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = enum_entry->val_int;
        n->type_size = 4;
        return n;
    }
    const char *resolved = name;
    HashMapEntry *sl_entry = hashmap_get((HashMap *)&p->current_static_locals, name);
    if (sl_entry) {
        resolved = (const char *)sl_entry->val_ptr;
    } else {
        resolved = resolve_name(p, name);
    }
    HashMapEntry *constexpr_entry = hashmap_get(&p->constexpr_vars, resolved);
    if (constexpr_entry) {
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = constexpr_entry->val_int;
        HashMapEntry *sz_entry = hashmap_get((HashMap *)&p->value_sizes, resolved);
        n->type_size = sz_entry ? sz_entry->val_int : 4;
        HashMapEntry *unsigned_entry = hashmap_get((HashMap *)&p->unsigned_vars, resolved);
        if (unsigned_entry) n->is_unsigned = unsigned_entry->val_int;
        return n;
    }
    Node *n = create_node(p, "var", tok_line, tok_col);
    n->name = resolved;
    HashMapEntry *unsigned_entry = hashmap_get((HashMap *)&p->unsigned_vars, resolved);
    if (unsigned_entry) n->is_unsigned = unsigned_entry->val_int;
    HashMapEntry *bool_entry = hashmap_get((HashMap *)&p->bool_vars, resolved);
    if (bool_entry) n->is_bool = bool_entry->val_int;
    HashMapEntry *float_entry = hashmap_get((HashMap *)&p->float_vars, resolved);
    if (float_entry) n->is_float = float_entry->val_int;
    HashMapEntry *sz_entry = hashmap_get((HashMap *)&p->value_sizes, resolved);
    n->type_size = sz_entry ? sz_entry->val_int : p->target_scale;
    return n;
}

static Node *factor(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = primary(p);
    while (strcmp(peek(p), "[") == 0 || strcmp(peek(p), "(") == 0 || strcmp(peek(p), ".") == 0 || strcmp(peek(p), "->") == 0 || strcmp(peek(p), "++") == 0 || strcmp(peek(p), "--") == 0) {
        if (strcmp(peek(p), "[") == 0) {
            take(p, "[");
            Node *idx = expr(p);
            take(p, "]");
            Node *deref = create_node(p, "index", tok_line, tok_col);
            deref->lhs = n;
            deref->rhs = idx;
            const char *tag = infer_struct_tag(p, n);
            if (tag && tag[0]) {
                deref->type_tag = tag;
                char key[256];
                snprintf(key, sizeof(key), "%s.data", tag); // simplified struct tag indexing
                HashMapEntry *entry = hashmap_get(&p->global_struct_field_sizes_by_tag, key);
                if (entry) deref->elem_size = entry->val_int;
            }
            n = deref;
        } else if (strcmp(peek(p), "(") == 0) {
            take(p, "(");
            Node *call = create_node(p, "call", tok_line, tok_col);
            call->lhs = n;
            if (strcmp(peek(p), ")") != 0) {
                while (1) {
                    node_array_push(&call->body, expr(p));
                    if (strcmp(peek(p), ",") == 0) {
                        take(p, ",");
                    } else {
                        break;
                    }
                }
            }
            take(p, ")");
            n = call;
        } else if (strcmp(peek(p), ".") == 0 || strcmp(peek(p), "->") == 0) {
            int is_arrow = (strcmp(peek(p), "->") == 0);
            take(p, nullptr);
            const char *field_name = take(p, nullptr);
            Node *parent = create_node(p, "index", tok_line, tok_col);
            const char *lhs_tag = infer_struct_tag(p, n);
            if (is_arrow) {
                Node *deref = create_node(p, "unary_*", tok_line, tok_col);
                deref->lhs = n;
                deref->type_tag = lhs_tag;
                parent->lhs = deref;
            } else {
                parent->lhs = n;
            }
            
            int byte_offset = 0;
            int field_sz = p->target_scale;
            int field_total_sz = field_sz;
            LongArray field_dims;
            long_array_init(&field_dims);
            const char *field_tag = "";
            
            char field_key[512];
            field_key[0] = '\0';
            if (lhs_tag && lhs_tag[0]) {
                snprintf(field_key, sizeof(field_key), "%s.%s", lhs_tag, field_name);
            }
            
            if (field_key[0] && hashmap_has(&p->global_struct_field_offsets_by_tag, field_key)) {
                HashMapEntry *off_entry = hashmap_get(&p->global_struct_field_offsets_by_tag, field_key);
                byte_offset = off_entry->val_int;
                HashMapEntry *sz_entry = hashmap_get(&p->global_struct_field_sizes_by_tag, field_key);
                field_sz = sz_entry->val_int;
                HashMapEntry *elem_entry = hashmap_get(&p->global_struct_field_elem_sizes_by_tag, field_key);
                if (elem_entry) {
                    parent->pointee_size = elem_entry->val_int;
                }
                HashMapEntry *tot_entry = hashmap_get(&p->global_struct_field_total_sizes_by_tag, field_key);
                field_total_sz = tot_entry ? tot_entry->val_int : field_sz;
                
                LongArray *dims_ptr = ir_get_struct_field_dims(field_key);
                if (dims_ptr) {
                    for (int d_i = 0; d_i < dims_ptr->count; ++d_i) {
                        long_array_push(&field_dims, dims_ptr->data[d_i]);
                    }
                }
                HashMapEntry *tag_entry = hashmap_get(&p->global_struct_field_tags, field_key);
                if (tag_entry) {
                    field_tag = (const char *)tag_entry->val_ptr;
                }
            } else if (hashmap_has(&p->global_field_offsets, field_name)) {
                HashMapEntry *off_entry = hashmap_get(&p->global_field_offsets, field_name);
                byte_offset = off_entry->val_int;
                HashMapEntry *sz_entry = hashmap_get(&p->global_field_sizes, field_name);
                if (sz_entry) {
                    field_sz = sz_entry->val_int;
                    field_total_sz = field_sz;
                }
            }
            
            parent->name = "byte_offset";
            Node *index_val = create_node(p, "num", tok_line, tok_col);
            index_val->value = byte_offset;
            parent->rhs = index_val;
            
            parent->value = field_sz;
            parent->elem_size = field_sz;
            parent->aggregate_size = field_total_sz;
            parent->array_dims = field_dims;
            parent->type_tag = field_tag;

            /* Bitfield metadata */
            if (field_key[0]) {
                int bf_bit_offset = ir_get_struct_field_bit_offset(field_key);
                int bf_bit_width  = ir_get_struct_field_bit_width(field_key);
                if (bf_bit_width > 0) {
                    parent->bit_offset = bf_bit_offset;
                    parent->bit_width  = bf_bit_width;
                }
            }
            n = parent;
        } else if (strcmp(peek(p), "++") == 0 || strcmp(peek(p), "--") == 0) {
            const char *op = take(p, nullptr);
            if (strcmp(n->op, "var") != 0 && strcmp(n->op, "index") != 0 && strcmp(n->op, "unary_*") != 0) {
                diagnostics_error(tok_line, tok_col, "lvalue required as increment operand");
            }
            char op_name[64];
            snprintf(op_name, sizeof(op_name), "postfix_%s", op);
            Node *parent = create_node(p, arena_strdup(p->arena, op_name), tok_line, tok_col);
            parent->name = n->name;
            parent->lhs = n;
            n = parent;
        }
    }
    return n;
}

static Node *unary(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    if (strcmp(peek(p), "&") == 0) {
        take(p, "&");
        Node *n = create_node(p, "unary_&", tok_line, tok_col);
        n->lhs = unary(p);
        n->type_size = p->target_scale;
        return n;
    }
    if (strcmp(peek(p), "*") == 0) {
        take(p, "*");
        Node *n = create_node(p, "unary_*", tok_line, tok_col);
        n->lhs = unary(p);
        n->type_size = p->target_scale;
        return n;
    }
    if (strcmp(peek(p), "+") == 0) {
        take(p, "+");
        return unary(p);
    }
    if (strcmp(peek(p), "++") == 0 || strcmp(peek(p), "--") == 0) {
        const char *op = take(p, nullptr);
        if (peek(p)[0] != '_' && !is_alpha(peek(p)[0])) {
            diagnostics_error(tok_line, tok_col, "lvalue required as increment operand");
        }
        char op_name[64];
        snprintf(op_name, sizeof(op_name), "prefix_%s", op);
        Node *n = create_node(p, arena_strdup(p->arena, op_name), tok_line, tok_col);
        const char *var_name = take(p, nullptr);
        HashMapEntry *sl_entry = hashmap_get(&p->current_static_locals, var_name);
        if (sl_entry) {
            var_name = (const char *)sl_entry->val_ptr;
        } else {
            var_name = resolve_name(p, var_name);
        }
        n->name = var_name;
        return n;
    }
    if (strcmp(peek(p), "~") == 0 || strcmp(peek(p), "!") == 0 || strcmp(peek(p), "-") == 0) {
        const char *op = take(p, nullptr);
        char op_name[64];
        snprintf(op_name, sizeof(op_name), "unary_%s", op);
        Node *n = create_node(p, arena_strdup(p->arena, op_name), tok_line, tok_col);
        n->lhs = unary(p);
        if (strcmp(op, "!") == 0) {
            n->type_size = 4;
            n->is_unsigned = 0;
        } else {
            n->type_size = n->lhs->type_size < 4 ? 4 : n->lhs->type_size;
            n->is_unsigned = n->lhs->is_unsigned;
            /* unary minus on a float keeps the floating type; '~'/'!' do not */
            if (strcmp(op, "-") == 0 && n->lhs->is_float) {
                n->is_float = 1;
                n->type_size = n->lhs->type_size;
            }
        }
        return n;
    }
    int is_sizeof = (strcmp(peek(p), "sizeof") == 0);
    int is_alignof = (strcmp(peek(p), "_Alignof") == 0 || strcmp(peek(p), "alignof") == 0);
    if (is_sizeof || is_alignof) {
        take(p, nullptr);
        if (paren_starts_type_name(p)) {
            take(p, "(");
            const char *s_tag = "";
            if (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) {
                if (p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "*") != 0 && strcmp(p->tokens.data[p->pos + 1].text, ")") != 0) {
                    s_tag = p->tokens.data[p->pos + 1].text;
                }
            }
            int base_size = parse_base_type(p);
            int stars = 0;
            while (strcmp(peek(p), "*") == 0) {
                take(p, "*");
                stars++;
                base_size = p->target_scale;
            }
            take(p, ")");
            Node *n = create_node(p, "num", tok_line, tok_col);
            n->value = is_sizeof ? base_size : alignof_type(p, base_size, s_tag, stars);
            n->type_size = 8;
            return n;
        }
        Node *target = unary(p);
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = is_sizeof ? sizeof_expr(p, target) : alignof_expr(p, target);
        n->type_size = 8;
        return n;
    }
    if (paren_starts_type_name(p)) {
        take(p, "(");
        int is_unsigned_cast = 0;
        int is_bool_cast = 0;
        int base_size = parse_base_type(p);
        is_unsigned_cast = p->last_type_unsigned;
        is_bool_cast = p->last_type_bool;
        int is_float_cast = p->last_type_float;
        while (strcmp(peek(p), "*") == 0) {
            take(p, "*");
            base_size = p->target_scale;
            is_unsigned_cast = 1; // pointers are unsigned
            is_float_cast = 0;    // pointer, not a floating value
        }
        take(p, ")");
        Node *n = create_node(p, is_bool_cast ? "bool_cast" : "cast", tok_line, tok_col);
        n->lhs = unary(p);
        n->value = base_size;
        n->type_size = base_size;
        n->is_unsigned = is_unsigned_cast;
        n->is_bool = is_bool_cast;
        n->is_float = is_float_cast;
        return n;
    }
    return factor(p);
}

static Node *term(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = unary(p);
    while (strcmp(peek(p), "*") == 0 || strcmp(peek(p), "/") == 0 || strcmp(peek(p), "%") == 0) {
        const char *op = take(p, nullptr);
        Node *rhs = unary(p);
        Node *parent = create_node(p, op, tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        /* Integer promotions: both operands promote to at least int (4 bytes) */
        int tls = n->type_size < 4 ? 4 : (int)n->type_size;
        int trs = rhs->type_size < 4 ? 4 : (int)rhs->type_size;
        /* Usual arithmetic conversions */
        if (tls == trs) {
            parent->type_size = tls;
            parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        } else if (tls > trs) {
            parent->type_size = tls;
            parent->is_unsigned = n->is_unsigned;
        } else {
            parent->type_size = trs;
            parent->is_unsigned = rhs->is_unsigned;
        }
        if (n->is_float || rhs->is_float) {
            parent->is_float = 1;
            int fs = n->is_float ? n->type_size : 0;
            if (rhs->is_float && rhs->type_size > fs) fs = rhs->type_size;
            parent->type_size = fs ? fs : 8;
            parent->is_unsigned = 0;
        }
        n = parent;
    }
    return n;
}

static Node *add(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = term(p);
    while (strcmp(peek(p), "+") == 0 || strcmp(peek(p), "-") == 0) {
        const char *op = take(p, nullptr);
        Node *rhs = term(p);
        Node *parent = create_node(p, op, tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        /* Integer promotions: both operands promote to at least int (4 bytes) */
        int als = n->type_size < 4 ? 4 : (int)n->type_size;
        int ars = rhs->type_size < 4 ? 4 : (int)rhs->type_size;
        /* Usual arithmetic conversions */
        if (als == ars) {
            parent->type_size = als;
            parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        } else if (als > ars) {
            parent->type_size = als;
            parent->is_unsigned = n->is_unsigned;
        } else {
            parent->type_size = ars;
            parent->is_unsigned = rhs->is_unsigned;
        }
        if (n->is_float || rhs->is_float) {
            parent->is_float = 1;
            int fs = n->is_float ? n->type_size : 0;
            if (rhs->is_float && rhs->type_size > fs) fs = rhs->type_size;
            parent->type_size = fs ? fs : 8;
            parent->is_unsigned = 0;
        }
        n = parent;
    }
    return n;
}

static Node *shift(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = add(p);
    while (strcmp(peek(p), "<<") == 0 || strcmp(peek(p), ">>") == 0) {
        const char *op = take(p, nullptr);
        Node *rhs = add(p);
        Node *parent = create_node(p, op, tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = n->type_size;
        parent->is_unsigned = n->is_unsigned;
        n = parent;
    }
    return n;
}

static Node *relational(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = shift(p);
    while (strcmp(peek(p), "<") == 0 || strcmp(peek(p), ">") == 0 || strcmp(peek(p), "<=") == 0 || strcmp(peek(p), ">=") == 0) {
        const char *op = take(p, nullptr);
        Node *rhs = shift(p);
        Node *parent = create_node(p, op, tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = 4; // comparison results in int
        if (n->type_size == rhs->type_size) {
            parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        } else if (n->is_unsigned && n->type_size > rhs->type_size) {
            parent->is_unsigned = 1;
        } else if (rhs->is_unsigned && rhs->type_size > n->type_size) {
            parent->is_unsigned = 1;
        }
        n = parent;
    }
    return n;
}

static Node *equality(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = relational(p);
    while (strcmp(peek(p), "==") == 0 || strcmp(peek(p), "!=") == 0) {
        const char *op = take(p, nullptr);
        Node *rhs = relational(p);
        Node *parent = create_node(p, op, tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = 4;
        n = parent;
    }
    return n;
}

static Node *bitwise_and(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = equality(p);
    while (strcmp(peek(p), "&") == 0) {
        take(p, "&");
        Node *rhs = equality(p);
        Node *parent = create_node(p, "&", tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = n->type_size;
        parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        n = parent;
    }
    return n;
}

static Node *bitwise_xor(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = bitwise_and(p);
    while (strcmp(peek(p), "^") == 0) {
        take(p, "^");
        Node *rhs = bitwise_and(p);
        Node *parent = create_node(p, "^", tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = n->type_size;
        parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        n = parent;
    }
    return n;
}

static Node *bitwise_or(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = bitwise_xor(p);
    while (strcmp(peek(p), "|") == 0) {
        take(p, "|");
        Node *rhs = bitwise_xor(p);
        Node *parent = create_node(p, "|", tok_line, tok_col);
        parent->lhs = n;
        parent->rhs = rhs;
        parent->type_size = n->type_size;
        parent->is_unsigned = n->is_unsigned || rhs->is_unsigned;
        n = parent;
    }
    return n;
}

static Node *logical_and(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = bitwise_or(p);
    while (strcmp(peek(p), "&&") == 0) {
        take(p, "&&");
        Node *rhs = bitwise_or(p);
        Node *parent = create_node(p, "&&", tok_line, tok_col);
        parent->lhs = bool_normalize(p, n);
        parent->rhs = bool_normalize(p, rhs);
        parent->type_size = 4;
        n = parent;
    }
    return n;
}

static Node *logical_or(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *n = logical_and(p);
    while (strcmp(peek(p), "||") == 0) {
        take(p, "||");
        Node *rhs = logical_and(p);
        Node *parent = create_node(p, "||", tok_line, tok_col);
        parent->lhs = bool_normalize(p, n);
        parent->rhs = bool_normalize(p, rhs);
        parent->type_size = 4;
        n = parent;
    }
    return n;
}

static Node *expr(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *cond = logical_or(p);
    if (strcmp(peek(p), "?") == 0) {
        take(p, "?");
        Node *true_expr = expr(p);
        take(p, ":");
        Node *false_expr = expr(p);
        Node *node = create_node(p, "?", tok_line, tok_col);
        node->lhs = cond;
        node_array_push(&node->body, true_expr);
        node_array_push(&node->body, false_expr);
        int lhs_size = node->body.data[0]->type_size < 4 ? 4 : node->body.data[0]->type_size;
        int rhs_size = node->body.data[1]->type_size < 4 ? 4 : node->body.data[1]->type_size;
        int lhs_unsigned = node->body.data[0]->type_size >= 4 && node->body.data[0]->is_unsigned;
        int rhs_unsigned = node->body.data[1]->type_size >= 4 && node->body.data[1]->is_unsigned;
        node->type_size = lhs_size > rhs_size ? lhs_size : rhs_size;
        node->is_unsigned = (lhs_size == rhs_size) ? (lhs_unsigned || rhs_unsigned) :
                            (lhs_size > rhs_size ? lhs_unsigned : rhs_unsigned);
        return node;
    }
    return cond;
}

typedef struct {
    long offset;
    Node *val;
    int size;
} InitElement;

typedef struct {
    InitElement *data;
    int count;
    int capacity;
} InitElementArray;

static void init_element_array_init(InitElementArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

static void init_element_array_push(InitElementArray *arr, const InitElement *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(InitElement));
    }
    arr->data[arr->count].offset = val->offset;
    arr->data[arr->count].val = val->val;
    arr->data[arr->count].size = val->size;
    arr->count = arr->count + 1;
}

static void init_element_array_free(InitElementArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

static void parse_aggregate_init(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits);

static void parse_aggregate_init_internal(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits, int has_braces) {
    (void)has_braces;
    if (struct_tag && struct_tag[0]) {
        HashMapEntry *entry = hashmap_get(&p->global_structs, struct_tag);
        if (!entry) parser_error(p, "unknown struct tag in initializer");
        HashMap *fields = (HashMap *)entry->val_ptr;
        
        // In order to preserve declaration order, we need to iterate fields logically or by offset.
        // HashMap doesn't guarantee order, but let's gather entries and sort them by offset.
        int f_count = fields->size;
        HashMapEntry **entries = malloc(f_count * sizeof(HashMapEntry*));
        int e_idx = 0;
        for (int b = 0; b < fields->bucket_count; ++b) {
            HashMapEntry *curr = fields->buckets[b];
            while (curr) {
                entries[e_idx++] = curr;
                curr = curr->next;
            }
        }
        // Bubble sort entries by offset (val_int)
        for (int j = 0; j < f_count; ++j) {
            for (int k = j + 1; k < f_count; ++k) {
                if (entries[j]->val_int > entries[k]->val_int) {
                    HashMapEntry *tmp = entries[j];
                    entries[j] = entries[k];
                    entries[k] = tmp;
                }
            }
        }

        for (int idx = 0; idx < f_count; ++idx) {
            HashMapEntry *f_entry = entries[idx];
            const char *field_name = f_entry->key;
            long field_offset = f_entry->val_int;
            if (strcmp(peek(p), ".") == 0) {
                take(p, ".");
                field_name = take(p, nullptr);
                take(p, "=");
                HashMapEntry *designated = hashmap_get(fields, field_name);
                if (!designated) parser_error(p, "unknown designated initializer field");
                field_offset = designated->val_int;
            }
            
            char key[256];
            snprintf(key, sizeof(key), "%s.%s", struct_tag, field_name);
            HashMapEntry *sz_entry = hashmap_get(&p->global_struct_field_sizes_by_tag, key);
            int field_size = sz_entry ? sz_entry->val_int : p->target_scale;
            
            HashMapEntry *dims_entry = hashmap_get(&p->global_struct_field_dims_by_tag, key);
            LongArray *sub_dims = dims_entry ? (LongArray *)dims_entry->val_ptr : nullptr;
            if (!sub_dims) {
                sub_dims = ir_get_struct_field_dims(key);
            }
            LongArray inferred_dims;
            int has_inferred_dims = 0;
            long_array_init(&inferred_dims);
            if (!sub_dims) {
                HashMapEntry *total_entry = hashmap_get(&p->global_struct_field_total_sizes_by_tag, key);
                if (total_entry && field_size > 0 && total_entry->val_int > field_size) {
                    long_array_push(&inferred_dims, total_entry->val_int / field_size);
                    sub_dims = &inferred_dims;
                    has_inferred_dims = 1;
                }
            }
            
            HashMapEntry *sub_tag_entry = hashmap_get(&p->global_struct_field_tags, key);
            const char *sub_tag = sub_tag_entry ? (const char *)sub_tag_entry->val_ptr : "";

            if ((sub_dims && sub_dims->count > 0) || (sub_tag && sub_tag[0])) {
                parse_aggregate_init(p, base_offset + field_offset, sub_tag, sub_dims, 0, field_size, inits);
            } else {
                Node *val = expr(p);
                InitElement item;
                item.offset = base_offset + field_offset;
                item.val = val;
                item.size = field_size;
                init_element_array_push(inits, &item);
            }
            if (has_inferred_dims) {
                long_array_free(&inferred_dims);
            }

            if (peek(p)[0] == ',') {
                take(p, ",");
            } else {
                break;
            }
        }
        free(entries);
    } else if (array_dims && dim_idx < (size_t)array_dims->count) {
        long limit = array_dims->data[dim_idx];
        
        long elem_total_size = base_type_size;
        for (size_t k = dim_idx + 1; k < (size_t)array_dims->count; ++k) {
            elem_total_size *= array_dims->data[k];
        }

        for (long idx = 0; idx < limit; ++idx) {
            long elem_offset = idx * elem_total_size;
            if (dim_idx + 1 < (size_t)array_dims->count) {
                if (strcmp(peek(p), "{") != 0) {
                    long flat_count = 1;
                    for (size_t k = dim_idx + 1; k < (size_t)array_dims->count; ++k) {
                        flat_count *= array_dims->data[k];
                    }
                    for (long flat_idx = 0; flat_idx < flat_count; ++flat_idx) {
                        Node *val = expr(p);
                        InitElement item;
                        item.offset = base_offset + elem_offset + flat_idx * base_type_size;
                        item.val = val;
                        item.size = base_type_size;
                        init_element_array_push(inits, &item);
                        if (flat_idx + 1 < flat_count && strcmp(peek(p), ",") == 0) {
                            take(p, ",");
                        }
                    }
                } else {
                    parse_aggregate_init(p, base_offset + elem_offset, struct_tag, array_dims, dim_idx + 1, base_type_size, inits);
                }
            } else {
                Node *val = expr(p);
                InitElement item;
                item.offset = base_offset + elem_offset;
                item.val = val;
                item.size = base_type_size;
                init_element_array_push(inits, &item);
            }

            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
            } else {
                break;
            }
        }
    }
}

static void fill_aggregate_zero(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits) {
    if (struct_tag && struct_tag[0]) {
        HashMapEntry *entry = hashmap_get(&p->global_structs, struct_tag);
        if (entry) {
            HashMap *fields = (HashMap *)entry->val_ptr;
            for (int b = 0; b < fields->bucket_count; ++b) {
                HashMapEntry *curr = fields->buckets[b];
                while (curr) {
                    const char *field_name = curr->key;
                    long field_offset = curr->val_int;
                    char key[256];
                    snprintf(key, sizeof(key), "%s.%s", struct_tag, field_name);
                    HashMapEntry *sz_entry = hashmap_get(&p->global_struct_field_sizes_by_tag, key);
                    int field_size = sz_entry ? sz_entry->val_int : p->target_scale;
                    HashMapEntry *dims_entry = hashmap_get(&p->global_struct_field_dims_by_tag, key);
                    LongArray *sub_dims = dims_entry ? (LongArray *)dims_entry->val_ptr : nullptr;
                    HashMapEntry *sub_tag_entry = hashmap_get(&p->global_struct_field_tags, key);
                    const char *sub_tag = sub_tag_entry ? (const char *)sub_tag_entry->val_ptr : "";
                    if ((sub_dims && sub_dims->count > 0) || (sub_tag && sub_tag[0])) {
                        fill_aggregate_zero(p, base_offset + field_offset, sub_tag, sub_dims, 0, field_size, inits);
                    } else {
                        Node *zero = create_node(p, "num", 1, 1);
                        zero->value = 0;
                        zero->type_size = field_size;
                        InitElement item;
                        item.offset = base_offset + field_offset;
                        item.val = zero;
                        item.size = field_size;
                        init_element_array_push(inits, &item);
                    }
                    curr = curr->next;
                }
            }
        }
    } else if (array_dims && dim_idx < (size_t)array_dims->count) {
        long limit = array_dims->data[dim_idx];
        long elem_total_size = base_type_size;
        for (size_t k = dim_idx + 1; k < (size_t)array_dims->count; ++k) {
            elem_total_size *= array_dims->data[k];
        }
        for (long idx = 0; idx < limit; ++idx) {
            long elem_offset = idx * elem_total_size;
            if (dim_idx + 1 < (size_t)array_dims->count) {
                fill_aggregate_zero(p, base_offset + elem_offset, struct_tag, array_dims, dim_idx + 1, base_type_size, inits);
            } else {
                Node *zero = create_node(p, "num", 1, 1);
                zero->value = 0;
                zero->type_size = base_type_size;
                InitElement item;
                item.offset = base_offset + elem_offset;
                item.val = zero;
                item.size = base_type_size;
                init_element_array_push(inits, &item);
            }
        }
    }
}

static void parse_aggregate_init(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits) {
    if (strcmp(peek(p), "{") == 0) {
        take(p, "{");
        if (strcmp(peek(p), "}") == 0) {
            take(p, "}");
            fill_aggregate_zero(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits);
        } else {
            parse_aggregate_init_internal(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits, 1);
            take(p, "}");
        }
    } else {
        parse_aggregate_init_internal(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits, 0);
    }
}

static Node *assign_stmt(ParserState *p, int semicolon) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    const char *name = take(p, nullptr);
    const char *resolved_name = name;
    HashMapEntry *sl_entry = hashmap_get(&p->current_static_locals, name);
    if (sl_entry) {
        resolved_name = (const char *)sl_entry->val_ptr;
    } else {
        resolved_name = resolve_name(p, name);
    }
    HashMapEntry *const_entry = hashmap_get(&p->const_vars, resolved_name);
    if (const_entry && const_entry->val_int) {
        diagnostics_error(tok_line, tok_col, "assignment of read-only (const-qualified) variable");
    }
    const char *op = take(p, nullptr);
    Node *node = create_node(p, "assign", tok_line, tok_col);
    node->name = resolved_name;
    if (strcmp(op, "=") == 0) {
        node->lhs = expr(p);
        HashMapEntry *b_entry = hashmap_get(&p->bool_vars, resolved_name);
        if (b_entry && b_entry->val_int) {
            node->lhs = bool_normalize(p, node->lhs);
        }
    } else {
        char bin_op[2] = { op[0], '\0' };
        const char *bin_op_dup = arena_strdup(p->arena, bin_op);
        Node *bin_node = create_node(p, bin_op_dup, tok_line, tok_col);
        
        Node *var_node = create_node(p, "var", tok_line, tok_col);
        var_node->name = resolved_name;
        HashMapEntry *u_entry = hashmap_get(&p->unsigned_vars, resolved_name);
        var_node->is_unsigned = u_entry ? u_entry->val_int : 0;
        HashMapEntry *sz_entry = hashmap_get(&p->value_sizes, resolved_name);
        var_node->type_size = sz_entry ? sz_entry->val_int : p->target_scale;
        HashMapEntry *b_entry = hashmap_get(&p->bool_vars, resolved_name);
        var_node->is_bool = b_entry ? b_entry->val_int : 0;
        
        bin_node->lhs = var_node;
        bin_node->rhs = expr(p);
        apply_integer_conversion(p, bin_node);
        node->lhs = bin_node;
        
        if (b_entry && b_entry->val_int) {
            node->lhs = bool_normalize(p, node->lhs);
        }
    }
    if (semicolon) {
        take(p, ";");
    }
    return node;
}


static void parse_static_assert(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    if (strcmp(peek(p), "_Static_assert") == 0) {
        take(p, "_Static_assert");
    } else {
        take(p, "static_assert");
    }
    take(p, "(");
    Node *expr_node = expr(p);
    const char *msg_lit = "\"static assertion failed\"";
    if (strcmp(peek(p), ",") == 0) {
        take(p, ",");
        msg_lit = take(p, nullptr);
    }
    take(p, ")");
    take(p, ";");
    long val = eval_const(p, expr_node);
    if (!val) {
        char clean_msg[512];
        clean_msg[0] = '\0';
        size_t len = strlen(msg_lit);
        if (len > 2 && msg_lit[0] == '"') {
            snprintf(clean_msg, sizeof(clean_msg), "%.*s", (int)(len - 2), msg_lit + 1);
        } else {
            snprintf(clean_msg, sizeof(clean_msg), "%s", msg_lit);
        }
        diagnostics_error(tok_line, tok_col, clean_msg);
    }
}

static Node *stmt(ParserState *p) {
    skip_attribute(p);
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    if (strcmp(peek(p), "_Static_assert") == 0 || strcmp(peek(p), "static_assert") == 0) {
        parse_static_assert(p);
        return create_node(p, "block", tok_line, tok_col);
    }
    if (strcmp(peek(p), "{") == 0) {
        return block_stmt(p);
    }
    if (strcmp(peek(p), "__asm__") == 0 || strcmp(peek(p), "__asm") == 0 || strcmp(peek(p), "asm") == 0) {
        take(p, nullptr);
        if (strcmp(peek(p), "volatile") == 0 || strcmp(peek(p), "__volatile__") == 0) {
            take(p, nullptr);
        }
        take(p, "(");
        int parens = 1;
        while (parens > 0 && strcmp(peek(p), "EOF") != 0) {
            const char *t = take(p, nullptr);
            if (strcmp(t, "(") == 0) parens++;
            if (strcmp(t, ")") == 0) parens--;
        }
        take(p, ";");
        return create_node(p, "block", tok_line, tok_col);
    }
    
    int is_static = 0;
    if (strcmp(peek(p), "static") == 0) {
        take(p, "static");
        is_static = 1;
    }
    
    if (strcmp(peek(p), "typedef") == 0) {
        take(p, "typedef");
        parser_type(p);
        const char *alias = take(p, nullptr);
        take(p, ";");
        hashmap_put(&p->global_typedefs, alias, nullptr, 1);
        return create_node(p, "block", tok_line, tok_col);
    }
    
    if (strcmp(peek(p), "enum") == 0) {
        take(p, "enum");
        if (strcmp(peek(p), "{") != 0) {
            take(p, nullptr);
        }
        take(p, "{");
        int val = 0;
        while (1) {
            if (strcmp(peek(p), "}") == 0) {
                break;
            }
            const char *name = take(p, nullptr);
            if (strcmp(peek(p), "=") == 0) {
                take(p, "=");
                val = strtol(take(p, nullptr), nullptr, 10);
            }
            hashmap_put(&p->global_enums, name, (void *)(long)val, 0);
            val++;
            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
            } else {
                break;
            }
        }
        take(p, "}");
        take(p, ";");
        return create_node(p, "block", tok_line, tok_col);
    }
    
    if ((strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) &&
        p->pos + 2 < p->tokens.count && strcmp(p->tokens.data[p->pos + 2].text, "{") == 0) {
        
        int is_union_local = (strcmp(peek(p), "union") == 0);
        take(p, nullptr);
        const char *tag = take(p, nullptr);
        take(p, "{");
        int offset = 0;
        int union_max_sz = 0;
        int bit_offset2 = 0;
        int current_unit_size2 = 0;
        HashMap fields;
        hashmap_init(&fields, 16);
        while (strcmp(peek(p), "}") != 0) {
            const char *f_tag = "";
            if (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) {
                if (p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
                    f_tag = p->tokens.data[p->pos + 1].text;
                }
            }
            int fbase = parse_base_type(p);
            if (!f_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
                f_tag = p->last_parsed_struct_tag;
            }
            while (1) {
                int fstars = 0;
                while (strcmp(peek(p), "*") == 0) {
                    take(p, "*");
                    fstars++;
                }
                int fsize = (fstars > 0) ? p->target_scale : fbase;
                
                /* Detect bitfield */
                const char *field_name;
                int is_bf = 0;
                int bf_width = 0;
                if (strcmp(peek(p), ":") == 0) {
                    /* anonymous bitfield */
                    is_bf = 1;
                    static int anon_bf2_id = 0;
                    char tmp_name[128];
                    snprintf(tmp_name, sizeof(tmp_name), "$anon_bf2_%d", anon_bf2_id++);
                    field_name = arena_strdup(p->arena, tmp_name);
                } else {
                    field_name = take(p, nullptr);
                    if (strcmp(peek(p), ":") == 0) {
                        is_bf = 1;
                    }
                }
                if (is_bf) {
                    take(p, ":");
                    Node *bf_expr = expr(p);
                    bf_width = (int)eval_const(p, bf_expr);
                }

                if (is_bf) {
                    int w = bf_width;
                    if (current_unit_size2 != fsize || bit_offset2 + w > fsize * 8 || w == 0) {
                        if (bit_offset2 > 0) offset += current_unit_size2;
                        int align = fsize; if (align > 8) align = 8;
                        if (align > 1 && (offset % align) != 0)
                            offset += align - (offset % align);
                        bit_offset2 = 0;
                        current_unit_size2 = fsize;
                    }
                    hashmap_put(&fields, field_name, nullptr, offset);
                    hashmap_put(&p->global_field_offsets, field_name, nullptr, offset);
                    hashmap_put(&p->global_field_sizes, field_name, nullptr, fsize);
                    if (tag[0]) {
                        char key[512];
                        snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                        const char *key_dup = arena_strdup(p->arena, key);
                        hashmap_put(&p->global_struct_field_offsets_by_tag, key_dup, nullptr, offset);
                        hashmap_put(&p->global_struct_field_sizes_by_tag, key_dup, nullptr, fsize);
                        hashmap_put(&p->global_struct_field_bit_offsets_by_tag, key_dup, nullptr, bit_offset2);
                        hashmap_put(&p->global_struct_field_bit_widths_by_tag, key_dup, nullptr, w);
                        ir_set_struct_field_bit_offset(key_dup, bit_offset2);
                        ir_set_struct_field_bit_width(key_dup, w);
                    }
                    bit_offset2 += w;
                } else {
                    if (bit_offset2 > 0) {
                        offset += current_unit_size2;
                        bit_offset2 = 0;
                        current_unit_size2 = 0;
                    }
                    long arr_cnt = 1;
                    LongArray field_dims;
                    long_array_init(&field_dims);
                    if (strcmp(peek(p), "[") == 0) {
                        while (strcmp(peek(p), "[") == 0) {
                            take(p, "[");
                            long dim = 0;
                            if (strcmp(peek(p), "]") != 0) {
                                Node *sz = expr(p);
                                dim = eval_const(p, sz);
                                arr_cnt *= dim;
                            }
                            take(p, "]");
                            long_array_push(&field_dims, dim);
                        }
                    }
                    int align = fsize;
                    if (align > 8) align = 8;
                    if (!is_union_local && align > 1 && (offset % align) != 0) {
                        offset += align - (offset % align);
                    }
                    
                    if (tag[0] && f_tag[0]) {
                        char field_key[512];
                        snprintf(field_key, sizeof(field_key), "%s.%s", tag, field_name);
                        hashmap_put(&p->global_struct_field_tags, arena_strdup(p->arena, field_key), (void *)f_tag, 0);
                    }
                    
                    hashmap_put(&fields, field_name, nullptr, offset);
                    hashmap_put(&p->global_field_offsets, field_name, nullptr, offset);
                    hashmap_put(&p->global_field_sizes, field_name, nullptr, fsize);
                    
                    int total = (int)(fsize * arr_cnt);
                    if (tag[0]) {
                        char key[512];
                        snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                        const char *key_dup = arena_strdup(p->arena, key);
                        
                        hashmap_put(&p->global_struct_field_offsets_by_tag, key_dup, nullptr, offset);
                        hashmap_put(&p->global_struct_field_sizes_by_tag, key_dup, nullptr, fsize);
                        if (fstars > 0) {
                            int field_elem_size = (fstars > 1) ? p->target_scale : fbase;
                            hashmap_put(&p->global_struct_field_elem_sizes_by_tag, key_dup, nullptr, field_elem_size);
                        }
                        hashmap_put(&p->global_struct_field_total_sizes_by_tag, key_dup, nullptr, total);
                        LongArray *dims_copy = malloc(sizeof(LongArray));
                        long_array_init(dims_copy);
                        for (int d_i = 0; d_i < field_dims.count; ++d_i) {
                            long_array_push(dims_copy, field_dims.data[d_i]);
                        }
                        hashmap_put(&p->global_struct_field_dims_by_tag, key_dup, dims_copy, 0);
                        ir_set_struct_field_dims(key_dup, field_dims);
                    }
                    
                    if (is_union_local) {
                        if (total > union_max_sz) union_max_sz = total;
                    } else {
                        offset += total;
                    }
                    long_array_free(&field_dims);
                }
                
                if (strcmp(peek(p), ",") == 0) {
                    take(p, ",");
                } else {
                    break;
                }
            }
            take(p, ";");
        }
        take(p, "}");
        take(p, ";");
        if (bit_offset2 > 0) offset += current_unit_size2;
        int struct_byte_size = is_union_local ? union_max_sz : offset;
        if (struct_byte_size > 0 && !is_union_local && (struct_byte_size % p->target_scale) != 0) {
            struct_byte_size += p->target_scale - (struct_byte_size % p->target_scale);
        }
        if (tag[0]) {
            HashMap *fields_alloc = malloc(sizeof(HashMap));
            *fields_alloc = fields;
            hashmap_put(&p->global_structs, tag, fields_alloc, 0);
            hashmap_put(&p->global_struct_sizes, tag, nullptr, struct_byte_size);
        } else {
            hashmap_free(&fields);
        }
        return create_node(p, "block", tok_line, tok_col);
    }
    
    const char *t = peek(p);
    if (strcmp(t, "constexpr") == 0 ||
        strcmp(t, "int") == 0 || strcmp(t, "char") == 0 || strcmp(t, "short") == 0 || strcmp(t, "long") == 0 ||
        strcmp(t, "void") == 0 || strcmp(t, "unsigned") == 0 || strcmp(t, "signed") == 0 || strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0 ||
        strcmp(t, "const") == 0 || strcmp(t, "volatile") == 0 || strcmp(t, "restrict") == 0 || strcmp(t, "__restrict") == 0 || strcmp(t, "__restrict__") == 0 || strcmp(t, "register") == 0 || strcmp(t, "_Atomic") == 0 ||
        strcmp(t, "float") == 0 || strcmp(t, "double") == 0 || strcmp(t, "struct") == 0 || strcmp(t, "union") == 0 ||
        hashmap_has(&p->global_typedefs, t)) {
        
        const char *struct_tag = "";
        if (strcmp(t, "struct") == 0 || strcmp(t, "union") == 0) {
            if (p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
                struct_tag = p->tokens.data[p->pos + 1].text;
            }
        }
        int is_constexpr = 0;
        if (strcmp(peek(p), "constexpr") == 0) {
            take(p, "constexpr");
            is_constexpr = 1;
        }
        int base_size = parse_base_type(p);
        if (!struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
            struct_tag = p->last_parsed_struct_tag;
        }
        if (is_constexpr) {
            int is_unsigned = p->last_type_unsigned;
            int is_bool = p->last_type_bool;
            int stars = 0;
            while (strcmp(peek(p), "*") == 0) {
                take(p, "*");
                stars++;
            }
            const char *name = take(p, nullptr);
            take(p, "=");
            Node *init_expr = expr(p);
            take(p, ";");
            long val = eval_const(p, init_expr);
            
            char unique_name[256];
            snprintf(unique_name, sizeof(unique_name), "%s$%s", p->current_func_name, name);
            const char *unique_name_dup = arena_strdup(p->arena, unique_name);
            
            hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)unique_name_dup, 0);
            hashmap_put(&p->unsigned_vars, unique_name_dup, nullptr, is_unsigned);
            hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool);
            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, (stars > 0) ? p->target_scale : base_size);
            hashmap_put(&p->constexpr_vars, unique_name_dup, nullptr, val);
            return create_node(p, "block", tok_line, tok_col);
        }
        int is_unsigned = p->last_type_unsigned;
        int is_bool = p->last_type_bool || strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0;
        int base_const = p->last_type_const;
        int base_volatile = p->last_type_volatile;
        int base_float = p->last_type_float;

        if (strcmp(peek(p), ";") == 0) {
            take(p, ";");
            return create_node(p, "block", tok_line, tok_col);
        }

        int stars = 0;
        while (strcmp(peek(p), "*") == 0) {
            take(p, "*");
            stars++;
        }
        p->last_type_const = 0;
        p->last_type_volatile = 0;
        skip_type_qualifiers(p);
        /* Object is const/volatile if the qualifier applies at top level:
           directly on a non-pointer object, or after the final '*' on a pointer. */
        int is_const = (stars == 0) ? (base_const || p->last_type_const) : p->last_type_const;
        int is_volatile = (stars == 0) ? (base_volatile || p->last_type_volatile) : p->last_type_volatile;
        skip_attribute_and_asm(p);
        
        const char *name = "";
        int is_func_ptr = 0;
        if (strcmp(peek(p), "(") == 0) {
            take(p, "(");
            while (strcmp(peek(p), "*") == 0) {
                take(p, "*");
            }
            skip_type_qualifiers(p);
            name = take(p, nullptr);
            take(p, ")");
            take(p, "(");
            int depth = 1;
            while (depth > 0 && p->pos < p->tokens.count) {
                if (strcmp(peek(p), "(") == 0) depth++;
                else if (strcmp(peek(p), ")") == 0) depth--;
                take(p, nullptr);
            }
            is_func_ptr = 1;
        } else {
            name = take(p, nullptr);
        }
        skip_attribute_and_asm(p);
        
        int is_pointer = (stars > 0 || is_func_ptr);
        int elem_scale = 1;
        if (stars == 1 && !is_func_ptr) {
            elem_scale = base_size;
        } else if (stars > 1 || is_func_ptr) {
            elem_scale = p->target_scale;
        }
        
        int is_aggregate = (strcmp(peek(p), "[") == 0 || (struct_tag[0] && stars == 0));
        
        if (is_aggregate) {
            LongArray dims;
            long_array_init(&dims);
            if (strcmp(peek(p), "[") == 0) {
                while (strcmp(peek(p), "[") == 0) {
                    take(p, "[");
                    long dim_size = 0;
                    if (strcmp(peek(p), "]") != 0) {
                        dim_size = strtol(take(p, nullptr), nullptr, 10);
                    }
                    take(p, "]");
                    long_array_push(&dims, dim_size);
                }
            }
            long total_size = 1;
            for (int k = 0; k < dims.count; ++k) {
                total_size *= dims.data[k];
            }
            
            long orig_base_size = base_size;
            if (struct_tag[0]) {
                long struct_slots = (base_size + p->target_scale - 1) / p->target_scale;
                total_size = total_size * struct_slots;
                base_size = p->target_scale;
                if (dims.count == 0) {
                    long_array_push(&dims, struct_slots);
                } else {
                    dims.data[0] = total_size;
                }
            }
            
            if (is_static) {
                char unique_name[256];
                snprintf(unique_name, sizeof(unique_name), "%s$%s", p->current_func_name, name);
                const char *unique_name_dup = arena_strdup(p->arena, unique_name);
                
                InitElementArray parsed_inits;
                init_element_array_init(&parsed_inits);
                int used_string_array_init = 0;
                if (strcmp(peek(p), "=") == 0) {
                    take(p, "=");
                    if (strcmp(peek(p), "{") == 0) {
                        int is_string_array_init = (dims.count > 0 &&
                                                    p->pos + 1 < p->tokens.count &&
                                                    p->tokens.data[p->pos + 1].text[0] == '"' &&
                                                    (stars > 0 || orig_base_size == 1));
                        if (is_string_array_init) {
                            take(p, "{");
                            while (strcmp(peek(p), "}") != 0) {
                                if (peek(p)[0] == '"') {
                                    take(p, nullptr);
                                } else {
                                    expr(p);
                                }
                                if (strcmp(peek(p), ",") == 0) {
                                    take(p, ",");
                                } else {
                                    break;
                                }
                            }
                            take(p, "}");
                            used_string_array_init = 1;
                        } else {
                            parse_aggregate_init(p, 0, struct_tag, &dims, 0, orig_base_size, &parsed_inits);
                        }
                    } else {
                        Node *init = expr(p);
                        if (is_bool && stars == 0) {
                            init = bool_normalize(p, init);
                        }
                        InitElement item;
                        item.offset = 0;
                        item.val = init;
                        item.size = (int)orig_base_size;
                        init_element_array_push(&parsed_inits, &item);
                    }
                }
                
                LongArray inits;
                long_array_init(&inits);
                if (used_string_array_init) {
                    long total_byte_size = total_size * base_size;
                    for (long b_i = 0; b_i < total_byte_size; ++b_i) {
                        long_array_push(&inits, 0);
                    }
                    base_size = 1;
                } else if (parsed_inits.count > 0) {
                    long total_byte_size = total_size * base_size;
                    char *byte_buf = calloc(total_byte_size + 1, 1);
                    for (int item_i = 0; item_i < parsed_inits.count; ++item_i) {
                        long val = eval_const(p, parsed_inits.data[item_i].val);
                        for (int b = 0; b < parsed_inits.data[item_i].size; ++b) {
                            if (parsed_inits.data[item_i].offset + b < total_byte_size) {
                                byte_buf[parsed_inits.data[item_i].offset + b] = (val >> (b * 8)) & 0xff;
                            }
                        }
                    }
                    for (long b_i = 0; b_i < total_byte_size; ++b_i) {
                        long_array_push(&inits, byte_buf[b_i]);
                    }
                    free(byte_buf);
                    base_size = 1;
                }
                init_element_array_free(&parsed_inits);
                
                ir_declare_global(unique_name_dup, 1, total_size, 1, 0, (int)base_size, p->target_scale);
                ir_set_global_array_dims(unique_name_dup, dims);
                ir_set_global_array_base_size(unique_name_dup, base_size);
                ir_set_global_initializers(unique_name_dup, inits);
                
                hashmap_put(&p->current_static_locals, name, (void *)unique_name_dup, 0);
                if (struct_tag[0]) {
                    hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
                }
                hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && stars == 0);
                
                take(p, ";");
                return create_node(p, "block", tok_line, tok_col);
            } else {
                char unique_name[256];
                snprintf(unique_name, sizeof(unique_name), "%s$%d", name, p->local_var_counter++);
                const char *unique_name_dup = arena_strdup(p->arena, unique_name);
                
                if (hashmap_has(&p->scopes[p->scope_count - 1], name)) {
                    diagnostics_error(tok_line, tok_col, "redefinition of local variable");
                }
                hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)unique_name_dup, 0);
                if (struct_tag[0]) {
                    hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
                }
                
                Node *node = create_node(p, "array_decl", tok_line, tok_col);
                node->name = unique_name_dup;
                node->value = total_size;
                
                char local_key[512];
                snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
                const char *local_key_dup = arena_strdup(p->arena, local_key);
                
                ir_set_local_array_dims(local_key_dup, dims);
                if (stars > 0) {
                    base_size = p->target_scale;
                    orig_base_size = p->target_scale;
                }
                ir_set_local_array_base_size(local_key_dup, base_size);
                ir_set_local_var_is_pointer(local_key_dup, stars > 0);
                ir_set_local_var_elem_scale(local_key_dup, elem_scale);
                
                hashmap_put(&p->value_sizes, unique_name_dup, nullptr, total_size * base_size);
                hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && stars == 0 && dims.count == 0);
                
                if (strcmp(peek(p), "=") == 0) {
                    take(p, "=");
                    if (strcmp(peek(p), "{") == 0) {
                        InitElementArray local_inits;
                        init_element_array_init(&local_inits);
                        parse_aggregate_init(p, 0, struct_tag, &dims, 0, orig_base_size, &local_inits);
                        for (int item_i = 0; item_i < local_inits.count; ++item_i) {
                            Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                            item_node->value = local_inits.data[item_i].offset;
                            char sz_str[64];
                            snprintf(sz_str, sizeof(sz_str), "%d", local_inits.data[item_i].size);
                            item_node->name = arena_strdup(p->arena, sz_str);
                            item_node->lhs = local_inits.data[item_i].val;
                            node_array_push(&node->body, item_node);
                        }
                        init_element_array_free(&local_inits);
                    } else if (dims.count > 0 && orig_base_size == 1 && peek(p)[0] == '"') {
                        const char *lit = take(p, nullptr);
                        size_t lit_len = strlen(lit);
                        size_t byte_count = lit_len >= 2 ? lit_len - 2 : 0;
                        if (dims.data[0] == 0) {
                            dims.data[0] = (long)byte_count + 1;
                            total_size = dims.data[0];
                            node->value = total_size;
                            ir_set_local_array_dims(local_key_dup, dims);
                            ir_set_local_array_base_size(local_key_dup, base_size);
                            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, total_size * base_size);
                        }
                        for (size_t ch_i = 0; ch_i <= byte_count; ++ch_i) {
                            Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                            item_node->value = (long)ch_i;
                            item_node->name = "1";
                            Node *ch_node = create_node(p, "num", tok_line, tok_col);
                            ch_node->value = (ch_i < byte_count) ? lit[ch_i + 1] : 0;
                            ch_node->type_size = 1;
                            item_node->lhs = ch_node;
                            node_array_push(&node->body, item_node);
                        }
                    } else {
                        Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                        item_node->value = 0;
                        char sz_str[64];
                        snprintf(sz_str, sizeof(sz_str), "%ld", orig_base_size);
                        item_node->name = arena_strdup(p->arena, sz_str);
                        Node *init = expr(p);
                        if (is_bool && stars == 0) {
                            init = bool_normalize(p, init);
                        }
                        item_node->lhs = init;
                        node_array_push(&node->body, item_node);
                    }
                }
                take(p, ";");
                return node;
            }
        }
        
        // Scalar variables
        if (is_static) {
            char unique_name[256];
            snprintf(unique_name, sizeof(unique_name), "%s$%s", p->current_func_name, name);
            const char *unique_name_dup = arena_strdup(p->arena, unique_name);
            
            LongArray inits;
            long_array_init(&inits);
            if (strcmp(peek(p), "=") == 0) {
                take(p, "=");
                Node *init = expr(p);
                if (is_bool && stars == 0) {
                    init = bool_normalize(p, init);
                }
                long_array_push(&inits, eval_const(p, init));
            }
            
            ir_declare_global(unique_name_dup, 0, 1, 1, 0, (stars > 0) ? p->target_scale : base_size, p->target_scale);
            ir_set_global_var_is_pointer(unique_name_dup, is_pointer);
            ir_set_global_var_elem_scale(unique_name_dup, elem_scale);
            ir_set_global_initializers(unique_name_dup, inits);
            
            hashmap_put(&p->current_static_locals, name, (void *)unique_name_dup, 0);
            if (struct_tag[0]) {
                hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
            }
            hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && !is_pointer);
            
            take(p, ";");
            return create_node(p, "block", tok_line, tok_col);
        } else {
            char unique_name[256];
            snprintf(unique_name, sizeof(unique_name), "%s$%d", name, p->local_var_counter++);
            const char *unique_name_dup = arena_strdup(p->arena, unique_name);
            
            if (hashmap_has(&p->scopes[p->scope_count - 1], name)) {
                diagnostics_error(tok_line, tok_col, "redefinition of local variable");
            }
            hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)unique_name_dup, 0);
            if (struct_tag[0]) {
                hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
            }
            
            Node *node = create_node(p, "decl", tok_line, tok_col);
            node->name = unique_name_dup;
            
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
            const char *local_key_dup = arena_strdup(p->arena, local_key);
            
            ir_set_local_var_is_pointer(local_key_dup, is_pointer);
            ir_set_local_var_elem_scale(local_key_dup, elem_scale);
            
            hashmap_put(&p->unsigned_vars, unique_name_dup, nullptr, is_unsigned && !is_pointer);
            hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && !is_pointer);
            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, is_pointer ? p->target_scale : base_size);
            hashmap_put(&p->const_vars, unique_name_dup, nullptr, is_const);
            hashmap_put(&p->volatile_vars, unique_name_dup, nullptr, is_volatile);
            hashmap_put(&p->float_vars, unique_name_dup, nullptr, base_float && !is_pointer);
            node->is_float = base_float && !is_pointer;
            node->type_size = is_pointer ? p->target_scale : base_size;

            if (strcmp(peek(p), "=") == 0) {
                take(p, "=");
                node->lhs = expr(p);
                if (is_bool && !is_pointer) {
                    node->lhs = bool_normalize(p, node->lhs);
                }
            }
            
            if (strcmp(peek(p), ",") == 0) {
                Node *block = create_node(p, "block", tok_line, tok_col);
                node_array_push(&block->body, node);
                while (strcmp(peek(p), ",") == 0) {
                    take(p, ",");
                    int next_stars = 0;
                    while (strcmp(peek(p), "*") == 0) {
                        take(p, "*");
                        next_stars++;
                    }
                    skip_type_qualifiers(p);
                    skip_attribute_and_asm(p);
                    
                    const char *next_name = "";
                    int next_is_func_ptr = 0;
                    if (strcmp(peek(p), "(") == 0) {
                        take(p, "(");
                        while (strcmp(peek(p), "*") == 0) {
                            take(p, "*");
                        }
                        next_name = take(p, nullptr);
                        take(p, ")");
                        take(p, "(");
                        int depth = 1;
                        while (depth > 0 && p->pos < p->tokens.count) {
                            if (strcmp(peek(p), "(") == 0) depth++;
                            else if (strcmp(peek(p), ")") == 0) depth--;
                            take(p, nullptr);
                        }
                        next_is_func_ptr = 1;
                    } else {
                        next_name = take(p, nullptr);
                    }
                    skip_attribute_and_asm(p);
                    
                    char next_unique_name[256];
                    snprintf(next_unique_name, sizeof(next_unique_name), "%s$%d", next_name, p->local_var_counter++);
                    const char *next_unique_name_dup = arena_strdup(p->arena, next_unique_name);
                    
                    if (hashmap_has(&p->scopes[p->scope_count - 1], next_name)) {
                        diagnostics_error(tok_line, tok_col, "redefinition of local variable");
                    }
                    hashmap_put(&p->scopes[p->scope_count - 1], next_name, (void *)next_unique_name_dup, 0);
                    
                    Node *next_node = create_node(p, "decl", tok_line, tok_col);
                    next_node->name = next_unique_name_dup;
                    
                    int next_is_pointer = (next_stars > 0 || next_is_func_ptr);
                    int next_elem_scale = next_is_func_ptr ? p->target_scale : (next_stars == 1 ? base_size : (next_stars > 1 ? p->target_scale : 1));
                    
                    char next_local_key[512];
                    snprintf(next_local_key, sizeof(next_local_key), "%s$%s", p->current_func_name, next_unique_name_dup);
                    const char *next_local_key_dup = arena_strdup(p->arena, next_local_key);
                    
                    ir_set_local_var_is_pointer(next_local_key_dup, next_is_pointer);
                    ir_set_local_var_elem_scale(next_local_key_dup, next_elem_scale);
                    
                    hashmap_put(&p->unsigned_vars, next_unique_name_dup, nullptr, is_unsigned && !next_is_pointer);
                    hashmap_put(&p->bool_vars, next_unique_name_dup, nullptr, is_bool && !next_is_pointer);
                    hashmap_put(&p->value_sizes, next_unique_name_dup, nullptr, next_is_pointer ? p->target_scale : base_size);
                    hashmap_put(&p->float_vars, next_unique_name_dup, nullptr, base_float && !next_is_pointer);
                    next_node->is_float = base_float && !next_is_pointer;
                    next_node->type_size = next_is_pointer ? p->target_scale : base_size;

                    if (strcmp(peek(p), "=") == 0) {
                        take(p, "=");
                        next_node->lhs = expr(p);
                        if (is_bool && !next_is_pointer) {
                            next_node->lhs = bool_normalize(p, next_node->lhs);
                        }
                    }
                    node_array_push(&block->body, next_node);
                }
                take(p, ";");
                return block;
            }
            take(p, ";");
            return node;
        }
    }
    
    if (strcmp(peek(p), "return") == 0) {
        take(p, "return");
        Node *val = nullptr;
        if (strcmp(peek(p), ";") != 0) {
            val = expr(p);
        }
        take(p, ";");
        Node *n = create_node(p, "return", tok_line, tok_col);
        n->lhs = val;
        return n;
    }
    if (strcmp(peek(p), "break") == 0) {
        take(p, "break");
        take(p, ";");
        return create_node(p, "break", tok_line, tok_col);
    }
    if (strcmp(peek(p), "continue") == 0) {
        take(p, "continue");
        take(p, ";");
        return create_node(p, "continue", tok_line, tok_col);
    }
    if (strcmp(peek(p), "switch") == 0) {
        take(p, "switch");
        take(p, "(");
        Node *cond = expr(p);
        take(p, ")");
        Node *body_node = stmt(p);
        Node *n = create_node(p, "switch", tok_line, tok_col);
        n->lhs = cond;
        n->rhs = body_node;
        return n;
    }
    if (strcmp(peek(p), "case") == 0) {
        take(p, "case");
        Node *case_expr = expr(p);
        long val = eval_const(p, case_expr);
        take(p, ":");
        Node *n = create_node(p, "case", tok_line, tok_col);
        n->value = val;
        return n;
    }
    if (strcmp(peek(p), "default") == 0) {
        take(p, "default");
        take(p, ":");
        return create_node(p, "default", tok_line, tok_col);
    }
    if (strcmp(peek(p), "if") == 0) {
        take(p, "if");
        take(p, "(");
        Node *cond = expr(p);
        take(p, ")");
        Node *then_branch = stmt(p);
        Node *else_branch = nullptr;
        if (strcmp(peek(p), "else") == 0) {
            take(p, "else");
            else_branch = stmt(p);
        }
        Node *n = create_node(p, "if", tok_line, tok_col);
        n->lhs = bool_normalize(p, cond);
        node_array_push(&n->body, then_branch);
        if (else_branch) {
            node_array_push(&n->body, else_branch);
        }
        return n;
    }
    if (strcmp(peek(p), "while") == 0) {
        take(p, "while");
        take(p, "(");
        Node *cond = expr(p);
        take(p, ")");
        Node *body_node = stmt(p);
        Node *n = create_node(p, "while", tok_line, tok_col);
        n->lhs = bool_normalize(p, cond);
        n->rhs = body_node;
        return n;
    }
    if (strcmp(peek(p), "for") == 0) {
        take(p, "for");
        take(p, "(");
        
        parser_enter_scope(p);

        Node *init_node = nullptr;
        if (strcmp(peek(p), ";") != 0) {
            const char *t_for = peek(p);
            if (strcmp(t_for, "int") == 0 || strcmp(t_for, "char") == 0 || strcmp(t_for, "short") == 0 || strcmp(t_for, "long") == 0 ||
                strcmp(t_for, "void") == 0 || strcmp(t_for, "unsigned") == 0 || strcmp(t_for, "signed") == 0 || strcmp(t_for, "_Bool") == 0 || strcmp(t_for, "bool") == 0 ||
                strcmp(t_for, "const") == 0 || strcmp(t_for, "volatile") == 0 || strcmp(t_for, "restrict") == 0 || strcmp(t_for, "__restrict") == 0 || strcmp(t_for, "__restrict__") == 0 || strcmp(t_for, "register") == 0 ||
                strcmp(t_for, "float") == 0 || strcmp(t_for, "double") == 0 || strcmp(t_for, "struct") == 0 || strcmp(t_for, "union") == 0 ||
                hashmap_has(&p->global_typedefs, t_for)) {
                
                const char *struct_tag = "";
                int base_size = parse_base_type(p);
                if (p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
                    struct_tag = p->last_parsed_struct_tag;
                }
                int is_unsigned = p->last_type_unsigned;
                int is_bool = p->last_type_bool || strcmp(t_for, "_Bool") == 0 || strcmp(t_for, "bool") == 0;
                while (strcmp(peek(p), "*") == 0) {
                    take(p, "*");
                    base_size = p->target_scale;
                    is_unsigned = 1;
                }
                const char *name = take(p, nullptr);
                char local_name[256];
                snprintf(local_name, sizeof(local_name), "%s.local.%d", name, p->local_var_counter++);
                const char *local_name_dup = arena_strdup(p->arena, local_name);
                hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)local_name_dup, 0);
                
                hashmap_put(&p->unsigned_vars, local_name_dup, nullptr, is_unsigned);
                hashmap_put(&p->bool_vars, local_name_dup, nullptr, is_bool);
                hashmap_put(&p->value_sizes, local_name_dup, nullptr, base_size);
                if (struct_tag[0]) {
                    hashmap_put(&p->var_struct_tags, local_name_dup, (void *)struct_tag, 0);
                }

                take(p, "=");
                Node *val = expr(p);
                init_node = create_node(p, "decl", tok_line, tok_col);
                init_node->name = local_name_dup;
                init_node->lhs = val;
                init_node->type_size = base_size;
            } else {
                init_node = assign_stmt(p, 0);
            }
        }
        take(p, ";");

        Node *cond_node = nullptr;
        if (strcmp(peek(p), ";") != 0) {
            cond_node = bool_normalize(p, expr(p));
        }
        take(p, ";");

        Node *step_node = nullptr;
        if (strcmp(peek(p), ")") != 0) {
            if (p->pos + 1 < p->tokens.count &&
                (strcmp(p->tokens.data[p->pos + 1].text, "=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "+=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "-=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "*=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "/=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "%=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "&=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "^=") == 0 ||
                 strcmp(p->tokens.data[p->pos + 1].text, "|=") == 0)) {
                step_node = assign_stmt(p, 0);
            } else {
                step_node = create_node(p, "expr", tok_line, tok_col);
                step_node->lhs = expr(p);
            }
        }
        take(p, ")");

        Node *body_node = stmt(p);

        Node *n = create_node(p, "for", tok_line, tok_col);
        n->lhs = cond_node;
        n->rhs = body_node;
        
        node_array_push(&n->body, init_node);
        node_array_push(&n->body, step_node);

        parser_exit_scope(p);
        return n;
    }
    
    if (strcmp(peek(p), ";") == 0) {
        take(p, ";");
        return create_node(p, "empty", tok_line, tok_col);
    }
    if (strcmp(peek(p), "*") == 0) {
        int pos_check = p->pos;
        int parens = 0;
        int has_eq = 0;
        while (pos_check < p->tokens.count && strcmp(p->tokens.data[pos_check].text, ";") != 0) {
            const char *txt = p->tokens.data[pos_check].text;
            if (strcmp(txt, "(") == 0 || strcmp(txt, "[") == 0) parens++;
            if (strcmp(txt, ")") == 0 || strcmp(txt, "]") == 0) parens--;
            if (strcmp(txt, "=") == 0 && parens == 0) {
                has_eq = 1;
                break;
            }
            pos_check++;
        }
        if (has_eq) {
            Node *lhs_node = expr(p);
            take(p, "=");
            Node *val_node = expr(p);
            take(p, ";");
            Node *node = create_node(p, "store_index", tok_line, tok_col);
            node->lhs = lhs_node;
            node->rhs = val_node;
            return node;
        }
    }
    if (p->pos < p->tokens.count && strcmp(p->tokens.data[p->pos].text, "EOF") != 0 &&
        (is_alpha(p->tokens.data[p->pos].text[0])) &&
        p->pos + 1 < p->tokens.count &&
        (strcmp(p->tokens.data[p->pos + 1].text, "[") == 0 || strcmp(p->tokens.data[p->pos + 1].text, ".") == 0 || strcmp(p->tokens.data[p->pos + 1].text, "->") == 0)) {
        int pos_check = p->pos + 1;
        while (pos_check < p->tokens.count) {
            const char *txt = p->tokens.data[pos_check].text;
            if (strcmp(txt, "[") == 0) {
                int brackets = 0;
                while (pos_check < p->tokens.count) {
                    const char *txt2 = p->tokens.data[pos_check].text;
                    if (strcmp(txt2, "[") == 0) brackets++;
                    if (strcmp(txt2, "]") == 0) brackets--;
                    pos_check++;
                    if (brackets == 0) break;
                }
            } else if (strcmp(txt, ".") == 0 || strcmp(txt, "->") == 0) {
                pos_check += 2;
            } else {
                break;
            }
        }
        if (pos_check < p->tokens.count && strcmp(p->tokens.data[pos_check].text, "=") == 0) {
            Node *lhs_node = expr(p);
            take(p, "=");
            Node *val_node = expr(p);
            take(p, ";");
            Node *node = create_node(p, "store_index", tok_line, tok_col);
            node->lhs = lhs_node;
            node->rhs = val_node;
            return node;
        }
    }
    if (p->pos + 1 < p->tokens.count &&
        (strcmp(p->tokens.data[p->pos + 1].text, "=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "+=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "-=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "*=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "/=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "%=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "&=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "^=") == 0 ||
         strcmp(p->tokens.data[p->pos + 1].text, "|=") == 0)) {
        return assign_stmt(p, 1);
    }
    Node *node = create_node(p, "expr", tok_line, tok_col);
    node->lhs = expr(p);
    take(p, ";");
    return node;
}

static Node *block_stmt(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    take(p, "{");
    Node *n = create_node(p, "block", tok_line, tok_col);
    
    parser_enter_scope(p);

    while (strcmp(peek(p), "}") != 0 && strcmp(peek(p), "EOF") != 0) {
        node_array_push(&n->body, stmt(p));
    }
    take(p, "}");
    
    parser_exit_scope(p);
    return n;
}

static Node *function(ParserState *p, int is_static) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    int ret_is_struct = (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0);
    int ret_base = parse_base_type(p);
    int ret_is_float = p->last_type_float;
    if (!ret_is_struct && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
        ret_is_struct = 1;
    }
    int ret_stars = 0;
    while (strcmp(peek(p), "*") == 0) {
        take(p, "*");
        ret_stars++;
    }
    skip_type_qualifiers(p);
    skip_attribute(p);
    int ret_size = (ret_stars > 0) ? p->target_scale : ret_base;
    /* float/double returns are scalar FP (st0/xmm0/v0), never aggregates, even
       though a double is wider than the 32-bit i386 word. */
    int ret_aggregate_size = (ret_stars == 0 && !ret_is_float && (ret_is_struct || ret_base > p->target_scale)) ? ret_base : 0;

    const char *name = "";
    if (strcmp(peek(p), "(") == 0 &&
        p->pos + 2 < p->tokens.count &&
        strcmp(p->tokens.data[p->pos + 2].text, ")") == 0) {
        take(p, "(");
        name = take(p, nullptr);
        take(p, ")");
    } else {
        name = take(p, nullptr);
    }
    p->current_func_name = name;
    hashmap_init(&p->current_static_locals, 16);

    take(p, "(");
    Node *fn_node = create_node(p, "func", tok_line, tok_col);
    fn_node->name = name;
    fn_node->value = ret_size;
    fn_node->aggregate_size = ret_aggregate_size;
    fn_node->is_float = ret_is_float && ret_stars == 0;
    fn_node->is_static = is_static;

    parser_enter_scope(p);

    if (strcmp(peek(p), "void") == 0 && p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, ")") == 0) {
        take(p, "void");
    } else if (strcmp(peek(p), ")") != 0) {
        while (1) {
            if (strcmp(peek(p), "...") == 0) {
                take(p, "...");
                string_array_push(&fn_node->params, "...");
                int_array_push(&fn_node->param_aggregate_sizes, 0);
                int_array_push(&fn_node->param_floats, 0);
                break;
            }
            int is_unsigned = 0;
            int is_bool = 0;
            const char *struct_tag = "";
            if ((strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) &&
                p->pos + 1 < p->tokens.count &&
                strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
                struct_tag = p->tokens.data[p->pos + 1].text;
            }
            int base_size = parse_base_type(p);
            if (!struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
                struct_tag = p->last_parsed_struct_tag;
            }
            int param_elem_scale = base_size;
            is_unsigned = p->last_type_unsigned;
            is_bool = p->last_type_bool;
            int base_is_float = p->last_type_float;

            int is_param_pointer = 0;
            while (strcmp(peek(p), "*") == 0) {
                param_elem_scale = base_size;
                take(p, "*");
                base_size = p->target_scale;
                is_unsigned = 1;
                is_param_pointer = 1;
            }
            skip_type_qualifiers(p);
            const char *param_name = "";
            int is_func_ptr = 0;
            if (strcmp(peek(p), "(") == 0) {
                take(p, "(");
                while (strcmp(peek(p), "*") == 0) {
                    take(p, "*");
                }
                skip_type_qualifiers(p);
                param_name = take(p, nullptr);
                take(p, ")");
                take(p, "(");
                int depth = 1;
                while (depth > 0 && p->pos < p->tokens.count) {
                    if (strcmp(peek(p), "(") == 0) depth++;
                    else if (strcmp(peek(p), ")") == 0) depth--;
                    take(p, nullptr);
                }
                base_size = p->target_scale;
                param_elem_scale = p->target_scale;
                is_unsigned = 1;
                is_func_ptr = 1;
                is_param_pointer = 1;
            } else {
                param_name = take(p, nullptr);
            }
            
            // Check if parameter is array type
            int is_param_arr = 0;
            if (strcmp(peek(p), "[") == 0) {
                is_param_arr = 1;
                while (strcmp(peek(p), "[") == 0) {
                    take(p, "[");
                    long dim = 0;
                    if (strcmp(peek(p), "]") != 0) {
                        Node *sz = expr(p);
                        dim = eval_const(p, sz);
                    }
                    (void)dim;
                    take(p, "]");
                }
            }

            int type_size = (is_param_arr || is_func_ptr) ? p->target_scale : base_size;
            int is_unsigned_var = (is_param_arr || is_func_ptr) ? 1 : is_unsigned;

            char local_name[256];
            snprintf(local_name, sizeof(local_name), "%s.local.%d", param_name, p->local_var_counter++);
            const char *local_name_dup = arena_strdup(p->arena, local_name);

            int is_param_float = base_is_float && !is_param_pointer && !is_param_arr && !is_func_ptr;
            hashmap_put(&p->scopes[p->scope_count - 1], param_name, (void *)local_name_dup, 0);
            hashmap_put(&p->unsigned_vars, local_name_dup, nullptr, is_unsigned_var);
            hashmap_put(&p->bool_vars, local_name_dup, nullptr, is_bool);
            hashmap_put(&p->float_vars, local_name_dup, nullptr, is_param_float);
            hashmap_put(&p->value_sizes, local_name_dup, nullptr, type_size);
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, local_name_dup);
            const char *local_key_dup = arena_strdup(p->arena, local_key);
            ir_set_local_var_elem_scale(local_key_dup, param_elem_scale);
            ir_set_local_var_is_pointer(local_key_dup, is_param_pointer || is_param_arr || is_func_ptr);
            if (struct_tag[0]) {
                hashmap_put(&p->var_struct_tags, local_name_dup, (void *)struct_tag, 0);
            }

            string_array_push(&fn_node->params, local_name_dup);
            int is_param_aggregate = !is_param_pointer && !is_param_arr && !is_func_ptr && !base_is_float &&
                                     (struct_tag[0] || base_size > p->target_scale);
            int_array_push(&fn_node->param_aggregate_sizes, is_param_aggregate ? base_size : 0);
            int_array_push(&fn_node->param_floats, is_param_float ? type_size : 0);

            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
            } else {
                break;
            }
        }
    }
    take(p, ")");

    skip_attribute(p);

    if (strcmp(peek(p), ";") == 0) {
        take(p, ";");
        hashmap_put(&ir_function_return_aggregate_sizes, fn_node->name, nullptr, fn_node->aggregate_size);
        IntArray *param_sizes = malloc(sizeof(IntArray));
        int_array_init(param_sizes);
        for (int p_i = 0; p_i < fn_node->param_aggregate_sizes.count; ++p_i) {
            int_array_push(param_sizes, fn_node->param_aggregate_sizes.data[p_i]);
        }
        hashmap_put(&ir_function_param_aggregate_sizes, fn_node->name, param_sizes, 0);

        IntArray *param_fl = malloc(sizeof(IntArray));
        int_array_init(param_fl);
        for (int p_i = 0; p_i < fn_node->param_floats.count; ++p_i) {
            int_array_push(param_fl, fn_node->param_floats.data[p_i]);
        }
        hashmap_put(&ir_function_param_floats, fn_node->name, param_fl, 0);
        hashmap_put(&ir_function_return_floats, fn_node->name, nullptr,
                    fn_node->is_float ? (int)fn_node->value : 0);

        int is_vararg = 0;
        if (fn_node->params.count > 0 && strcmp(fn_node->params.data[fn_node->params.count - 1], "...") == 0) {
            is_vararg = 1;
        }
        if (is_vararg) {
            hashmap_put(&ir_function_vararg_fixed_counts, fn_node->name, nullptr, fn_node->params.count - 1);
        }
        parser_exit_scope(p);
        hashmap_free(&p->current_static_locals);
        return nullptr;
    }

    Node *body_stmt = block_stmt(p);
    node_array_push(&fn_node->body, body_stmt);

    int is_vararg = 0;
    if (fn_node->params.count > 0 && strcmp(fn_node->params.data[fn_node->params.count - 1], "...") == 0) {
        is_vararg = 1;
    }
    if (is_vararg) {
        hashmap_put(&ir_function_vararg_fixed_counts, fn_node->name, nullptr, fn_node->params.count - 1);
    }
    parser_exit_scope(p);
    return fn_node;
}

static void global_decl(ParserState *p, int is_static, int is_extern) {
    const char *struct_tag = "";
    if ((strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) &&
        p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
        struct_tag = p->tokens.data[p->pos + 1].text;
    }
    int base_size = parse_base_type(p);
    if (!struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
        struct_tag = p->last_parsed_struct_tag;
    }
    int is_unsigned = p->last_type_unsigned;
    int is_bool = p->last_type_bool;
    int base_const = p->last_type_const;
    int base_volatile = p->last_type_volatile;
    int base_float = p->last_type_float;

    if (strcmp(peek(p), ";") == 0) {
        take(p, ";");
        return;
    }

    while (1) {
        int stars = 0;
        while (strcmp(peek(p), "*") == 0) {
            take(p, "*");
            stars++;
        }
        p->last_type_const = 0;
        p->last_type_volatile = 0;
        skip_type_qualifiers(p);
        int is_const = (stars == 0) ? (base_const || p->last_type_const) : p->last_type_const;
        int is_volatile = (stars == 0) ? (base_volatile || p->last_type_volatile) : p->last_type_volatile;
        int type_size = (stars > 0) ? p->target_scale : base_size;
        int is_unsigned_var = (stars > 0) ? 1 : is_unsigned;
        const char *name = take(p, nullptr);
        skip_attribute_and_asm(p);

        long total_arr_count = 1;
        LongArray arr_dims;
        long_array_init(&arr_dims);
        if (strcmp(peek(p), "[") == 0) {
            while (strcmp(peek(p), "[") == 0) {
                take(p, "[");
                long dim = 0;
                if (strcmp(peek(p), "]") != 0) {
                    Node *sz = expr(p);
                    dim = eval_const(p, sz);
                    total_arr_count *= dim;
                }
                take(p, "]");
                long_array_push(&arr_dims, dim);
            }
            type_size = (int)(type_size * total_arr_count);
        }

        hashmap_put(&p->unsigned_vars, name, nullptr, is_unsigned_var);
        hashmap_put(&p->bool_vars, name, nullptr, is_bool);
        hashmap_put(&p->value_sizes, name, nullptr, type_size);
        /* A const array's elements are read-only, but the array name itself is
           never an assignment target, so only flag non-array const scalars. */
        hashmap_put(&p->const_vars, name, nullptr, is_const && arr_dims.count == 0);
        hashmap_put(&p->volatile_vars, name, nullptr, is_volatile);
        hashmap_put(&p->float_vars, name, nullptr, base_float && stars == 0);
        if (struct_tag[0]) {
            hashmap_put(&p->var_struct_tags, name, (void *)struct_tag, 0);
        }

        int elem_size = (stars > 0) ? p->target_scale : base_size;
        long global_size = (arr_dims.count > 0) ? total_arr_count : 1;
        if (struct_tag[0] && stars == 0) {
            elem_size = 1;
            global_size = type_size;
        }
        int is_storage_array = arr_dims.count > 0 || (struct_tag[0] && stars == 0);
        ir_declare_global(name, is_storage_array, global_size, is_static, is_extern, elem_size, p->target_scale);
        if (struct_tag[0] && stars == 0) {
            ir_mark_global_struct(name);
        }
        if (arr_dims.count > 0) {
            ir_set_global_array_dims(name, arr_dims);
            ir_set_global_array_base_size(name, elem_size);
        }

        if (strcmp(peek(p), "=") == 0) {
            take(p, "=");
            if (strcmp(peek(p), "{") == 0) {
                LongArray init_vals;
                long_array_init(&init_vals);

                int is_string_array_init = (arr_dims.count > 0 &&
                                            p->pos + 1 < p->tokens.count &&
                                            p->tokens.data[p->pos + 1].text[0] == '"' &&
                                            (stars > 0 || base_size == 1));
                if (is_string_array_init) {
                    take(p, "{");
                    long string_count = 0;
                    while (strcmp(peek(p), "}") != 0) {
                        if (peek(p)[0] == '"') {
                            take(p, nullptr);
                            long_array_push(&init_vals, 0);
                            string_count++;
                        } else {
                            Node *val = expr(p);
                            long_array_push(&init_vals, eval_const(p, val));
                        }
                        if (strcmp(peek(p), ",") == 0) {
                            take(p, ",");
                        } else {
                            break;
                        }
                    }
                    take(p, "}");
                    if (arr_dims.data[0] == 0) {
                        arr_dims.data[0] = string_count;
                        if (stars > 0) {
                            global_size = string_count;
                        } else {
                            long stride = 1;
                            for (int d_i = 1; d_i < arr_dims.count; ++d_i) {
                                if (arr_dims.data[d_i] > 0) {
                                    stride *= arr_dims.data[d_i];
                                }
                            }
                            global_size = string_count * stride;
                        }
                    }
                    type_size = (int)(elem_size * global_size);
                    ir_declare_global(name, 1, global_size, is_static, is_extern, elem_size, p->target_scale);
                    ir_set_global_array_dims(name, arr_dims);
                } else {
                    InitElementArray inits;
                    init_element_array_init(&inits);
                    parse_aggregate_init(p, 0, struct_tag, &arr_dims, 0, (stars > 0) ? p->target_scale : base_size, &inits);
                    
                    if (struct_tag[0] && stars == 0) {
                        char *byte_buf = calloc(type_size + 1, 1);
                        for (int k = 0; k < inits.count; ++k) {
                            long val = eval_const(p, inits.data[k].val);
                            for (int b = 0; b < inits.data[k].size; ++b) {
                                if (inits.data[k].offset + b < type_size) {
                                    byte_buf[inits.data[k].offset + b] = (val >> (b * 8)) & 0xff;
                                }
                            }
                        }
                        for (long b = 0; b < type_size; ++b) {
                            long_array_push(&init_vals, byte_buf[b]);
                        }
                        free(byte_buf);
                    } else {
                        for (long offset = 0; offset < type_size; offset += elem_size) {
                            long val = 0;
                            for (int k = 0; k < inits.count; ++k) {
                                if (inits.data[k].offset == offset) {
                                    val = eval_const(p, inits.data[k].val);
                                    break;
                                }
                            }
                            long_array_push(&init_vals, val);
                        }
                    }
                    init_element_array_free(&inits);
                }
                ir_set_global_initializers(name, init_vals);
            } else {
                Node *val_node = expr(p);
                LongArray init_vals;
                long_array_init(&init_vals);
                /* Floating global initializer: store the IEEE-754 bit pattern so
                   the backend emits .long/.quad with the right bits. Supports a
                   plain literal and a negated literal. */
                int fp_init = base_float && stars == 0;
                double fp_d = 0.0;
                int got_fp = 0;
                if (fp_init && strcmp(val_node->op, "fnum") == 0) {
                    fp_d = val_node->fvalue; got_fp = 1;
                } else if (fp_init && strcmp(val_node->op, "unary_-") == 0 &&
                           val_node->lhs && strcmp(val_node->lhs->op, "fnum") == 0) {
                    fp_d = -val_node->lhs->fvalue; got_fp = 1;
                }
                if (got_fp) {
                    long bits;
                    if (base_size == 4) {
                        float f = (float)fp_d;
                        unsigned int u;
                        memcpy(&u, &f, 4);
                        bits = (long)u;
                    } else {
                        memcpy(&bits, &fp_d, 8);
                    }
                    long_array_push(&init_vals, bits);
                } else {
                    long_array_push(&init_vals, eval_const(p, val_node));
                }
                ir_set_global_initializers(name, init_vals);
            }
        }

        long_array_free(&arr_dims);

        if (strcmp(peek(p), ",") == 0) {
            take(p, ",");
        } else {
            break;
        }
    }
    take(p, ";");
}

static void parser_state_cleanup(ParserState *state) {
    hashmap_free(&state->current_static_locals);
    for (int idx = 0; idx < state->scope_count; ++idx) {
        hashmap_free(&state->scopes[idx]);
    }
    free(state->scopes);

    hashmap_free(&state->unsigned_vars);
    hashmap_free(&state->bool_vars);
    hashmap_free(&state->const_vars);
    hashmap_free(&state->volatile_vars);
    hashmap_free(&state->float_vars);
    hashmap_free(&state->value_sizes);
    hashmap_free(&state->var_struct_tags);

    hashmap_free(&state->global_typedefs);
    hashmap_free(&state->global_typedef_sizes);
    hashmap_free(&state->global_typedef_struct_tags);
    hashmap_free(&state->global_enums);
    hashmap_free(&state->constexpr_vars);
    
    for (int b = 0; b < state->global_structs.bucket_count; ++b) {
        HashMapEntry *curr = state->global_structs.buckets[b];
        while (curr) {
            HashMap *fields = (HashMap *)curr->val_ptr;
            hashmap_free(fields);
            free(fields);
            curr = curr->next;
        }
    }
    hashmap_free(&state->global_structs);

    hashmap_free(&state->global_field_offsets);
    hashmap_free(&state->global_field_sizes);
    hashmap_free(&state->global_struct_sizes);
    hashmap_free(&state->global_struct_alignments);
    hashmap_free(&state->global_struct_field_tags);
    hashmap_free(&state->global_struct_field_offsets_by_tag);
    hashmap_free(&state->global_struct_field_sizes_by_tag);
    hashmap_free(&state->global_struct_field_elem_sizes_by_tag);
    hashmap_free(&state->global_struct_field_total_sizes_by_tag);
    hashmap_free(&state->global_struct_field_bit_offsets_by_tag);
    hashmap_free(&state->global_struct_field_bit_widths_by_tag);

    for (int b = 0; b < state->global_struct_field_dims_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state->global_struct_field_dims_by_tag.buckets[b];
        while (curr) {
            LongArray *arr = (LongArray *)curr->val_ptr;
            long_array_free(arr);
            free(arr);
            curr = curr->next;
        }
    }
    hashmap_free(&state->global_struct_field_dims_by_tag);
}

NodeArray parser_parse(const TokenArray *tokens, int target_scale, Arena *arena) {
    ParserState state;
    state.tokens.data = tokens->data;
    state.tokens.count = tokens->count;
    state.tokens.capacity = tokens->capacity;
    state.pos = 0;
    state.target_scale = target_scale;
    state.current_func_name = "";
    hashmap_init(&state.current_static_locals, 16);
    
    state.scopes = nullptr;
    state.scope_count = 0;
    state.scope_capacity = 0;

    hashmap_init(&state.unsigned_vars, 64);
    hashmap_init(&state.bool_vars, 64);
    hashmap_init(&state.const_vars, 64);
    hashmap_init(&state.volatile_vars, 64);
    hashmap_init(&state.float_vars, 64);
    hashmap_init(&state.value_sizes, 64);
    hashmap_init(&state.var_struct_tags, 64);
    state.local_var_counter = 0;
    state.last_type_unsigned = 0;
    state.last_type_bool = 0;

    hashmap_init(&state.global_typedefs, 64);
    hashmap_init(&state.global_typedef_sizes, 64);
    hashmap_init(&state.global_typedef_struct_tags, 64);
    hashmap_init(&state.global_enums, 64);
    hashmap_init(&state.constexpr_vars, 64);
    hashmap_init(&state.global_structs, 64);
    hashmap_init(&state.global_field_offsets, 64);
    hashmap_init(&state.global_field_sizes, 64);
    hashmap_init(&state.global_struct_sizes, 64);
    hashmap_init(&state.global_struct_alignments, 64);
    hashmap_init(&state.global_struct_field_tags, 64);
    hashmap_init(&state.global_struct_field_offsets_by_tag, 64);
    hashmap_init(&state.global_struct_field_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_elem_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_total_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_dims_by_tag, 64);
    hashmap_init(&state.global_struct_field_bit_offsets_by_tag, 64);
    hashmap_init(&state.global_struct_field_bit_widths_by_tag, 64);

    state.last_parsed_struct_tag = "";
    state.arena = arena;

    NodeArray funcs;
    node_array_init(&funcs);

    while (strcmp(peek(&state), "EOF") != 0) {
        int is_static = 0;
        int is_extern = 0;
        int is_constexpr = 0;
        while (strcmp(peek(&state), "static") == 0 || strcmp(peek(&state), "extern") == 0 || strcmp(peek(&state), "inline") == 0 || strcmp(peek(&state), "__inline") == 0 || strcmp(peek(&state), "__inline__") == 0 || strcmp(peek(&state), "_Noreturn") == 0 || strcmp(peek(&state), "noreturn") == 0 || strcmp(peek(&state), "constexpr") == 0 ||
               strcmp(peek(&state), "__attribute__") == 0 || strcmp(peek(&state), "__attribute") == 0 ||
               (strcmp(peek(&state), "[") == 0 && state.pos + 1 < state.tokens.count && strcmp(state.tokens.data[state.pos + 1].text, "[") == 0)) {
            if (strcmp(peek(&state), "static") == 0) {
                take(&state, "static");
                is_static = 1;
            } else if (strcmp(peek(&state), "extern") == 0) {
                take(&state, "extern");
                is_extern = 1;
            } else if (strcmp(peek(&state), "constexpr") == 0) {
                take(&state, "constexpr");
                is_constexpr = 1;
            } else if (strcmp(peek(&state), "__attribute__") == 0 || strcmp(peek(&state), "__attribute") == 0 || strcmp(peek(&state), "[") == 0) {
                skip_attribute(&state);
            } else {
                take(&state, nullptr);
            }
        }

        if (is_constexpr) {
            int base_size = parse_base_type(&state);
            int is_unsigned = state.last_type_unsigned;
            int is_bool = state.last_type_bool;
            int stars = 0;
            while (strcmp(peek(&state), "*") == 0) {
                take(&state, "*");
                stars++;
            }
            const char *name = take(&state, nullptr);
            take(&state, "=");
            Node *init_expr = expr(&state);
            take(&state, ";");
            long val = eval_const(&state, init_expr);
            
            const char *name_dup = arena_strdup(state.arena, name);
            hashmap_put(&state.unsigned_vars, name_dup, nullptr, is_unsigned);
            hashmap_put(&state.bool_vars, name_dup, nullptr, is_bool);
            hashmap_put(&state.value_sizes, name_dup, nullptr, (stars > 0) ? state.target_scale : base_size);
            hashmap_put(&state.constexpr_vars, name_dup, nullptr, val);
        } else if (strcmp(peek(&state), "_Static_assert") == 0 || strcmp(peek(&state), "static_assert") == 0) {
            parse_static_assert(&state);
        } else if (strcmp(peek(&state), "typedef") == 0) {
            take(&state, "typedef");
            int typedef_base_size = parse_base_type(&state);
            const char *typedef_struct_tag = state.last_parsed_struct_tag;
            int typedef_stars = 0;
            while (strcmp(peek(&state), "*") == 0) {
                take(&state, "*");
                typedef_stars++;
            }
            skip_attribute(&state);
            const char *alias = "";
            int alias_size = (typedef_stars > 0) ? state.target_scale : typedef_base_size;
            const char *alias_struct_tag = (typedef_stars == 0) ? typedef_struct_tag : "";
            if (strcmp(peek(&state), "(") == 0 &&
                state.pos + 1 < state.tokens.count &&
                strcmp(state.tokens.data[state.pos + 1].text, "*") == 0) {
                take(&state, "(");
                take(&state, "*");
                skip_type_qualifiers(&state);
                alias = take(&state, nullptr);
                take(&state, ")");
                if (strcmp(peek(&state), "(") == 0) {
                    int depth = 0;
                    int keep_scanning = 1;
                    while (keep_scanning && state.pos < state.tokens.count) {
                        if (strcmp(peek(&state), "(") == 0) depth++;
                        else if (strcmp(peek(&state), ")") == 0) depth--;
                        take(&state, nullptr);
                        if (depth <= 0) {
                            keep_scanning = 0;
                        }
                    }
                }
                alias_size = state.target_scale;
                alias_struct_tag = "";
            } else {
                alias = take(&state, nullptr);
                while (strcmp(peek(&state), "[") == 0) {
                    take(&state, "[");
                    if (strcmp(peek(&state), "]") != 0) {
                        Node *dim_expr = expr(&state);
                        long dim = eval_const(&state, dim_expr);
                        if (dim > 0) {
                            alias_size = (int)(alias_size * dim);
                        }
                    }
                    take(&state, "]");
                }
            }
            take(&state, ";");
            hashmap_put(&state.global_typedefs, alias, nullptr, 1);
            hashmap_put(&state.global_typedef_sizes, alias, nullptr, alias_size);
            if (alias_struct_tag && alias_struct_tag[0]) {
                hashmap_put(&state.global_typedef_struct_tags, alias, (void *)alias_struct_tag, 0);
            }
        } else if (is_function_decl(&state)) {
            Node *fn = function(&state, is_static);
            if (fn) {
                node_array_push(&funcs, fn);
            }
        } else {
            global_decl(&state, is_static, is_extern);
        }
    }
    take(&state, "EOF");

    // Copy global struct/variable type information so that IR can access it.
    // In b1cc, parser state type properties (unsigned_vars, bool_vars, value_sizes, var_struct_tags)
    // are queried during IR lowering. So we populate the IR global state!
    for (int b = 0; b < state.unsigned_vars.bucket_count; ++b) {
        HashMapEntry *curr = state.unsigned_vars.buckets[b];
        while (curr) {
            ir_set_var_unsigned(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.bool_vars.bucket_count; ++b) {
        HashMapEntry *curr = state.bool_vars.buckets[b];
        while (curr) {
            ir_set_var_bool(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.value_sizes.bucket_count; ++b) {
        HashMapEntry *curr = state.value_sizes.buckets[b];
        while (curr) {
            ir_set_var_type_size(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.float_vars.bucket_count; ++b) {
        HashMapEntry *curr = state.float_vars.buckets[b];
        while (curr) {
            ir_set_var_float(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.var_struct_tags.bucket_count; ++b) {
        HashMapEntry *curr = state.var_struct_tags.buckets[b];
        while (curr) {
            ir_set_var_struct_tag(curr->key, (const char *)curr->val_ptr);
            curr = curr->next;
        }
    }

    // copy struct offsets/sizes for IR
    for (int b = 0; b < state.global_struct_field_offsets_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_offsets_by_tag.buckets[b];
        while (curr) {
            ir_set_struct_field_offset(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_sizes_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_sizes_by_tag.buckets[b];
        while (curr) {
            ir_set_struct_field_size(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_total_sizes_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_total_sizes_by_tag.buckets[b];
        while (curr) {
            ir_set_struct_field_total_size(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_dims_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_dims_by_tag.buckets[b];
        while (curr) {
            LongArray *dims = (LongArray *)curr->val_ptr;
            ir_set_struct_field_dims(curr->key, *dims);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_tags.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_tags.buckets[b];
        while (curr) {
            ir_set_struct_field_tag(curr->key, (const char *)curr->val_ptr);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_bit_offsets_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_bit_offsets_by_tag.buckets[b];
        while (curr) {
            ir_set_struct_field_bit_offset(curr->key, curr->val_int);
            curr = curr->next;
        }
    }
    for (int b = 0; b < state.global_struct_field_bit_widths_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = state.global_struct_field_bit_widths_by_tag.buckets[b];
        while (curr) {
            ir_set_struct_field_bit_width(curr->key, curr->val_int);
            curr = curr->next;
        }
    }

    parser_state_cleanup(&state);
    return funcs;
}

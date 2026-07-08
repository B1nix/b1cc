#include "parser.h"
#include "diagnostics.h"
#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *expr(ParserState *p);
static Node *comma_expr(ParserState *p);
static Node *block_stmt(ParserState *p);
static Node *stmt(ParserState *p);
static int is_assignment_operator(const char *t);
static void note_type_qualifier(ParserState *p, const char *tok);
static void parser_error(const ParserState *p, const char *msg);
static int paren_starts_type_name(ParserState *p);
static int parse_base_type(ParserState *p);

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
    node->op_enum = str_to_op(op);
    node->name = "";
    node->value = 0;
    node->is_static = 0;
    node->lhs = nullptr;
    node->rhs = nullptr;
    node_array_init(&node->body);
    string_array_init(&node->params);
    int_array_init(&node->param_aggregate_sizes);
    int_array_init(&node->param_aggregate_float_classes);
    int_array_init(&node->param_floats);
    node->aggregate_size = 0;
    node->aggregate_float_class = 0;
    node->type_tag = "";
    long_array_init(&node->array_dims);
    node->elem_size = 0;
    node->pointee_size = 0;
    node->pointee_unsigned = 0;
    node->pointee_unsigned_known = 0;
    node->is_unsigned = 0;
    node->compare_unsigned = 0;
    node->type_size = 8;
    node->is_bool = 0;
    node->is_float = 0;
    node->fvalue = 0.0;
    node->bit_offset = 0;
    node->bit_width = 0;
    node->alignment = 0;
    node->vla_dim_expr = nullptr;
    node->line = line;
    node->col = col;
    return node;
}

static void parser_set_struct_float_aggregate_class(ParserState *p, const char *tag, int hfa_valid, int hfa_count, int hfa_elem_size) {
    if (!tag || !tag[0]) return;
    int hfa_class = (hfa_valid && hfa_count > 0) ? ((hfa_count << 8) | hfa_elem_size) : 0;
    hashmap_put(&p->global_struct_float_aggregate_classes, tag, nullptr, hfa_class);
    ir_set_struct_float_aggregate_class(tag, hfa_class);
}

static const char *infer_struct_tag(const ParserState *p, const Node *node) {
    if (!node) return "";
    if (node->op_enum == OP_VAR) {
        HashMapEntry *entry = hashmap_get((HashMap *)&p->var_struct_tags, node->name);
        return entry ? (const char *)entry->val_ptr : "";
    }
    if (node->op_enum == OP_INDEX && node->type_tag && node->type_tag[0]) {
        return node->type_tag;
    }
    if (node->op_enum == OP_UNARY_DEREF && node->lhs) {
        return infer_struct_tag(p, node->lhs);
    }
    return "";
}

static long eval_const(ParserState *p, const Node *node);
static long sizeof_expr(const ParserState *p, const Node *node);

static int const_expr_has_runtime_var(ParserState *p, const Node *node) {
    if (!node) return 0;
    if (node->op_enum == OP_VAR) {
        return !hashmap_has(&p->global_enums, node->name) && !hashmap_has(&p->constexpr_vars, node->name);
    }
    if (const_expr_has_runtime_var(p, node->lhs) || const_expr_has_runtime_var(p, node->rhs)) return 1;
    for (int i = 0; i < node->body.count; ++i) {
        if (const_expr_has_runtime_var(p, node->body.data[i])) return 1;
    }
    return 0;
}

static long eval_const(ParserState *p, const Node *node) {
    if (node->op_enum == OP_NUM) return node->value;
    if (node->op_enum == OP_CHAR) return node->value;
    if (node->op_enum == OP_CAST) return eval_const(p, node->lhs);
    if (node->op_enum == OP_UNARY_MINUS) return -eval_const(p, node->lhs);
    if (node->op_enum == OP_UNARY_TILDE) return ~eval_const(p, node->lhs);
    if (node->op_enum == OP_UNARY_NOT) return !eval_const(p, node->lhs);
    if (node->op_enum == OP_ADD) return eval_const(p, node->lhs) + eval_const(p, node->rhs);
    if (node->op_enum == OP_SUB) {
        if (node->rhs) return eval_const(p, node->lhs) - eval_const(p, node->rhs);
        return -eval_const(p, node->lhs);
    }
    if (node->op_enum == OP_MUL) return eval_const(p, node->lhs) * eval_const(p, node->rhs);
    if (node->op_enum == OP_DIV) {
        long divisor = eval_const(p, node->rhs);
        if (divisor == 0) parser_error(p, "division by zero in constant expression");
        return eval_const(p, node->lhs) / divisor;
    }
    if (node->op_enum == OP_EQ) return eval_const(p, node->lhs) == eval_const(p, node->rhs);
    if (node->op_enum == OP_NE) return eval_const(p, node->lhs) != eval_const(p, node->rhs);
    if (node->op_enum == OP_LT) return eval_const(p, node->lhs) < eval_const(p, node->rhs);
    if (node->op_enum == OP_GT) return eval_const(p, node->lhs) > eval_const(p, node->rhs);
    if (node->op_enum == OP_LE) return eval_const(p, node->lhs) <= eval_const(p, node->rhs);
    if (node->op_enum == OP_GE) return eval_const(p, node->lhs) >= eval_const(p, node->rhs);
    if (node->op_enum == OP_LAND) return eval_const(p, node->lhs) && eval_const(p, node->rhs);
    if (node->op_enum == OP_LOR) return eval_const(p, node->lhs) || eval_const(p, node->rhs);
    if (node->op_enum == OP_BAND) return eval_const(p, node->lhs) & eval_const(p, node->rhs);
    if (node->op_enum == OP_BOR) return eval_const(p, node->lhs) | eval_const(p, node->rhs);
    if (node->op_enum == OP_BXOR) return eval_const(p, node->lhs) ^ eval_const(p, node->rhs);
    if (node->op_enum == OP_SHL) return eval_const(p, node->lhs) << eval_const(p, node->rhs);
    if (node->op_enum == OP_SHR) return eval_const(p, node->lhs) >> eval_const(p, node->rhs);
    if (node->op_enum == OP_TERNARY) {
        long cond = eval_const(p, node->lhs);
        if (node->body.count < 2) parser_error(p, "invalid conditional constant expression");
        return cond ? eval_const(p, node->body.data[0]) : eval_const(p, node->body.data[1]);
    }
    if (node->op_enum == OP_SIZEOF) {
        return sizeof_expr(p, node->lhs);
    }
    if (node->op_enum == OP_VAR) {
        if (strcmp(node->name, ";") == 0 || strcmp(node->name, "}") == 0) {
            return 0;
        }
        HashMapEntry *entry = hashmap_get(&p->global_enums, node->name);
        if (entry) return entry->val_int;
        parser_error(p, "non-constant variable in constant expression");
    }
    parser_error(p, "invalid constant expression");
    return 0;
}

static int node_address_ref(ParserState *p, const Node *node, const char **base, long *offset) {
    if (!node) return 0;
    if (node->op_enum == OP_CAST) {
        return node_address_ref(p, node->lhs, base, offset);
    }
    if (node->op_enum == OP_UNARY_ADDR) {
        return node_address_ref(p, node->lhs, base, offset);
    }
    if (node->op_enum == OP_VAR) {
        *base = node->name;
        *offset = 0;
        return 1;
    }
    if (node->op_enum == OP_INDEX) {
        const char *inner_base = nullptr;
        long inner_offset = 0;
        if (!node_address_ref(p, node->lhs, &inner_base, &inner_offset)) return 0;
        long idx = eval_const(p, node->rhs);
        long scale = 1;
        if (strcmp(node->name, "byte_offset") == 0) {
            scale = 1;
        } else if (node->elem_size > 0) {
            scale = node->elem_size;
        } else if (node->type_size > 0) {
            scale = node->type_size;
        } else if (node->lhs && node->lhs->op_enum == OP_VAR) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", node->lhs->name);
            int base_size = ir_get_local_array_base_size(local_key);
            if (base_size == 0) base_size = ir_get_global_array_base_size(node->lhs->name);
            if (base_size > 0) scale = base_size;
        }
        *base = inner_base;
        *offset = inner_offset + idx * scale;
        return 1;
    }
    return 0;
}

static int format_address_ref(ParserState *p, const Node *node, char *buf, size_t buf_size) {
    const char *base = nullptr;
    long offset = 0;
    if (!node_address_ref(p, node, &base, &offset) || !base || !base[0]) return 0;
    if (offset == 0) {
        snprintf(buf, buf_size, "%s", base);
    } else if (offset > 0) {
        snprintf(buf, buf_size, "%s+%ld", base, offset);
    } else {
        snprintf(buf, buf_size, "%s%ld", base, offset);
    }
    return 1;
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
    if (node->op_enum == OP_NUM) {
        *type_size = node->type_size ? node->type_size : 4;
        *is_unsigned = node->is_unsigned;
        return;
    }
    if (node->op_enum == OP_STR) {
        *type_size = p->target_scale;
        *is_unsigned = 1;
        *is_pointer = 1;
        *elem_size = 1;
        return;
    }
    if (node->op_enum == OP_VAR) {
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
    if (node->op_enum == OP_CAST) {
        *type_size = node->type_size;
        *is_unsigned = node->is_unsigned;
        return;
    }
    if (node->op_enum == OP_UNARY_DEREF) {
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
    if (node->op_enum == OP_INDEX) {
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
    if (node->op_enum == OP_NUM) return 8;
    if (node->op_enum == OP_CHAR) return 1;
    if (node->op_enum == OP_VAR) {
        HashMapEntry *entry = hashmap_get((HashMap *)&p->value_sizes, node->name);
        if (entry) return entry->val_int;
        return p->target_scale;
    }
    if (node->op_enum == OP_INDEX) {
        if (node->elem_size > 0) return node->elem_size;
        if (node->lhs && node->lhs->op_enum == OP_VAR) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", node->lhs->name);
            int base = ir_get_local_array_base_size(local_key);
            if (base > 0) return base;
            base = ir_get_global_array_base_size(node->lhs->name);
            if (base > 0) return base;
        }
        return p->target_scale;
    }
    if (node->op_enum == OP_CAST) {
        return node->type_size;
    }
    if (node->op_enum == OP_UNARY_DEREF) {
        if (node->lhs && node->lhs->pointee_size > 0) {
            return node->lhs->pointee_size;
        }
        const char *tag = infer_struct_tag(p, node->lhs);
        if (tag && tag[0]) {
            HashMapEntry *entry = hashmap_get((HashMap *)&p->global_struct_sizes, tag);
            if (entry) return entry->val_int;
        }
        const Node *base = node->lhs;
        if (base && base->op_enum == OP_INDEX) {
            base = base->lhs;
        }
        if (base && base->op_enum == OP_VAR) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", base->name);
            int scale = ir_get_local_var_elem_scale(local_key);
            if (scale > 0) return scale;
            scale = ir_get_global_var_elem_scale(base->name);
            if (scale > 0) return scale;
        }
        return p->target_scale;
    }
    if (node->op_enum == OP_MEMBER || node->op_enum == OP_MEMBER_PTR) {
        const char *tag = "";
        if (node->op_enum == OP_MEMBER) {
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

static int is_calling_convention_qualifier(const char *t) {
    if (strcmp(t, "__cdecl") == 0) return 1;
    if (strcmp(t, "__cdecl__") == 0) return 1;
    if (strcmp(t, "__stdcall") == 0) return 1;
    if (strcmp(t, "__stdcall__") == 0) return 1;
    if (strcmp(t, "__fastcall") == 0) return 1;
    if (strcmp(t, "__fastcall__") == 0) return 1;
    if (strcmp(t, "__thiscall") == 0) return 1;
    if (strcmp(t, "__thiscall__") == 0) return 1;
    if (strcmp(t, "__vectorcall") == 0) return 1;
    if (strcmp(t, "__vectorcall__") == 0) return 1;
    return 0;
}

static void skip_calling_convention_qualifiers(ParserState *p) {
    while (is_calling_convention_qualifier(peek(p))) {
        take(p, nullptr);
    }
}

static const char *decode_asm_string(ParserState *p, const char *tok) {
    size_t len = strlen(tok);
    if (len < 2 || tok[0] != '"' || tok[len - 1] != '"') {
        return "";
    }
    StringBuilder sb;
    sb_init(&sb);
    for (size_t i = 1; i + 1 < len; ++i) {
        if (tok[i] == '\\' && i + 1 < len - 1) {
            i++;
            if (tok[i] == 'n') sb_append_char(&sb, '\n');
            else if (tok[i] == 't') sb_append_char(&sb, '\t');
            else sb_append_char(&sb, tok[i]);
        } else {
            sb_append_char(&sb, tok[i]);
        }
    }
    const char *res = sb_to_string(&sb, p->arena);
    sb_free(&sb);
    return res;
}

static void append_string_literal_bytes(ParserState *p, LongArray *out, const char *tok) {
    size_t len = strlen(tok);
    if (len < 2 || tok[0] != '"' || tok[len - 1] != '"') {
        return;
    }
    int start_count = out->count;
    int saw_escape = 0;
    const char *cur = tok + 1;
    while (*cur != '"') {
        long ch;
        if (*cur == '\\' && cur[1] != '"') {
            saw_escape = 1;
            cur++;
            if (*cur == 'n') ch = '\n';
            else if (*cur == 't') ch = '\t';
            else if (*cur == 'r') ch = '\r';
            else if (*cur == '0') ch = '\0';
            else ch = *cur;
        } else {
            ch = *cur;
        }
        long_array_push(out, ch);
        cur++;
    }
    if (!saw_escape) {
        int raw_count = (int)len - 2;
        if (out->count - start_count == raw_count - 1 && raw_count > 0) {
            long_array_push(out, tok[len - 2]);
        }
    }
    (void)p;
}

static const char *parse_gnu_asm(ParserState *p, int require_semicolon) {
    take(p, nullptr);
    if (strcmp(peek(p), "volatile") == 0 || strcmp(peek(p), "__volatile__") == 0) {
        take(p, nullptr);
    }
    take(p, "(");
    StringBuilder asm_text;
    sb_init(&asm_text);
    int saw_template = 0;
    while (peek(p)[0] == '"') {
        if (saw_template) {
            sb_append_char(&asm_text, '\n');
        }
        sb_append(&asm_text, decode_asm_string(p, take(p, nullptr)));
        saw_template = 1;
    }
    int parens = 1;
    int has_operands = 0;
    while (parens > 0 && strcmp(peek(p), "EOF") != 0) {
        const char *t = take(p, nullptr);
        if (strcmp(t, "(") == 0) parens++;
        if (parens == 1 && strcmp(t, ":") == 0) has_operands = 1;
        if (strcmp(t, ")") == 0) parens--;
    }
    if (require_semicolon) {
        take(p, ";");
    }
    const char *res = has_operands ? "" : sb_to_string(&asm_text, p->arena);
    sb_free(&asm_text);
    return res;
}

static void skip_attribute_and_asm(ParserState *p) {
    skip_attribute(p);
    skip_calling_convention_qualifiers(p);
    if (strcmp(peek(p), "__asm__") == 0 || strcmp(peek(p), "__asm") == 0 || strcmp(peek(p), "asm") == 0) {
        (void)parse_gnu_asm(p, 0);
    }
}

static void note_type_qualifier(ParserState *p, const char *t) {
    if (strcmp(t, "const") == 0) p->last_type_const = 1;
    else if (strcmp(t, "volatile") == 0) p->last_type_volatile = 1;
}

static void skip_type_qualifiers(ParserState *p) {
    while (strcmp(peek(p), "const") == 0 || strcmp(peek(p), "volatile") == 0 ||
           strcmp(peek(p), "restrict") == 0 || strcmp(peek(p), "__restrict") == 0 ||
           strcmp(peek(p), "__restrict__") == 0 || strcmp(peek(p), "_Thread_local") == 0 ||
           strcmp(peek(p), "thread_local") == 0 || strcmp(peek(p), "_Atomic") == 0) {
        note_type_qualifier(p, peek(p));
        take(p, nullptr);
    }
    skip_calling_convention_qualifiers(p);
}

static void take_star(ParserState *p) {
    take(p, "*");
    skip_type_qualifiers(p);
}

static int parse_base_type(ParserState *p);

static void parse_function_pointer_param_floats(ParserState *p, IntArray *out) {
    int_array_init(out);
    take(p, "(");
    if (strcmp(peek(p), "void") == 0 &&
        p->pos + 1 < p->tokens.count &&
        strcmp(p->tokens.data[p->pos + 1].text, ")") == 0) {
        take(p, "void");
        take(p, ")");
        return;
    }
    if (strcmp(peek(p), ")") != 0) {
        while (1) {
            if (strcmp(peek(p), "...") == 0) {
                take(p, "...");
                int_array_push(out, 0);
                break;
            }
            int base_size = parse_base_type(p);
            int base_is_float = p->last_type_float;
            int stars = 0;
            while (strcmp(peek(p), "*") == 0) {
                take_star(p);
                stars++;
            }
            skip_type_qualifiers(p);
            if (strcmp(peek(p), "(") == 0 &&
                p->pos + 1 < p->tokens.count &&
                strcmp(p->tokens.data[p->pos + 1].text, "*") == 0) {
                int depth = 0;
                do {
                    if (strcmp(peek(p), "(") == 0) depth++;
                    else if (strcmp(peek(p), ")") == 0) depth--;
                    take(p, nullptr);
                } while (depth > 0 && p->pos < p->tokens.count);
                if (strcmp(peek(p), "(") == 0) {
                    IntArray nested;
                    parse_function_pointer_param_floats(p, &nested);
                    int_array_free(&nested);
                }
                int_array_push(out, 0);
            } else {
                if (strcmp(peek(p), ",") != 0 && strcmp(peek(p), ")") != 0) {
                    take(p, nullptr);
                }
                while (strcmp(peek(p), "[") == 0) {
                    take(p, "[");
                    while (strcmp(peek(p), "]") != 0 && strcmp(peek(p), "EOF") != 0) {
                        take(p, nullptr);
                    }
                    take(p, "]");
                    stars = 1;
                }
                int_array_push(out, (base_is_float && stars == 0) ? base_size : 0);
            }
            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
                if (strcmp(peek(p), "}") == 0) {
                    break;
                }
            } else {
                break;
            }
        }
    }
    take(p, ")");
}

static int is_type_qualifier_token(const char *t) {
    return strcmp(t, "const") == 0 || strcmp(t, "volatile") == 0 ||
           strcmp(t, "restrict") == 0 || strcmp(t, "__restrict") == 0 ||
           strcmp(t, "__restrict__") == 0 || strcmp(t, "register") == 0 ||
           strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0 ||
           strcmp(t, "_Atomic") == 0 ||
           strcmp(t, "__attribute__") == 0 || strcmp(t, "__attribute") == 0;
}

static void parse_qualifier_token(ParserState *p, int *is_complex) {
    const char *t = peek(p);
    if (strcmp(t, "__attribute__") == 0 || strcmp(t, "__attribute") == 0) {
        skip_attribute(p);
    } else if (strcmp(t, "_Alignas") == 0 || strcmp(t, "alignas") == 0) {
        take(p, nullptr);
        int align_val = 0;
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
                take_star(p);
                stars++;
                base_size = p->target_scale;
            }
            take(p, ")");
            align_val = alignof_type(p, base_size, s_tag, stars);
        } else {
            take(p, "(");
            Node *expr_node = expr(p);
            take(p, ")");
            align_val = (int)eval_const(p, expr_node);
        }
        if (align_val > p->last_type_alignment) {
            p->last_type_alignment = align_val;
        }
    } else if (strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0) {
        p->last_type_thread_local = 1;
        take(p, nullptr);
    } else if (strcmp(t, "_Complex") == 0 || strcmp(t, "complex") == 0 || strcmp(t, "__complex__") == 0 || strcmp(t, "_Imaginary") == 0 || strcmp(t, "imaginary") == 0) {
        *is_complex = 1;
        take(p, nullptr);
    } else {
        note_type_qualifier(p, t);
        take(p, nullptr);
    }
}

static int parse_base_type(ParserState *p) {
    p->last_parsed_struct_tag = "";
    p->last_parsed_typedef = "";
    skip_attribute(p);
    int base_size = p->target_scale;
    p->last_type_unsigned = 0;
    p->last_type_bool = 0;
    p->last_type_const = 0;
    p->last_type_volatile = 0;
    p->last_type_float = 0;
    p->last_type_alignment = 0;
    p->last_type_thread_local = 0;
    int is_complex = 0;

    while (is_type_qualifier_token(peek(p))) {
        parse_qualifier_token(p, &is_complex);
    }
    if (strcmp(peek(p), "unsigned") == 0 || strcmp(peek(p), "signed") == 0) {
        p->last_type_unsigned = (strcmp(peek(p), "unsigned") == 0);
        take(p, nullptr);
        while (is_type_qualifier_token(peek(p))) {
            parse_qualifier_token(p, &is_complex);
        }
        if (strcmp(peek(p), "struct") != 0 && strcmp(peek(p), "enum") != 0 &&
            (strcmp(peek(p), "char") == 0 || strcmp(peek(p), "int") == 0 || strcmp(peek(p), "long") == 0 || strcmp(peek(p), "short") == 0 || strcmp(peek(p), "void") == 0 || strcmp(peek(p), "_Bool") == 0 || strcmp(peek(p), "bool") == 0 ||
             strcmp(peek(p), "float") == 0 || strcmp(peek(p), "double") == 0 || hashmap_has(&p->global_typedefs, peek(p)))) {
            // continue
        } else {
            if (is_complex) base_size *= 2;
            return base_size;
        }
    }
    while (is_type_qualifier_token(peek(p))) {
        parse_qualifier_token(p, &is_complex);
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
                static int anon_counter = 0;
                char anon_tag[64];
                int anon_id = anon_counter++;
                snprintf(anon_tag, sizeof(anon_tag), "anon.struct.%d", anon_id);
                tag = arena_strdup(p->arena, anon_tag);
            }
            take(p, "{");
            int offset = 0;
            int union_max_size = 0;
            int struct_alignment = 1;
            int bit_offset = 0;
            int current_unit_size = 0;
            int hfa_valid = !is_union;
            int hfa_elem_size = 0;
            int hfa_count = 0;
            int has_flexible_array = 0;
            HashMap *fields = malloc(sizeof(HashMap));
            hashmap_init(fields, 16);
            while (strcmp(peek(p), "}") != 0) {
                int field_base_size = parse_base_type(p);
                int field_base_is_unsigned = p->last_type_unsigned;
                int field_base_is_float = p->last_type_float;
                const char *f_tag = p->last_parsed_struct_tag;
                while (1) {
                    int stars = 0;
                    while (strcmp(peek(p), "*") == 0) {
                        take_star(p);
                        stars++;
                    }
                    int is_func_ptr_field = 0;
                    const char *func_ptr_field_name = "";
                    if (strcmp(peek(p), "(") == 0 &&
                        p->pos + 1 < p->tokens.count &&
                        strcmp(p->tokens.data[p->pos + 1].text, "*") == 0) {
                        take(p, "(");
                        take_star(p);
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
                    
                    int is_anon = !is_func_ptr_field && (strcmp(peek(p), ";") == 0 || strcmp(peek(p), ",") == 0);
                    if (is_anon && f_tag[0]) {
                        hfa_valid = 0;
                        // Anonymous struct/union flattening!
                        HashMapEntry *f_entry = hashmap_get(&p->global_structs, f_tag);
                        if (f_entry) {
                            HashMap *sub_fields = (HashMap *)f_entry->val_ptr;
                            for (int b = 0; b < sub_fields->bucket_count; ++b) {
                                HashMapEntry *curr = &sub_fields->entries[b];
                                if (curr->key && curr->key != TOMBSTONE) {
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
                                    HashMapEntry *sub_unsigned = hashmap_get(&p->global_struct_field_unsigned_by_tag, f_key);
                                    int sub_unsigned_val = sub_unsigned ? sub_unsigned->val_int : 0;
                                    HashMapEntry *sub_dims_entry = hashmap_get(&p->global_struct_field_dims_by_tag, f_key);
                                    LongArray *sub_dims = sub_dims_entry ? (LongArray *)sub_dims_entry->val_ptr : nullptr;
                                    if (!sub_dims) {
                                        sub_dims = ir_get_struct_field_dims(f_key);
                                    }
                                    
                                    hashmap_put(&p->global_field_sizes, sub_field_name, nullptr, sub_size);
                                    
                                    if (tag[0]) {
                                        char p_key[512];
                                        snprintf(p_key, sizeof(p_key), "%s.%s", tag, sub_field_name);
                                        const char *p_key_dup = arena_strdup(p->arena, p_key);
                                        
                                        hashmap_put(&p->global_struct_field_offsets_by_tag, p_key_dup, nullptr, offset + sub_offset);
                                        hashmap_put(&p->global_struct_field_sizes_by_tag, p_key_dup, nullptr, sub_size);
                                        hashmap_put(&p->global_struct_field_total_sizes_by_tag, p_key_dup, nullptr, sub_total);
                                        hashmap_put(&p->global_struct_field_unsigned_by_tag, p_key_dup, nullptr, sub_unsigned_val);
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
                        hfa_valid = 0;
                        take(p, ":");
                        Node *bf_expr = expr(p);
                        bit_width = (int)eval_const(p, bf_expr);
                    }

                    long arr_count = 1;
                    LongArray *field_dims = malloc(sizeof(LongArray));
                    long_array_init(field_dims);
                    if (!is_bitfield && strcmp(peek(p), "[") == 0) {
                        hfa_valid = 0;
                        while (strcmp(peek(p), "[") == 0) {
                            take(p, "[");
                            long dim = 0;
                            if (strcmp(peek(p), "]") != 0) {
                                Node *sz_expr = expr(p);
                                dim = eval_const(p, sz_expr);
                                arr_count *= dim;
                            } else {
                                has_flexible_array = 1;
                                arr_count = 0;
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
                            hashmap_put(&p->global_struct_field_unsigned_by_tag, key_dup, nullptr, (stars == 0) ? field_base_is_unsigned : 0);
                        }
                        hashmap_put(fields, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_offsets, field_name, nullptr, offset);
                        hashmap_put(&p->global_field_sizes, field_name, nullptr, field_size);

                        bit_offset += w;
                        if (is_union && current_unit_size > union_max_size) {
                            union_max_size = current_unit_size;
                        }
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
                            hfa_valid = 0;
                            char key[256];
                            snprintf(key, sizeof(key), "%s.%s", tag, field_name);
                            hashmap_put(&p->global_struct_field_tags, arena_strdup(p->arena, key), (void *)f_tag, 0);
                        }
                        if (stars > 0 || !field_base_is_float) {
                            hfa_valid = 0;
                        } else {
                            if (hfa_elem_size == 0) {
                                hfa_elem_size = field_size;
                            } else if (hfa_elem_size != field_size) {
                                hfa_valid = 0;
                            }
                            hfa_count++;
                            if (hfa_count > 4) {
                                hfa_valid = 0;
                            }
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
                            if (stars == 0 && field_base_is_float) {
                                hashmap_put(&p->global_struct_field_float_sizes_by_tag, key_dup, nullptr, field_size);
                            }
                            if (stars > 0) {
                                int field_elem_size = (stars > 1) ? p->target_scale : field_base_size;
                                hashmap_put(&p->global_struct_field_elem_sizes_by_tag, key_dup, nullptr, field_elem_size);
                            }
                            hashmap_put(&p->global_struct_field_is_pointer_by_tag, key_dup, nullptr, stars > 0);
                            hashmap_put(&p->global_struct_field_unsigned_by_tag, key_dup, nullptr, (stars == 0) ? field_base_is_unsigned : 0);
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
            skip_attribute(p);
            if (is_union) {
                base_size = union_max_size;
            } else {
                if (bit_offset > 0) {
                    offset += current_unit_size;
                }
                if (!has_flexible_array && offset > 0 && (offset % p->target_scale) != 0) {
                    offset += p->target_scale - (offset % p->target_scale);
                }
                base_size = offset;
            }
            if (tag[0]) {
                hashmap_put(&p->global_structs, tag, fields, 0);
                hashmap_put(&p->global_struct_sizes, tag, nullptr, base_size);
                hashmap_put(&p->global_struct_alignments, tag, nullptr, struct_alignment);
                parser_set_struct_float_aggregate_class(p, tag, hfa_valid, hfa_count, hfa_elem_size);
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
        p->last_parsed_struct_tag = "";
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
                    val = (int)eval_const(p, expr(p));
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
        p->last_parsed_struct_tag = "";
        const char *t = take(p, nullptr);
        if (strcmp(t, "char") == 0) {
            base_size = 1;
        } else if (strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0) {
            base_size = 1;
            p->last_type_bool = 1;
        } else if (strcmp(t, "short") == 0) {
            if (strcmp(peek(p), "int") == 0) take(p, nullptr);
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
                base_size = p->target_scale;
            }
        } else if (strcmp(t, "void") == 0) {
            base_size = 8;
        } else {
            HashMapEntry *typedef_size = hashmap_get(&p->global_typedef_sizes, t);
            if (typedef_size) {
                p->last_parsed_typedef = t;
                base_size = typedef_size->val_int;
                HashMapEntry *typedef_unsigned = hashmap_get(&p->global_typedef_unsigned, t);
                if (typedef_unsigned) p->last_type_unsigned = typedef_unsigned->val_int;
                HashMapEntry *typedef_float = hashmap_get(&p->global_typedef_float, t);
                if (typedef_float) p->last_type_float = typedef_float->val_int;
                HashMapEntry *typedef_bool = hashmap_get(&p->global_typedef_bool, t);
                if (typedef_bool) p->last_type_bool = typedef_bool->val_int;
                HashMapEntry *typedef_tag = hashmap_get(&p->global_typedef_struct_tags, t);
                if (typedef_tag) {
                    const char *resolved_tag = (const char *)typedef_tag->val_ptr;
                    p->last_parsed_struct_tag = resolved_tag;
                    HashMapEntry *struct_size = hashmap_get(&p->global_struct_sizes, resolved_tag);
                    if (struct_size) {
                        base_size = struct_size->val_int;
                    }
                }
            } else {
                base_size = p->target_scale;
            }
        }
    }
    while (strcmp(peek(p), "_Complex") == 0 || strcmp(peek(p), "complex") == 0 ||
           strcmp(peek(p), "__complex__") == 0 || strcmp(peek(p), "_Imaginary") == 0 ||
           strcmp(peek(p), "imaginary") == 0) {
        is_complex = 1;
        take(p, nullptr);
    }
    if (is_complex) {
        base_size *= 2;
    }
    return base_size;
}

static void parser_type(ParserState *p) {
    parse_base_type(p);
    while (strcmp(peek(p), "*") == 0) {
        take_star(p);
    }
    skip_attribute(p);
}

static int is_type_start_token(ParserState *p, const char *t) {
    return strcmp(t, "int") == 0 || strcmp(t, "char") == 0 ||
           strcmp(t, "short") == 0 || strcmp(t, "long") == 0 || strcmp(t, "_Atomic") == 0 ||
           strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0 ||
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
            strcmp(t, "__restrict__") == 0 || strcmp(t, "register") == 0 ||
            strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0 ||
            is_calling_convention_qualifier(t)) {
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
            strcmp(t, "inline") == 0 || strcmp(t, "__inline") == 0 || strcmp(t, "__inline__") == 0 || strcmp(t, "register") == 0 || strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0 || strcmp(t, "_Noreturn") == 0 || strcmp(t, "noreturn") == 0 || is_calling_convention_qualifier(t)) {
            pos++;
        } else if (strcmp(t, "struct") == 0 || strcmp(t, "union") == 0 || strcmp(t, "enum") == 0) {
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
    if (node->op_enum == OP_NUM) {
        if (node->value > 2147483647L || node->value < -2147483648L) {
            node->type_size = 8;
        } else {
            node->type_size = 4;
        }
    }
}

static int promoted_is_unsigned(const Node *node) {
    if (!node) return 0;
    if (node->op_enum == OP_EQ || node->op_enum == OP_NE ||
        node->op_enum == OP_LT || node->op_enum == OP_GT ||
        node->op_enum == OP_LE || node->op_enum == OP_GE ||
        node->op_enum == OP_LAND || node->op_enum == OP_LOR ||
        node->op_enum == OP_UNARY_NOT) {
        return 0;
    }
    return node && node->is_unsigned && node->type_size >= 4;
}

static void apply_integer_literal_conversion(ParserState *p, Node *node, const char *literal, int is_hex) {
    apply_integer_conversion(p, node);
    int has_unsigned_suffix = 0;
    int has_long_suffix = 0;
    int lit_i = 0;
    while (literal[lit_i]) {
        if (literal[lit_i] == 'u' || literal[lit_i] == 'U') has_unsigned_suffix = 1;
        if (literal[lit_i] == 'l' || literal[lit_i] == 'L') has_long_suffix = 1;
        lit_i++;
    }
    if (has_long_suffix) {
        node->type_size = 8;
    }
    if (has_unsigned_suffix) {
        node->is_unsigned = 1;
        return;
    }
    if (is_hex && node->type_size == 8 && node->value < 0) {
        node->is_unsigned = 1;
        return;
    }
    if (is_hex && node->value > 2147483647L && node->value <= 4294967295L) {
        node->type_size = 4;
        node->is_unsigned = 1;
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
                take_star(p);
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
    if (strcmp(peek(p), "__func__") == 0 || strcmp(peek(p), "__FUNCTION__") == 0) {
        take(p, nullptr);
        Node *n = create_node(p, "str", tok_line, tok_col);
        n->name = p->current_func_name ? p->current_func_name : "";
        n->type_size = p->target_scale;
        return n;
    }
    if (strcmp(peek(p), "__builtin_offsetof") == 0) {
        take(p, "__builtin_offsetof");
        take(p, "(");
        const char *tag = "";
        if (strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) {
            take(p, nullptr);
            tag = take(p, nullptr);
        } else {
            const char *type_name = take(p, nullptr);
            HashMapEntry *typedef_tag = hashmap_get(&p->global_typedef_struct_tags, type_name);
            tag = typedef_tag ? (const char *)typedef_tag->val_ptr : type_name;
        }
        take(p, ",");
        long total_offset = 0;
        const char *current_tag = tag;
        while (1) {
            const char *field_name = take(p, nullptr);
            char key[512];
            snprintf(key, sizeof(key), "%s.%s", current_tag, field_name);
            HashMapEntry *off_entry = hashmap_get(&p->global_struct_field_offsets_by_tag, key);
            if (!off_entry) parser_error(p, "unknown field in __builtin_offsetof");
            total_offset += off_entry->val_int;
            if (strcmp(peek(p), ".") != 0) {
                break;
            }
            take(p, ".");
            HashMapEntry *tag_entry = hashmap_get(&p->global_struct_field_tags, key);
            if (!tag_entry) parser_error(p, "unknown nested field in __builtin_offsetof");
            current_tag = (const char *)tag_entry->val_ptr;
        }
        take(p, ")");
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = total_offset;
        n->type_size = p->target_scale;
        n->is_unsigned = 1;
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
    if (strcmp(peek(p), "__builtin_va_start") == 0) {
        take(p, "__builtin_va_start");
        take(p, "(");
        Node *ap_node = expr(p);
        take(p, ",");
        Node *last_arg_node = expr(p);
        take(p, ")");
        Node *n = create_node(p, "va_start", tok_line, tok_col);
        n->lhs = ap_node;
        n->rhs = last_arg_node;
        return n;
    }
    if (strcmp(peek(p), "__builtin_va_arg") == 0) {
        take(p, "__builtin_va_arg");
        take(p, "(");
        Node *ap_node = expr(p);
        take(p, ",");
        int base_size = parse_base_type(p);
        int stars = 0;
        int is_unsigned = p->last_type_unsigned;
        int is_float = p->last_type_float;
        while (strcmp(peek(p), "*") == 0) {
            take_star(p);
            stars++;
            base_size = p->target_scale;
            is_unsigned = 1;
            is_float = 0;
        }
        take(p, ")");
        Node *n = create_node(p, "va_arg", tok_line, tok_col);
        n->lhs = ap_node;
        n->type_size = (stars > 0) ? p->target_scale : base_size;
        n->is_unsigned = is_unsigned;
        n->is_float = is_float;
        return n;
    }
    if (strcmp(peek(p), "__builtin_va_end") == 0) {
        take(p, "__builtin_va_end");
        take(p, "(");
        expr(p);
        take(p, ")");
        Node *n = create_node(p, "num", tok_line, tok_col);
        n->value = 0;
        n->type_size = 4;
        return n;
    }
    if (strcmp(peek(p), "__builtin_va_copy") == 0) {
        take(p, "__builtin_va_copy");
        take(p, "(");
        Node *dest_node = expr(p);
        take(p, ",");
        Node *src_node = expr(p);
        take(p, ")");
        Node *n = create_node(p, "va_copy", tok_line, tok_col);
        n->lhs = dest_node;
        n->rhs = src_node;
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
        Node *n = comma_expr(p);
        take(p, ")");
        return n;
    }
    if (p->pos < p->tokens.count && p->tokens.data[p->pos].text[0] == '"') {
        StringBuilder joined;
        sb_init(&joined);
        while (p->pos < p->tokens.count && p->tokens.data[p->pos].text[0] == '"') {
            const char *t = take(p, nullptr);
            size_t len = strlen(t);
            if (len >= 2) {
                const char *part = arena_strndup(p->arena, t + 1, len - 2);
                sb_append(&joined, part);
            }
        }
        Node *n = create_node(p, "str", tok_line, tok_col);
        n->name = sb_to_string(&joined, p->arena);
        sb_free(&joined);
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
        } else {
            for (const char *s = t; *s; ++s) {
                if (*s == '.' || *s == 'p' || *s == 'P') { is_fp = 1; break; }
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
        n->value = (long)strtoull(t, nullptr, is_hex ? 16 : 10);
        apply_integer_literal_conversion(p, n, t, is_hex);
        return n;
    }
    const char *name = take(p, nullptr);
    const char *resolved = name;
    HashMapEntry *sl_entry = hashmap_get((HashMap *)&p->current_static_locals, name);
    if (sl_entry) {
        resolved = (const char *)sl_entry->val_ptr;
    } else {
        resolved = resolve_name(p, name);
    }
    if (strcmp(resolved, name) == 0 && !hashmap_has((HashMap *)&p->value_sizes, resolved)) {
        HashMapEntry *enum_entry = hashmap_get(&p->global_enums, name);
        if (enum_entry) {
            Node *n = create_node(p, "num", tok_line, tok_col);
            n->value = enum_entry->val_int;
            n->type_size = 4;
            return n;
        }
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
    if (n->type_size == p->target_scale) {
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", resolved);
        HashMapEntry *is_pointer_entry = hashmap_get((HashMap *)&ir_local_var_is_pointer, local_key);
        if (is_pointer_entry && is_pointer_entry->val_int && !hashmap_has((HashMap *)&ir_local_array_dims, local_key)) {
            int pointee_size = ir_get_local_var_elem_scale(local_key);
            if (pointee_size > 0) n->pointee_size = pointee_size;
        } else {
            is_pointer_entry = hashmap_get((HashMap *)&ir_global_var_is_pointer, resolved);
            if (is_pointer_entry && is_pointer_entry->val_int && !hashmap_has((HashMap *)&ir_global_array_dims, resolved)) {
                int pointee_size = ir_get_global_var_elem_scale(resolved);
                if (pointee_size > 0) n->pointee_size = pointee_size;
            }
        }
    }
    HashMapEntry *pointee_unsigned = hashmap_get((HashMap *)&p->pointer_pointee_unsigned, resolved);
    if (pointee_unsigned) {
        n->pointee_unsigned = pointee_unsigned->val_int;
        n->pointee_unsigned_known = 1;
    }
    return n;
}

static int is_lvalue_node(const Node *n) {
    if (!n) return 0;
    if (n->op_enum == OP_VAR || n->op_enum == OP_INDEX || n->op_enum == OP_UNARY_DEREF) {
        return 1;
    }
    if (n->op_enum == OP_COMMA) {
        return is_lvalue_node(n->rhs);
    }
    return 0;
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
            deref->is_unsigned = n->is_unsigned;
	            if (n->pointee_size > 0) {
	                deref->elem_size = n->pointee_size;
	                deref->type_size = n->pointee_size;
	                deref->is_unsigned = n->pointee_unsigned_known ? n->pointee_unsigned : n->is_unsigned;
	            } else if (n->op_enum == OP_VAR) {
	                char local_key[512];
	                snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", n->name);
	                int elem = ir_get_local_array_base_size(local_key);
	                if (elem == 0) elem = ir_get_global_array_base_size(n->name);
	                int pointee = ir_get_local_var_elem_scale(local_key);
	                if (pointee == 0) pointee = ir_get_global_var_elem_scale(n->name);
	                if (elem == 0) elem = pointee;
		                if (elem > 0) {
		                    deref->elem_size = elem;
		                    deref->type_size = elem;
		                    deref->is_unsigned = n->pointee_unsigned_known ? n->pointee_unsigned : n->is_unsigned;
		                    if (elem == p->target_scale && pointee > 0) {
		                        deref->pointee_size = pointee;
		                    }
		                }
		            }
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
            if (n->op_enum == OP_VAR) {
                HashMapEntry *ret_size = hashmap_get(&p->function_return_sizes, n->name);
                if (ret_size) call->type_size = ret_size->val_int;
                HashMapEntry *ret_unsigned = hashmap_get(&p->function_return_unsigned, n->name);
                if (ret_unsigned) call->is_unsigned = ret_unsigned->val_int;
            }
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
                HashMapEntry *float_entry = hashmap_get(&p->global_struct_field_float_sizes_by_tag, field_key);
                if (float_entry) {
                    parent->is_float = 1;
                    parent->type_size = float_entry->val_int;
                }
                HashMapEntry *unsigned_entry = hashmap_get(&p->global_struct_field_unsigned_by_tag, field_key);
                if (unsigned_entry) {
                    parent->is_unsigned = unsigned_entry->val_int;
                }
                HashMapEntry *elem_entry = hashmap_get(&p->global_struct_field_elem_sizes_by_tag, field_key);
                if (elem_entry) {
                    parent->pointee_size = elem_entry->val_int;
                }
                HashMapEntry *tot_entry = hashmap_get(&p->global_struct_field_total_sizes_by_tag, field_key);
                field_total_sz = tot_entry ? tot_entry->val_int : field_sz;
                
                HashMapEntry *dims_entry = hashmap_get(&p->global_struct_field_dims_by_tag, field_key);
                LongArray *dims_ptr = dims_entry ? (LongArray *)dims_entry->val_ptr : nullptr;
                if (!dims_ptr) {
                    dims_ptr = ir_get_struct_field_dims(field_key);
                }
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
            parent->type_size = field_sz;
            parent->aggregate_size = (field_tag[0] || field_dims.count > 0 || field_total_sz > field_sz) ? field_total_sz : 0;
            parent->array_dims = field_dims;
            parent->type_tag = field_tag;

            /* Bitfield metadata */
            if (field_key[0]) {
                HashMapEntry *bf_off_entry = hashmap_get(&p->global_struct_field_bit_offsets_by_tag, field_key);
                int bf_bit_offset = bf_off_entry ? bf_off_entry->val_int : ir_get_struct_field_bit_offset(field_key);
                HashMapEntry *bf_w_entry = hashmap_get(&p->global_struct_field_bit_widths_by_tag, field_key);
                int bf_bit_width = bf_w_entry ? bf_w_entry->val_int : ir_get_struct_field_bit_width(field_key);
                if (bf_bit_width > 0) {
                    parent->bit_offset = bf_bit_offset;
                    parent->bit_width  = bf_bit_width;
                }
            }
            n = parent;
        } else if (strcmp(peek(p), "++") == 0 || strcmp(peek(p), "--") == 0) {
            const char *op = take(p, nullptr);
            if (!is_lvalue_node(n)) {
                diagnostics_error(tok_line, tok_col, "lvalue required as increment operand");
            }
            char op_name[64];
            snprintf(op_name, sizeof(op_name), "postfix_%s", op);
            Node *parent = create_node(p, arena_strdup(p->arena, op_name), tok_line, tok_col);
            parent->name = n->name;
            parent->lhs = n;
            parent->type_size = n->type_size;
            parent->elem_size = n->elem_size;
            parent->aggregate_size = n->aggregate_size;
            parent->type_tag = n->type_tag;
            parent->is_unsigned = n->is_unsigned;
            parent->is_bool = n->is_bool;
            parent->is_float = n->is_float;
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
        n->is_unsigned = n->lhs ? n->lhs->is_unsigned : 0;
        int elem = (n->lhs && n->lhs->pointee_size > 0) ? n->lhs->pointee_size : 0;
        if (elem == 0 && n->lhs && n->lhs->op_enum == OP_VAR) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name ? p->current_func_name : "", n->lhs->name);
            elem = ir_get_local_var_elem_scale(local_key);
            if (elem == 0) elem = ir_get_global_var_elem_scale(n->lhs->name);
        }
        if (elem > 0 && n->lhs) {
            n->is_unsigned = n->lhs->pointee_unsigned_known ? n->lhs->pointee_unsigned : n->lhs->is_unsigned;
        }
        n->elem_size = elem;
        n->type_size = elem > 0 ? elem : p->target_scale;
        const char *tag = infer_struct_tag(p, n->lhs);
        if (tag && tag[0]) {
            n->type_tag = tag;
            HashMapEntry *se = hashmap_get(&p->global_struct_sizes, tag);
            int struct_size = se ? se->val_int : n->type_size;
            n->type_size = struct_size;
            n->elem_size = struct_size;
            n->aggregate_size = struct_size;
        }
        return n;
    }
    if (strcmp(peek(p), "+") == 0) {
        take(p, "+");
        return unary(p);
    }
    if (strcmp(peek(p), "++") == 0 || strcmp(peek(p), "--") == 0) {
        const char *op = take(p, nullptr);
        char op_name[64];
        snprintf(op_name, sizeof(op_name), "prefix_%s", op);
        Node *n = create_node(p, arena_strdup(p->arena, op_name), tok_line, tok_col);
        n->lhs = unary(p);
        if (n->lhs->op_enum == OP_VAR) {
            n->name = n->lhs->name;
        }
        n->type_size = n->lhs->type_size;
        n->elem_size = n->lhs->elem_size;
        n->aggregate_size = n->lhs->aggregate_size;
        n->type_tag = n->lhs->type_tag;
        n->is_unsigned = n->lhs->is_unsigned;
        n->is_bool = n->lhs->is_bool;
        n->is_float = n->lhs->is_float;
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
            n->is_unsigned = promoted_is_unsigned(n->lhs);
            /* unary minus on a float keeps the floating type; '~'/'!' do not */
            if (strcmp(op, "-") == 0 && n->lhs->is_float) {
                n->is_float = 1;
                n->type_size = n->lhs->type_size;
                n->is_unsigned = 0;
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
                take_star(p);
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
        const char *compound_struct_tag = "";
        if ((strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) &&
            p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
            compound_struct_tag = p->tokens.data[p->pos + 1].text;
        }
        int is_unsigned_cast = 0;
        int is_bool_cast = 0;
        int base_size = parse_base_type(p);
        if (!compound_struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
            compound_struct_tag = p->last_parsed_struct_tag;
        }
        is_unsigned_cast = p->last_type_unsigned;
        is_bool_cast = p->last_type_bool;
        int is_float_cast = p->last_type_float;
        if (p->last_parsed_typedef && p->last_parsed_typedef[0]) {
            HashMapEntry *typedef_unsigned = hashmap_get(&p->global_typedef_unsigned, p->last_parsed_typedef);
            if (typedef_unsigned) is_unsigned_cast = typedef_unsigned->val_int;
            HashMapEntry *typedef_bool = hashmap_get(&p->global_typedef_bool, p->last_parsed_typedef);
            if (typedef_bool) is_bool_cast = typedef_bool->val_int;
            HashMapEntry *typedef_float = hashmap_get(&p->global_typedef_float, p->last_parsed_typedef);
            if (typedef_float) is_float_cast = typedef_float->val_int;
        }
        while (strcmp(peek(p), "*") == 0) {
            take_star(p);
            base_size = p->target_scale;
            is_unsigned_cast = 1; // pointers are unsigned
            is_float_cast = 0;    // pointer, not a floating value
        }
        while (strcmp(peek(p), "[") == 0) {
            take(p, "[");
            if (strcmp(peek(p), "]") != 0) {
                expr(p);
            }
            take(p, "]");
            base_size = p->target_scale;
            is_unsigned_cast = 1;
            is_float_cast = 0;
        }
        if (strcmp(peek(p), "(") == 0 &&
            p->pos + 1 < p->tokens.count &&
            strcmp(p->tokens.data[p->pos + 1].text, "*") == 0) {
            take(p, "(");
            take_star(p);
            take(p, ")");
            if (strcmp(peek(p), "(") == 0) {
                int depth = 0;
                do {
                    if (strcmp(peek(p), "(") == 0) depth++;
                    else if (strcmp(peek(p), ")") == 0) depth--;
                    take(p, nullptr);
                } while (depth > 0 && strcmp(peek(p), "EOF") != 0);
            }
            base_size = p->target_scale;
            is_unsigned_cast = 1;
            is_float_cast = 0;
        }
        take(p, ")");
        if (strcmp(peek(p), "{") == 0) {
            if (compound_struct_tag[0]) {
                Node *compound = create_node(p, "compound_literal", tok_line, tok_col);
                compound->value = base_size;
                compound->type_size = base_size;
                compound->type_tag = compound_struct_tag;
                take(p, "{");
                long positional_offset = 0;
                while (strcmp(peek(p), "}") != 0) {
                    long offset = positional_offset;
                    int field_size = base_size;
                    if (strcmp(peek(p), ".") == 0) {
                        take(p, ".");
                        const char *field_name = take(p, nullptr);
                        char key[512];
                        snprintf(key, sizeof(key), "%s.%s", compound_struct_tag, field_name);
                        HashMapEntry *off_entry = hashmap_get(&p->global_struct_field_offsets_by_tag, key);
                        HashMapEntry *sz_entry = hashmap_get(&p->global_struct_field_sizes_by_tag, key);
                        if (!off_entry || !sz_entry) {
                            parser_error(p, "unknown field in compound literal");
                        }
                        offset = off_entry->val_int;
                        field_size = sz_entry->val_int;
                        take(p, "=");
                    }
                    Node *val;
                    if (strcmp(peek(p), "{") == 0) {
                        take(p, "{");
                        if (strcmp(peek(p), "}") == 0) {
                            val = create_node(p, "num", tok_line, tok_col);
                            val->value = 0;
                            val->type_size = field_size;
                        } else {
                            val = expr(p);
                            while (strcmp(peek(p), ",") == 0) {
                                take(p, ",");
                                if (strcmp(peek(p), "}") == 0) break;
                                expr(p);
                            }
                        }
                        take(p, "}");
                    } else {
                        val = expr(p);
                    }
                    Node *item = create_node(p, "init_item", tok_line, tok_col);
                    item->value = offset;
                    char size_buf[64];
                    snprintf(size_buf, sizeof(size_buf), "%d", field_size);
                    item->name = arena_strdup(p->arena, size_buf);
                    item->lhs = val;
                    node_array_push(&compound->body, item);
                    positional_offset += field_size;
                    if (strcmp(peek(p), ",") == 0) {
                        take(p, ",");
                        if (strcmp(peek(p), "}") == 0) break;
                    } else {
                        break;
                    }
                }
                take(p, "}");
                return compound;
            }
            take(p, "{");
            if (strcmp(peek(p), "}") != 0) {
                if (peek(p)[0] == '"') {
                    while (peek(p)[0] == '"') take(p, nullptr);
                } else {
                    expr(p);
                }
                while (strcmp(peek(p), ",") == 0) {
                    take(p, ",");
                    if (strcmp(peek(p), "}") == 0) break;
                    if (peek(p)[0] == '"') {
                        while (peek(p)[0] == '"') take(p, nullptr);
                    } else {
                        expr(p);
                    }
                }
            }
            take(p, "}");
            Node *zero = create_node(p, "num", tok_line, tok_col);
            zero->value = 0;
            zero->type_size = base_size;
            zero->is_unsigned = is_unsigned_cast;
            return zero;
        }
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
        int l_unsigned = promoted_is_unsigned(n);
        int r_unsigned = promoted_is_unsigned(rhs);
        if (tls == trs) {
            parent->type_size = tls;
            parent->is_unsigned = l_unsigned || r_unsigned;
        } else if (tls > trs) {
            parent->type_size = tls;
            parent->is_unsigned = l_unsigned;
        } else {
            parent->type_size = trs;
            parent->is_unsigned = r_unsigned;
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
        int l_unsigned = promoted_is_unsigned(n);
        int r_unsigned = promoted_is_unsigned(rhs);
        if (als == ars) {
            parent->type_size = als;
            parent->is_unsigned = l_unsigned || r_unsigned;
        } else if (als > ars) {
            parent->type_size = als;
            parent->is_unsigned = l_unsigned;
        } else {
            parent->type_size = ars;
            parent->is_unsigned = r_unsigned;
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
        if ((n->type_size < 4 ? 4 : n->type_size) == (rhs->type_size < 4 ? 4 : rhs->type_size)) {
            parent->compare_unsigned = promoted_is_unsigned(n) || promoted_is_unsigned(rhs);
        } else if (promoted_is_unsigned(n) && n->type_size > rhs->type_size) {
            parent->compare_unsigned = 1;
        } else if (promoted_is_unsigned(rhs) && rhs->type_size > n->type_size) {
            parent->compare_unsigned = 1;
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
        if ((n->type_size < 4 ? 4 : n->type_size) == (rhs->type_size < 4 ? 4 : rhs->type_size)) {
            parent->compare_unsigned = promoted_is_unsigned(n) || promoted_is_unsigned(rhs);
        } else if (promoted_is_unsigned(n) && n->type_size > rhs->type_size) {
            parent->compare_unsigned = 1;
        } else if (promoted_is_unsigned(rhs) && rhs->type_size > n->type_size) {
            parent->compare_unsigned = 1;
        }
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
        int lhs_size = n->type_size < 4 ? 4 : n->type_size;
        int rhs_size = rhs->type_size < 4 ? 4 : rhs->type_size;
        parent->type_size = lhs_size > rhs_size ? lhs_size : rhs_size;
        parent->is_unsigned = (lhs_size == rhs_size) ? (promoted_is_unsigned(n) || promoted_is_unsigned(rhs)) :
                              (lhs_size > rhs_size ? promoted_is_unsigned(n) : promoted_is_unsigned(rhs));
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
        int lhs_size = n->type_size < 4 ? 4 : n->type_size;
        int rhs_size = rhs->type_size < 4 ? 4 : rhs->type_size;
        parent->type_size = lhs_size > rhs_size ? lhs_size : rhs_size;
        parent->is_unsigned = (lhs_size == rhs_size) ? (promoted_is_unsigned(n) || promoted_is_unsigned(rhs)) :
                              (lhs_size > rhs_size ? promoted_is_unsigned(n) : promoted_is_unsigned(rhs));
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
        int lhs_size = n->type_size < 4 ? 4 : n->type_size;
        int rhs_size = rhs->type_size < 4 ? 4 : rhs->type_size;
        parent->type_size = lhs_size > rhs_size ? lhs_size : rhs_size;
        parent->is_unsigned = (lhs_size == rhs_size) ? (promoted_is_unsigned(n) || promoted_is_unsigned(rhs)) :
                              (lhs_size > rhs_size ? promoted_is_unsigned(n) : promoted_is_unsigned(rhs));
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
    if (is_assignment_operator(peek(p))) {
        const char *op = take(p, nullptr);
        Node *rhs = expr(p);
        if (strcmp(op, "=") != 0) {
            const char *bin_op_dup = nullptr;
            size_t oplen = strlen(op);
            if (oplen > 1 && op[oplen - 1] == '=') {
                char *tmp = malloc(oplen);
                memcpy(tmp, op, oplen - 1);
                tmp[oplen - 1] = '\0';
                bin_op_dup = arena_strdup(p->arena, tmp);
                free(tmp);
            } else {
                char bin_op[2] = { op[0], '\0' };
                bin_op_dup = arena_strdup(p->arena, bin_op);
            }
            Node *bin_node = create_node(p, bin_op_dup, tok_line, tok_col);
            bin_node->lhs = cond;
            bin_node->rhs = rhs;
            apply_integer_conversion(p, bin_node);
            rhs = bin_node;
        }
        if (cond->op_enum == OP_VAR) {
            HashMapEntry *const_entry = hashmap_get(&p->const_vars, cond->name);
            if (const_entry && const_entry->val_int) {
                diagnostics_error(tok_line, tok_col, "assignment of read-only (const-qualified) variable");
            }
            Node *node = create_node(p, "assign", tok_line, tok_col);
            node->name = cond->name;
	            node->lhs = rhs;
	            node->type_size = cond->type_size;
	            node->elem_size = cond->elem_size;
	            node->pointee_size = cond->pointee_size;
	            node->pointee_unsigned = cond->pointee_unsigned;
	            node->pointee_unsigned_known = cond->pointee_unsigned_known;
	            node->aggregate_size = cond->pointee_size > 0 ? 0 : cond->aggregate_size;
	            node->type_tag = cond->type_tag;
	            node->is_unsigned = cond->is_unsigned;
            node->is_bool = cond->is_bool;
            node->is_float = cond->is_float;
            HashMapEntry *b_entry = hashmap_get(&p->bool_vars, cond->name);
            if (b_entry && b_entry->val_int) {
                node->lhs = bool_normalize(p, node->lhs);
            }
            return node;
        } else {
            Node *node = create_node(p, "store_index", tok_line, tok_col);
            node->lhs = cond;
	            node->rhs = rhs;
	            node->type_size = cond->type_size;
	            node->elem_size = cond->elem_size;
	            node->pointee_size = cond->pointee_size;
	            node->pointee_unsigned = cond->pointee_unsigned;
	            node->pointee_unsigned_known = cond->pointee_unsigned_known;
	            node->aggregate_size = cond->pointee_size > 0 ? 0 : cond->aggregate_size;
	            node->type_tag = cond->type_tag;
	            node->is_unsigned = cond->is_unsigned;
            node->is_bool = cond->is_bool;
            node->is_float = cond->is_float;
            return node;
        }
    }
    return cond;
}

static Node *comma_expr(ParserState *p) {
    int tok_line = 1, tok_col = 1;
    if (p->pos < p->tokens.count) {
        tok_line = p->tokens.data[p->pos].line;
        tok_col = p->tokens.data[p->pos].col;
    }
    Node *node = expr(p);
    while (strcmp(peek(p), ",") == 0) {
        take(p, ",");
        Node *parent = create_node(p, ",", tok_line, tok_col);
        parent->lhs = node;
        parent->rhs = expr(p);
        parent->type_size = parent->rhs->type_size;
        parent->elem_size = parent->rhs->elem_size;
        parent->pointee_size = parent->rhs->pointee_size;
        parent->pointee_unsigned = parent->rhs->pointee_unsigned;
        parent->pointee_unsigned_known = parent->rhs->pointee_unsigned_known;
        parent->aggregate_size = parent->rhs->aggregate_size;
        parent->aggregate_float_class = parent->rhs->aggregate_float_class;
        parent->type_tag = parent->rhs->type_tag;
        parent->is_unsigned = parent->rhs->is_unsigned;
        parent->is_bool = parent->rhs->is_bool;
        parent->is_float = parent->rhs->is_float;
        node = parent;
    }
    return node;
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

static void parse_array_element_init(ParserState *p, long elem_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, long elem_total_size, InitElementArray *inits) {
    if (dim_idx + 1 < (size_t)array_dims->count) {
        if (strcmp(peek(p), "{") != 0) {
            long flat_count = 1;
            for (size_t k = dim_idx + 1; k < (size_t)array_dims->count; ++k) {
                flat_count *= array_dims->data[k];
            }
            for (long flat_idx = 0; flat_idx < flat_count; ++flat_idx) {
                Node *val = expr(p);
                InitElement item;
                item.offset = elem_offset + flat_idx * base_type_size;
                item.val = val;
                item.size = base_type_size;
                init_element_array_push(inits, &item);
                if (flat_idx + 1 < flat_count && strcmp(peek(p), ",") == 0) {
                    take(p, ",");
                }
            }
        } else {
            parse_aggregate_init(p, elem_offset, struct_tag, array_dims, dim_idx + 1, base_type_size, inits);
        }
    } else {
        if (struct_tag && struct_tag[0]) {
            parse_aggregate_init(p, elem_offset, struct_tag, nullptr, 0, base_type_size, inits);
        } else {
            Node *val = expr(p);
            InitElement item;
            item.offset = elem_offset;
            item.val = val;
            item.size = base_type_size;
            init_element_array_push(inits, &item);
        }
    }
    (void)elem_total_size;
}

static void parse_aggregate_init_internal(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits, int has_braces) {
    (void)has_braces;
    if (array_dims && dim_idx < (size_t)array_dims->count) {
        long limit = array_dims->data[dim_idx];
        int is_unspecified = (limit == 0);
        if (base_type_size == 1 && dim_idx + 1 == (size_t)array_dims->count && peek(p)[0] == '"') {
            LongArray bytes;
            long_array_init(&bytes);
            while (peek(p)[0] == '"') {
                append_string_literal_bytes(p, &bytes, take(p, nullptr));
            }
            if (is_unspecified) {
                limit = bytes.count + 1;
                array_dims->data[dim_idx] = limit;
            }
            long emit_count = bytes.count;
            if (emit_count > limit) emit_count = limit;
            for (long idx = 0; idx < emit_count; ++idx) {
                Node *ch = create_node(p, "num", 1, 1);
                ch->value = bytes.data[idx];
                ch->type_size = 1;
                InitElement item;
                item.offset = base_offset + idx;
                item.val = ch;
                item.size = 1;
                init_element_array_push(inits, &item);
            }
            if (bytes.count < limit) {
                Node *zero = create_node(p, "num", 1, 1);
                zero->value = 0;
                zero->type_size = 1;
                InitElement item;
                item.offset = base_offset + bytes.count;
                item.val = zero;
                item.size = 1;
                init_element_array_push(inits, &item);
            }
            long_array_free(&bytes);
            return;
        }
        
        long elem_total_size = base_type_size;
        for (size_t k = dim_idx + 1; k < (size_t)array_dims->count; ++k) {
            elem_total_size *= array_dims->data[k];
        }

        long idx = 0;
        for (; is_unspecified || idx < limit; ++idx) {
            if (strcmp(peek(p), "}") == 0 || strcmp(peek(p), ";") == 0) {
                break;
            }
            if (strcmp(peek(p), "[") == 0) {
                take(p, "[");
                Node *designator_expr = expr(p);
                long designated_idx = eval_const(p, designator_expr);
                long range_end = designated_idx;
                int has_range = 0;
                if (strcmp(peek(p), "...") == 0) {
                    take(p, "...");
                    Node *end_expr = expr(p);
                    range_end = eval_const(p, end_expr);
                    has_range = 1;
                }
                take(p, "]");
                take(p, "=");
                idx = designated_idx;
                if (has_range) {
                    InitElementArray elem_inits;
                    init_element_array_init(&elem_inits);
                    parse_array_element_init(p, 0, struct_tag, array_dims, dim_idx, base_type_size, elem_total_size, &elem_inits);
                    for (long r = designated_idx; r <= range_end; ++r) {
                        long range_elem_offset = r * elem_total_size;
                        for (int e_i = 0; e_i < elem_inits.count; ++e_i) {
                            InitElement item;
                            item.offset = base_offset + range_elem_offset + elem_inits.data[e_i].offset;
                            item.val = elem_inits.data[e_i].val;
                            item.size = elem_inits.data[e_i].size;
                            init_element_array_push(inits, &item);
                        }
                    }
                    init_element_array_free(&elem_inits);
                    idx = range_end;
                    if (strcmp(peek(p), ",") == 0) {
                        take(p, ",");
                    } else {
                        break;
                    }
                    continue;
                }
            }
            long elem_offset = idx * elem_total_size;
            parse_array_element_init(p, base_offset + elem_offset, struct_tag, array_dims, dim_idx, base_type_size, elem_total_size, inits);

            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
            } else {
                break;
            }
        }
        if (is_unspecified) {
            array_dims->data[dim_idx] = idx;
        }
    } else if (struct_tag && struct_tag[0]) {
        HashMapEntry *entry = hashmap_get(&p->global_structs, struct_tag);
        if (!entry) parser_error(p, "unknown struct tag in initializer");
        HashMap *fields = (HashMap *)entry->val_ptr;
        
        int f_count = 0;
        for (int b = 0; b < fields->bucket_count; ++b) {
            HashMapEntry *curr = &fields->entries[b];
            if (curr->key && curr->key != TOMBSTONE) {
                f_count++;
            }
        }
        long last_field_offset = -1;
        const char *last_field_name = "";
        for (int idx = 0; idx < f_count; ++idx) {
            if (strcmp(peek(p), "}") == 0 || strcmp(peek(p), ";") == 0) {
                break;
            }
            HashMapEntry *f_entry = nullptr;
            for (int b = 0; b < fields->bucket_count; ++b) {
                HashMapEntry *curr = &fields->entries[b];
                if (curr->key && curr->key != TOMBSTONE) {
                    int after_last = curr->val_int > last_field_offset ||
                                     (curr->val_int == last_field_offset && strcmp(curr->key, last_field_name) > 0);
                    int before_best = !f_entry ||
                                      curr->val_int < f_entry->val_int ||
                                      (curr->val_int == f_entry->val_int && strcmp(curr->key, f_entry->key) < 0);
                    if (after_last && before_best) {
                        f_entry = curr;
                    }
                }
            }
            if (!f_entry) break;
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
            HashMapEntry *bf_off_entry = hashmap_get(&p->global_struct_field_bit_offsets_by_tag, key);
            int bf_bit_offset = bf_off_entry ? bf_off_entry->val_int : ir_get_struct_field_bit_offset(key);
            HashMapEntry *bf_w_entry = hashmap_get(&p->global_struct_field_bit_widths_by_tag, key);
            int bf_bit_width = bf_w_entry ? bf_w_entry->val_int : ir_get_struct_field_bit_width(key);
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

            HashMapEntry *ptr_entry = hashmap_get(&p->global_struct_field_is_pointer_by_tag, key);
            int field_is_pointer = ptr_entry && ptr_entry->val_int;
            if ((sub_dims && sub_dims->count > 0) || (sub_tag && sub_tag[0] && !field_is_pointer)) {
                parse_aggregate_init(p, base_offset + field_offset, sub_tag, sub_dims, 0, field_size, inits);
            } else {
                Node *val;
                if (strcmp(peek(p), "{") == 0) {
                    take(p, "{");
                    if (strcmp(peek(p), "}") == 0) {
                        val = create_node(p, "num", 1, 1);
                        val->value = 0;
                        val->type_size = field_size;
                    } else {
                        val = expr(p);
                        while (strcmp(peek(p), ",") == 0) {
                            take(p, ",");
                            if (strcmp(peek(p), "}") == 0) break;
                            expr(p);
                        }
                    }
                    take(p, "}");
                } else {
                    val = expr(p);
                }
                if (bf_bit_width > 0) {
                    unsigned long long raw = (unsigned long long)eval_const(p, val);
                    unsigned long long mask = bf_bit_width >= 64 ? ~0ULL : ((1ULL << bf_bit_width) - 1ULL);
                    Node *packed = create_node(p, "num", val->line, val->col);
                    packed->value = (long)((raw & mask) << bf_bit_offset);
                    packed->type_size = field_size;
                    val = packed;
                    int merged = 0;
                    for (int init_i = 0; init_i < inits->count; ++init_i) {
                        InitElement *prev = &inits->data[init_i];
                        if (prev->offset == base_offset + field_offset &&
                            prev->size == field_size &&
                            prev->val->op_enum == OP_NUM) {
                            prev->val->value |= val->value;
                            merged = 1;
                            break;
                        }
                    }
                    if (merged) {
                        if (has_inferred_dims) {
                            long_array_free(&inferred_dims);
                        }
                        if (peek(p)[0] == ',') {
                            take(p, ",");
                        } else {
                            break;
                        }
                        last_field_offset = f_entry->val_int;
                        last_field_name = f_entry->key;
                        continue;
                    }
                }
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
            last_field_offset = f_entry->val_int;
            last_field_name = f_entry->key;
        }
    }
}

static void fill_aggregate_zero(ParserState *p, long base_offset, const char *struct_tag, const LongArray *array_dims, size_t dim_idx, int base_type_size, InitElementArray *inits) {
    if (struct_tag && struct_tag[0]) {
        HashMapEntry *entry = hashmap_get(&p->global_structs, struct_tag);
        if (entry) {
            HashMap *fields = (HashMap *)entry->val_ptr;
            for (int b = 0; b < fields->bucket_count; ++b) {
                HashMapEntry *curr = &fields->entries[b];
                if (curr->key && curr->key != TOMBSTONE) {
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
                    HashMapEntry *ptr_entry = hashmap_get(&p->global_struct_field_is_pointer_by_tag, key);
                    int field_is_pointer = ptr_entry && ptr_entry->val_int;
                    if ((sub_dims && sub_dims->count > 0) || (sub_tag && sub_tag[0] && !field_is_pointer)) {
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
        if (array_dims && dim_idx + 1 == (size_t)array_dims->count &&
            (!struct_tag || !struct_tag[0]) && strcmp(peek(p), "{") == 0) {
            parse_aggregate_init(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits);
            if (strcmp(peek(p), "}") == 0) {
                take(p, "}");
            }
        } else if (strcmp(peek(p), "}") == 0) {
            take(p, "}");
            fill_aggregate_zero(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits);
        } else {
            parse_aggregate_init_internal(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits, 1);
            if (strcmp(peek(p), "}") == 0) {
                take(p, "}");
            }
        }
    } else {
        parse_aggregate_init_internal(p, base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits, 0);
    }
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

static Node *parse_local_array_decl(ParserState *p, const char *name, const char *unique_name_dup, int stars, int base_size, int is_unsigned, int is_bool, int base_float, int is_const, int is_volatile, const char *struct_tag, int is_func_ptr, int tok_line, int tok_col, LongArray *dims) {
    (void)name;
    Node *vla_expr = nullptr;
    int vla_detected = 0;
    while (strcmp(peek(p), "[") == 0) {
        take(p, "[");
        long dim_size = 0;
        if (strcmp(peek(p), "]") != 0) {
            Node *dim_expr = expr(p);
            if (const_expr_has_runtime_var(p, dim_expr)) {
                vla_detected = 1;
                vla_expr = dim_expr;
                dim_size = 64;
            } else {
                dim_size = eval_const(p, dim_expr);
            }
        }
        take(p, "]");
        long_array_push(dims, dim_size);
    }
    
    char local_key[512];
    snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
    const char *local_key_dup = arena_strdup(p->arena, local_key);
    
    if (vla_detected) {
        Node *node = create_node(p, "decl", tok_line, tok_col);
        node->name = unique_name_dup;
        node->vla_dim_expr = vla_expr;
        node->alignment = p->last_type_alignment;
        node->elem_size = (stars > 0) ? p->target_scale : base_size;
        
        ir_set_local_var_is_pointer(local_key_dup, 1);
        ir_set_local_var_elem_scale(local_key_dup, node->elem_size);
        
        hashmap_put(&p->unsigned_vars, unique_name_dup, nullptr, is_unsigned);
        hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool);
        hashmap_put(&p->value_sizes, unique_name_dup, nullptr, p->target_scale);
        hashmap_put(&p->const_vars, unique_name_dup, nullptr, is_const);
        hashmap_put(&p->volatile_vars, unique_name_dup, nullptr, is_volatile);
        hashmap_put(&p->float_vars, unique_name_dup, nullptr, base_float);
        node->is_float = base_float;
        node->type_size = p->target_scale;
        return node;
    }
    
    long total_size = 1;
    for (int k = 0; k < dims->count; ++k) {
        total_size *= dims->data[k];
    }
    if (struct_tag[0] && stars == 0 && dims->count == 0) {
        long struct_slots = (base_size + p->target_scale - 1) / p->target_scale;
        total_size = total_size * struct_slots;
        base_size = p->target_scale;
        long_array_push(dims, struct_slots);
    }
    
    Node *node = create_node(p, "array_decl", tok_line, tok_col);
    node->name = unique_name_dup;
    node->value = total_size;
    node->alignment = p->last_type_alignment;
    for (int d_i = 0; d_i < dims->count; ++d_i) {
        long_array_push(&node->array_dims, dims->data[d_i]);
    }
    
    ir_set_local_array_dims(local_key_dup, *dims);
    ir_set_local_array_base_size(local_key_dup, (stars > 0 || is_func_ptr) ? p->target_scale : base_size);
    ir_set_local_var_is_pointer(local_key_dup, stars > 0 || is_func_ptr);
    ir_set_local_var_elem_scale(local_key_dup, stars == 1 ? base_size : (stars > 1 || is_func_ptr ? p->target_scale : 1));
    
    hashmap_put(&p->unsigned_vars, unique_name_dup, nullptr, is_unsigned && stars == 0 && !is_func_ptr);
    if (dims->count > 0 && stars == 0) {
        ir_set_var_pointee_unsigned(unique_name_dup, is_unsigned);
    }
    hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && stars == 0);
    hashmap_put(&p->value_sizes, unique_name_dup, nullptr, total_size * ((stars > 0 || is_func_ptr) ? p->target_scale : base_size));
    hashmap_put(&p->const_vars, unique_name_dup, nullptr, is_const);
    hashmap_put(&p->volatile_vars, unique_name_dup, nullptr, is_volatile);
    hashmap_put(&p->float_vars, unique_name_dup, nullptr, base_float && stars == 0);
    if (struct_tag[0] && stars == 0) {
        hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
    }
    return node;
}

static int is_assignment_operator(const char *t) {
    return strcmp(t, "=") == 0 ||
           strcmp(t, "+=") == 0 || strcmp(t, "-=") == 0 ||
           strcmp(t, "*=") == 0 || strcmp(t, "/=") == 0 ||
           strcmp(t, "%=") == 0 || strcmp(t, "&=") == 0 ||
           strcmp(t, "^=") == 0 || strcmp(t, "|=") == 0 ||
           strcmp(t, "<<=") == 0 || strcmp(t, ">>=") == 0;
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
    if (strcmp(peek(p), "goto") == 0) {
        take(p, "goto");
        Node *n = create_node(p, "goto", tok_line, tok_col);
        n->name = take(p, nullptr);
        take(p, ";");
        return n;
    }
    if (p->pos + 1 < p->tokens.count &&
        strcmp(p->tokens.data[p->pos + 1].text, ":") == 0 &&
        strcmp(peek(p), "case") != 0 &&
        strcmp(peek(p), "default") != 0) {
        Node *n = create_node(p, "label_stmt", tok_line, tok_col);
        n->name = take(p, nullptr);
        take(p, ":");
        n->lhs = stmt(p);
        return n;
    }
    if (strcmp(peek(p), "{") == 0) {
        return block_stmt(p);
    }
    if (strcmp(peek(p), "__asm__") == 0 || strcmp(peek(p), "__asm") == 0 || strcmp(peek(p), "asm") == 0) {
        Node *n = create_node(p, "asm", tok_line, tok_col);
        n->name = parse_gnu_asm(p, 1);
        return n;
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
    
    if (strcmp(peek(p), "enum") == 0 &&
        ((p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, "{") == 0) ||
         (p->pos + 2 < p->tokens.count && strcmp(p->tokens.data[p->pos + 2].text, "{") == 0))) {
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
                val = (int)eval_const(p, expr(p));
            }
            hashmap_put(&p->global_enums, name, nullptr, val);
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
        int hfa_valid = !is_union_local;
        int hfa_elem_size = 0;
        int hfa_count = 0;
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
            int fbase_is_unsigned = p->last_type_unsigned;
            int fbase_is_float = p->last_type_float;
            if (!f_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
                f_tag = p->last_parsed_struct_tag;
            }
            while (1) {
                int fstars = 0;
                while (strcmp(peek(p), "*") == 0) {
                    take_star(p);
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
                    hfa_valid = 0;
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
                        hashmap_put(&p->global_struct_field_unsigned_by_tag, key_dup, nullptr, (fstars == 0) ? fbase_is_unsigned : 0);
                    }
                    bit_offset2 += w;
                    if (is_union_local && current_unit_size2 > union_max_sz) {
                        union_max_sz = current_unit_size2;
                    }
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
                        hfa_valid = 0;
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
                        hfa_valid = 0;
                        char field_key[512];
                        snprintf(field_key, sizeof(field_key), "%s.%s", tag, field_name);
                        hashmap_put(&p->global_struct_field_tags, arena_strdup(p->arena, field_key), (void *)f_tag, 0);
                    }
                    if (fstars > 0 || !fbase_is_float) {
                        hfa_valid = 0;
                    } else {
                        if (hfa_elem_size == 0) {
                            hfa_elem_size = fsize;
                        } else if (hfa_elem_size != fsize) {
                            hfa_valid = 0;
                        }
                        hfa_count++;
                        if (hfa_count > 4) {
                            hfa_valid = 0;
                        }
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
                        if (fstars == 0 && fbase_is_float) {
                            hashmap_put(&p->global_struct_field_float_sizes_by_tag, key_dup, nullptr, fsize);
                        }
                        if (fstars > 0) {
                            int field_elem_size = (fstars > 1) ? p->target_scale : fbase;
                            hashmap_put(&p->global_struct_field_elem_sizes_by_tag, key_dup, nullptr, field_elem_size);
                        }
                        hashmap_put(&p->global_struct_field_is_pointer_by_tag, key_dup, nullptr, fstars > 0);
                        hashmap_put(&p->global_struct_field_unsigned_by_tag, key_dup, nullptr, (fstars == 0) ? fbase_is_unsigned : 0);
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
        skip_attribute(p);
        if (bit_offset2 > 0) offset += current_unit_size2;
        int struct_byte_size = is_union_local ? union_max_sz : offset;
        if (struct_byte_size > 0 && !is_union_local && (struct_byte_size % p->target_scale) != 0) {
            struct_byte_size += p->target_scale - (struct_byte_size % p->target_scale);
        }
        if (tag[0]) {
            HashMap *fields_alloc = malloc(sizeof(HashMap));
            fields_alloc->entries = fields.entries;
            fields_alloc->bucket_count = fields.bucket_count;
            fields_alloc->size = fields.size;
            hashmap_put(&p->global_structs, tag, fields_alloc, 0);
            hashmap_put(&p->global_struct_sizes, tag, nullptr, struct_byte_size);
            parser_set_struct_float_aggregate_class(p, tag, hfa_valid, hfa_count, hfa_elem_size);
        } else {
            hashmap_free(&fields);
        }
        if (strcmp(peek(p), ";") != 0) {
            const char *name = take(p, nullptr);
            char unique_name[256];
            snprintf(unique_name, sizeof(unique_name), "%s$%d", name, p->local_var_counter++);
            const char *unique_name_dup = arena_strdup(p->arena, unique_name);
            hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)unique_name_dup, 0);
            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, struct_byte_size);
            if (tag[0]) {
                hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)tag, 0);
            }
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
            const char *local_key_dup = arena_strdup(p->arena, local_key);
            ir_set_local_var_is_pointer(local_key_dup, 0);
            ir_set_local_var_elem_scale(local_key_dup, p->target_scale);
            Node *decl = create_node(p, "decl", tok_line, tok_col);
            decl->name = unique_name_dup;
            decl->type_size = struct_byte_size;
            take(p, ";");
            return decl;
        }
        take(p, ";");
        return create_node(p, "block", tok_line, tok_col);
    }
    
    const char *t = peek(p);
    if (strcmp(t, "extern") == 0) {
        take(p, "extern");
        int parens = 0;
        int brackets = 0;
        while (strcmp(peek(p), "EOF") != 0) {
            if (strcmp(peek(p), "(") == 0) parens++;
            else if (strcmp(peek(p), ")") == 0 && parens > 0) parens--;
            else if (strcmp(peek(p), "[") == 0) brackets++;
            else if (strcmp(peek(p), "]") == 0 && brackets > 0) brackets--;
            if (parens == 0 && brackets == 0 && strcmp(peek(p), ";") == 0) {
                take(p, ";");
                break;
            }
            take(p, nullptr);
        }
        return create_node(p, "empty", tok_line, tok_col);
    }
    if (strcmp(t, "constexpr") == 0 ||
        strcmp(t, "int") == 0 || strcmp(t, "char") == 0 || strcmp(t, "short") == 0 || strcmp(t, "long") == 0 ||
        strcmp(t, "void") == 0 || strcmp(t, "unsigned") == 0 || strcmp(t, "signed") == 0 || strcmp(t, "_Bool") == 0 || strcmp(t, "bool") == 0 ||
        strcmp(t, "const") == 0 || strcmp(t, "volatile") == 0 || strcmp(t, "restrict") == 0 || strcmp(t, "__restrict") == 0 || strcmp(t, "__restrict__") == 0 || strcmp(t, "register") == 0 || strcmp(t, "_Thread_local") == 0 || strcmp(t, "thread_local") == 0 || strcmp(t, "_Atomic") == 0 ||
        strcmp(t, "float") == 0 || strcmp(t, "double") == 0 || strcmp(t, "struct") == 0 || strcmp(t, "union") == 0 || strcmp(t, "enum") == 0 ||
        hashmap_has(&p->global_typedefs, t) || is_calling_convention_qualifier(t)) {
        
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
        if (!struct_tag[0] && hashmap_has(&p->global_structs, t)) {
            struct_tag = t;
        }
        if (is_constexpr) {
            int is_unsigned = p->last_type_unsigned;
            int is_bool = p->last_type_bool;
            int stars = 0;
            while (strcmp(peek(p), "*") == 0) {
                take_star(p);
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
            take_star(p);
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
        LongArray func_ptr_dims;
        long_array_init(&func_ptr_dims);
        IntArray func_ptr_param_floats;
        int_array_init(&func_ptr_param_floats);
        int has_func_ptr_signature = 0;
        int func_ptr_return_float = base_float ? base_size : 0;
        if (strcmp(peek(p), "(") == 0) {
            take(p, "(");
            while (strcmp(peek(p), "*") == 0) {
                take_star(p);
            }
            skip_type_qualifiers(p);
            name = take(p, nullptr);
            while (strcmp(peek(p), "[") == 0) {
                take(p, "[");
                long dim_size = 0;
                if (strcmp(peek(p), "]") != 0) {
                    Node *dim_expr = expr(p);
                    dim_size = const_expr_has_runtime_var(p, dim_expr) ? 64 : eval_const(p, dim_expr);
                }
                take(p, "]");
                long_array_push(&func_ptr_dims, dim_size);
            }
            take(p, ")");
            parse_function_pointer_param_floats(p, &func_ptr_param_floats);
            has_func_ptr_signature = 1;
            is_func_ptr = 1;
        } else {
            name = take(p, nullptr);
        }
        skip_attribute_and_asm(p);

        if (!is_func_ptr && strcmp(peek(p), "(") == 0) {
            int depth = 0;
            do {
                if (strcmp(peek(p), "(") == 0) depth++;
                else if (strcmp(peek(p), ")") == 0) depth--;
                take(p, nullptr);
            } while (depth > 0 && strcmp(peek(p), "EOF") != 0);
            skip_attribute_and_asm(p);
            take(p, ";");
            long_array_free(&func_ptr_dims);
            int_array_free(&func_ptr_param_floats);
            return create_node(p, "block", tok_line, tok_col);
        }
        
        int is_pointer = (stars > 0 || is_func_ptr);
        int elem_scale = 1;
        if (stars == 1 && !is_func_ptr) {
            elem_scale = base_size;
        } else if (stars > 1 || is_func_ptr) {
            elem_scale = p->target_scale;
        }
        
        int is_aggregate = (func_ptr_dims.count > 0 || strcmp(peek(p), "[") == 0 || (struct_tag[0] && stars == 0));
        
        if (is_aggregate) {
            LongArray dims;
            long_array_init(&dims);
            for (int fd_i = 0; fd_i < func_ptr_dims.count; ++fd_i) {
                long_array_push(&dims, func_ptr_dims.data[fd_i]);
            }
            if (strcmp(peek(p), "[") == 0) {
                while (strcmp(peek(p), "[") == 0) {
                    take(p, "[");
                    long dim_size = 0;
                    if (strcmp(peek(p), "]") != 0) {
                        Node *dim_expr = expr(p);
                        dim_size = const_expr_has_runtime_var(p, dim_expr) ? 64 : eval_const(p, dim_expr);
                    }
                    take(p, "]");
                    long_array_push(&dims, dim_size);
                }
            }
            LongArray orig_dims;
            long_array_init(&orig_dims);
            for (int k = 0; k < dims.count; ++k) {
                long_array_push(&orig_dims, dims.data[k]);
            }

            long total_size = 1;
            for (int k = 0; k < dims.count; ++k) {
                total_size *= dims.data[k];
            }
            
            long orig_base_size = (stars > 0 || is_func_ptr) ? p->target_scale : base_size;
            if (struct_tag[0] && dims.count == 0) {
                long struct_slots = (base_size + p->target_scale - 1) / p->target_scale;
                total_size = total_size * struct_slots;
                base_size = p->target_scale;
                long_array_push(&dims, struct_slots);
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
                            const char *init_struct_tag = (stars > 0) ? "" : struct_tag;
                            parse_aggregate_init(p, 0, init_struct_tag, &orig_dims, 0, orig_base_size, &parsed_inits);
                        }
                    } else if (dims.count > 0 && orig_base_size == 1 && peek(p)[0] == '"') {
                        long ch_i = 0;
                        long limit = orig_dims.count > 0 ? orig_dims.data[0] : 2147483647L;
                        while (peek(p)[0] == '"') {
                            const char *lit = take(p, nullptr);
                            const char *cur = lit + 1;
                            while (*cur != '"' && ch_i < limit) {
                                long ch_val;
                                if (*cur == '\\' && cur[1] != '"') {
                                    cur++;
                                    if (*cur == 'n') ch_val = '\n';
                                    else if (*cur == 't') ch_val = '\t';
                                    else if (*cur == 'r') ch_val = '\r';
                                    else if (*cur == '0') ch_val = '\0';
                                    else ch_val = *cur;
                                } else {
                                    ch_val = *cur;
                                }
                                Node *ch = create_node(p, "num", tok_line, tok_col);
                                ch->value = ch_val;
                                ch->type_size = 1;
                                InitElement item;
                                item.offset = ch_i;
                                item.val = ch;
                                item.size = 1;
                                init_element_array_push(&parsed_inits, &item);
                                ch_i++;
                                cur++;
                            }
                        }
                        if (ch_i < limit) {
                            Node *zero = create_node(p, "num", tok_line, tok_col);
                            zero->value = 0;
                            zero->type_size = 1;
                            InitElement item;
                            item.offset = ch_i;
                            item.val = zero;
                            item.size = 1;
                            init_element_array_push(&parsed_inits, &item);
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

                if (orig_dims.count > 0 && orig_dims.data[0] > 0 && dims.count > 0 && dims.data[0] == 0) {
                    long inferred_total = 1;
                    for (int d_i = 0; d_i < orig_dims.count; ++d_i) {
                        if (orig_dims.data[d_i] > 0) {
                            inferred_total *= orig_dims.data[d_i];
                        }
                    }
                    if (struct_tag[0]) {
                        long struct_slots = (orig_base_size + p->target_scale - 1) / p->target_scale;
                        total_size = inferred_total * struct_slots;
                        dims.data[0] = total_size;
                    } else {
                        total_size = inferred_total;
                        dims.data[0] = orig_dims.data[0];
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
                        Node *init_val = parsed_inits.data[item_i].val;
                        if (init_val->op_enum == OP_CAST && init_val->lhs && init_val->lhs->op_enum == OP_STR) {
                            init_val = init_val->lhs;
                        }
                        long val = init_val->op_enum == OP_STR ? 0 : eval_const(p, init_val);
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
                long_array_free(&orig_dims);
                long_array_free(&func_ptr_dims);
                int_array_free(&func_ptr_param_floats);
                return create_node(p, "block", tok_line, tok_col);
            } else {
                char unique_name[256];
                snprintf(unique_name, sizeof(unique_name), "%s$%d", name, p->local_var_counter++);
                const char *unique_name_dup = arena_strdup(p->arena, unique_name);
                
                if (hashmap_has(&p->scopes[p->scope_count - 1], name)) {
                    diagnostics_error(tok_line, tok_col, "redefinition of local variable");
                }
                hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)unique_name_dup, 0);
                
                Node *node = parse_local_array_decl(p, name, unique_name_dup, stars, base_size, is_unsigned, is_bool, base_float, is_const, is_volatile, struct_tag, is_func_ptr, tok_line, tok_col, &dims);
                if (has_func_ptr_signature) {
                    ir_set_function_pointer_signature(unique_name_dup, &func_ptr_param_floats, func_ptr_return_float);
                }
                
                if (node->vla_dim_expr) {
                    long_array_free(&orig_dims);
                    long_array_free(&func_ptr_dims);
                    int_array_free(&func_ptr_param_floats);
                    return node;
                }
                
                if (strcmp(peek(p), "=") == 0) {
                    take(p, "=");
                    if (strcmp(peek(p), "{") == 0) {
                        InitElementArray local_inits;
                        init_element_array_init(&local_inits);
                        const char *init_struct_tag = (stars > 0) ? "" : struct_tag;
                        parse_aggregate_init(p, 0, init_struct_tag, &orig_dims, 0, orig_base_size, &local_inits);
                        
                        char local_key[512];
                        snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
                        const char *local_key_dup = arena_strdup(p->arena, local_key);
                        
                        if (node->array_dims.count > 0 && node->array_dims.data[0] == 0 && orig_dims.count > 0 && orig_dims.data[0] > 0) {
                            long inferred_total = 1;
                            for (int d_i = 0; d_i < orig_dims.count; ++d_i) {
                                if (orig_dims.data[d_i] > 0) {
                                    inferred_total *= orig_dims.data[d_i];
                                }
                            }
                            node->value = inferred_total;
                            dims.data[0] = orig_dims.data[0];
                            ir_set_local_array_dims(local_key_dup, dims);
                            int actual_base = (stars > 0 || is_func_ptr) ? p->target_scale : base_size;
                            ir_set_local_array_base_size(local_key_dup, actual_base);
                            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, inferred_total * actual_base);
                        }
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
                    } else if (node->array_dims.count > 0 && orig_base_size == 1 && peek(p)[0] == '"') {
                        const char *lit = take(p, nullptr);
                        long byte_count = 0;
                        while (lit[byte_count + 1] != '"') {
                            byte_count++;
                        }
                        char local_key[512];
                        snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
                        const char *local_key_dup = arena_strdup(p->arena, local_key);
                        if (node->array_dims.data[0] == 0) {
                            node->array_dims.data[0] = byte_count + 1;
                            node->value = byte_count + 1;
                            ir_set_local_array_dims(local_key_dup, node->array_dims);
                            ir_set_local_array_base_size(local_key_dup, base_size);
                            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, (byte_count + 1) * base_size);
                        }
                        for (long ch_i = 0; ch_i <= byte_count; ++ch_i) {
                            Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                            item_node->value = ch_i;
                            item_node->name = "1";
                            Node *ch_node = create_node(p, "num", tok_line, tok_col);
                            ch_node->value = (ch_i < byte_count) ? lit[ch_i + 1] : 0;
                            ch_node->type_size = 1;
                            item_node->lhs = ch_node;
                            node_array_push(&node->body, item_node);
                        }
                    } else {
                        Node *init = expr(p);
                        if (is_bool && stars == 0) {
                            init = bool_normalize(p, init);
                        }
                        if (node->array_dims.count > 0 && orig_base_size == 1 && init->op_enum == OP_STR) {
                            const char *lit = init->name;
                            long byte_count = 0;
                            while (lit[byte_count] != '\0') {
                                byte_count++;
                            }
                            char local_key[512];
                            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
                            const char *local_key_dup = arena_strdup(p->arena, local_key);
                            if (node->array_dims.data[0] == 0) {
                                node->array_dims.data[0] = byte_count + 1;
                                node->value = byte_count + 1;
                                ir_set_local_array_dims(local_key_dup, node->array_dims);
                                ir_set_local_array_base_size(local_key_dup, base_size);
                                hashmap_put(&p->value_sizes, unique_name_dup, nullptr, (byte_count + 1) * base_size);
                            }
                            for (long ch_i = 0; ch_i <= byte_count; ++ch_i) {
                                Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                                item_node->value = ch_i;
                                item_node->name = "1";
                                Node *ch_node = create_node(p, "num", tok_line, tok_col);
                                ch_node->value = (ch_i < byte_count) ? lit[ch_i] : 0;
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
                            item_node->lhs = init;
                            node_array_push(&node->body, item_node);
                        }
                    }
                }
                if (strcmp(peek(p), ",") == 0) {
                    Node *block = create_node(p, "block", tok_line, tok_col);
                    node_array_push(&block->body, node);
                    while (strcmp(peek(p), ",") == 0) {
                        take(p, ",");
                        int next_stars = 0;
                        while (strcmp(peek(p), "*") == 0) {
                            take_star(p);
                            next_stars++;
                        }
                        skip_type_qualifiers(p);
                        const char *next_name = take(p, nullptr);
                        skip_attribute_and_asm(p);

                        char next_unique_name[256];
                        snprintf(next_unique_name, sizeof(next_unique_name), "%s$%d", next_name, p->local_var_counter++);
                        const char *next_unique_name_dup = arena_strdup(p->arena, next_unique_name);

                        if (hashmap_has(&p->scopes[p->scope_count - 1], next_name)) {
                            diagnostics_error(tok_line, tok_col, "redefinition of local variable");
                        }
                        hashmap_put(&p->scopes[p->scope_count - 1], next_name, (void *)next_unique_name_dup, 0);

                        if (strcmp(peek(p), "[") == 0 || (struct_tag[0] && next_stars == 0)) {
                            LongArray next_dims;
                            long_array_init(&next_dims);
                            Node *next_array = parse_local_array_decl(p, next_name, next_unique_name_dup, next_stars, base_size, is_unsigned, is_bool, base_float, is_const, is_volatile, struct_tag, 0, tok_line, tok_col, &next_dims);
                            long_array_free(&next_dims);
                            
                            if (next_array->vla_dim_expr) {
                                node_array_push(&block->body, next_array);
                                continue;
                            }
                            
                            if (strcmp(peek(p), "=") == 0) {
                                take(p, "=");
                                if (strcmp(peek(p), "{") == 0) {
                                    LongArray next_orig_dims;
                                    long_array_init(&next_orig_dims);
                                    for (int k = 0; k < next_array->array_dims.count; ++k) {
                                        long_array_push(&next_orig_dims, next_array->array_dims.data[k]);
                                    }
                                    InitElementArray next_inits;
                                    init_element_array_init(&next_inits);
                                    const char *next_init_struct_tag = (next_stars > 0) ? "" : struct_tag;
                                    parse_aggregate_init(p, 0, next_init_struct_tag, &next_orig_dims, 0, orig_base_size, &next_inits);
                                    for (int item_i = 0; item_i < next_inits.count; ++item_i) {
                                        Node *item_node = create_node(p, "init_item", tok_line, tok_col);
                                        item_node->value = next_inits.data[item_i].offset;
                                        char sz_str[64];
                                        snprintf(sz_str, sizeof(sz_str), "%d", next_inits.data[item_i].size);
                                        item_node->name = arena_strdup(p->arena, sz_str);
                                        item_node->lhs = next_inits.data[item_i].val;
                                        node_array_push(&next_array->body, item_node);
                                    }
                                    init_element_array_free(&next_inits);
                                    long_array_free(&next_orig_dims);
                                } else {
                                    diagnostics_error(tok_line, tok_col, "non-braced initializer after a mixed aggregate declarator is not supported yet");
                                }
                            }

                            node_array_push(&block->body, next_array);
                            continue;
                        }

                        Node *next_node = create_node(p, "decl", tok_line, tok_col);
                        next_node->name = next_unique_name_dup;
                        next_node->alignment = p->last_type_alignment;

                        int next_is_pointer = next_stars > 0;
                        int next_elem_scale = next_stars == 1 ? (int)orig_base_size : (next_stars > 1 ? p->target_scale : 1);

                        char next_local_key[512];
                        snprintf(next_local_key, sizeof(next_local_key), "%s$%s", p->current_func_name, next_unique_name_dup);
                        const char *next_local_key_dup = arena_strdup(p->arena, next_local_key);

                        ir_set_local_var_is_pointer(next_local_key_dup, next_is_pointer);
                        ir_set_local_var_elem_scale(next_local_key_dup, next_elem_scale);

                        hashmap_put(&p->unsigned_vars, next_unique_name_dup, nullptr, is_unsigned && !next_is_pointer);
                        if (next_is_pointer) {
                            hashmap_put(&p->pointer_pointee_unsigned, next_unique_name_dup, nullptr, is_unsigned);
                            ir_set_var_pointee_unsigned(next_unique_name_dup, is_unsigned);
                        }
                        hashmap_put(&p->bool_vars, next_unique_name_dup, nullptr, is_bool && !next_is_pointer);
                        hashmap_put(&p->value_sizes, next_unique_name_dup, nullptr, next_is_pointer ? p->target_scale : orig_base_size);
                        hashmap_put(&p->const_vars, next_unique_name_dup, nullptr, is_const);
                        hashmap_put(&p->volatile_vars, next_unique_name_dup, nullptr, is_volatile);
                        hashmap_put(&p->float_vars, next_unique_name_dup, nullptr, base_float && !next_is_pointer);
                        next_node->is_float = base_float && !next_is_pointer;
                        next_node->type_size = next_is_pointer ? p->target_scale : (int)orig_base_size;
                        next_node->is_unsigned = is_unsigned && !next_is_pointer;
                        next_node->is_bool = is_bool && !next_is_pointer;

                        if (strcmp(peek(p), "=") == 0) {
                            take(p, "=");
                            if (strcmp(peek(p), "{") == 0) {
                                take(p, "{");
                                if (strcmp(peek(p), "}") != 0) {
                                    next_node->lhs = expr(p);
                                    while (strcmp(peek(p), ",") == 0) {
                                        take(p, ",");
                                        if (strcmp(peek(p), "}") == 0) break;
                                        expr(p);
                                    }
                                }
                                take(p, "}");
                            } else {
                                next_node->lhs = expr(p);
                                if (is_bool && !next_is_pointer) {
                                    next_node->lhs = bool_normalize(p, next_node->lhs);
                                }
                            }
                        }
                        node_array_push(&block->body, next_node);
                    }
                take(p, ";");
                long_array_free(&orig_dims);
                long_array_free(&func_ptr_dims);
                int_array_free(&func_ptr_param_floats);
                return block;
                }
                take(p, ";");
                long_array_free(&orig_dims);
                long_array_free(&func_ptr_dims);
                int_array_free(&func_ptr_param_floats);
                return node;
            }
        }
        long_array_free(&func_ptr_dims);
        
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
            if (is_pointer) {
                hashmap_put(&p->pointer_pointee_unsigned, unique_name_dup, nullptr, is_unsigned);
                ir_set_var_pointee_unsigned(unique_name_dup, is_unsigned);
            }
            ir_set_global_initializers(unique_name_dup, inits);
            
            hashmap_put(&p->current_static_locals, name, (void *)unique_name_dup, 0);
            if (struct_tag[0]) {
                hashmap_put(&p->var_struct_tags, unique_name_dup, (void *)struct_tag, 0);
            }
            hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && !is_pointer);
            
            while (strcmp(peek(p), ",") == 0) {
                take(p, ",");
                int next_stars = 0;
                while (strcmp(peek(p), "*") == 0) {
                    take_star(p);
                    next_stars++;
                }
                const char *next_name = take(p, nullptr);
                char next_unique_name[256];
                snprintf(next_unique_name, sizeof(next_unique_name), "%s$%s", p->current_func_name, next_name);
                const char *next_unique_name_dup = arena_strdup(p->arena, next_unique_name);
                LongArray next_inits;
                long_array_init(&next_inits);
                if (strcmp(peek(p), "=") == 0) {
                    take(p, "=");
                    Node *next_init = expr(p);
                    long_array_push(&next_inits, eval_const(p, next_init));
                }
                int next_is_pointer = next_stars > 0;
                ir_declare_global(next_unique_name_dup, 0, 1, 1, 0, next_is_pointer ? p->target_scale : base_size, p->target_scale);
                ir_set_global_var_is_pointer(next_unique_name_dup, next_is_pointer);
                ir_set_global_var_elem_scale(next_unique_name_dup, next_stars == 1 ? base_size : (next_stars > 1 ? p->target_scale : 1));
                if (next_is_pointer) {
                    hashmap_put(&p->pointer_pointee_unsigned, next_unique_name_dup, nullptr, is_unsigned);
                    ir_set_var_pointee_unsigned(next_unique_name_dup, is_unsigned);
                }
                ir_set_global_initializers(next_unique_name_dup, next_inits);
                hashmap_put(&p->current_static_locals, next_name, (void *)next_unique_name_dup, 0);
                hashmap_put(&p->bool_vars, next_unique_name_dup, nullptr, is_bool && !next_is_pointer);
                long_array_free(&next_inits);
            }
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
            node->alignment = p->last_type_alignment;
            
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, unique_name_dup);
            const char *local_key_dup = arena_strdup(p->arena, local_key);
            
            ir_set_local_var_is_pointer(local_key_dup, is_pointer);
            ir_set_local_var_elem_scale(local_key_dup, elem_scale);
            if (is_pointer) {
                hashmap_put(&p->pointer_pointee_unsigned, unique_name_dup, nullptr, is_unsigned);
                ir_set_var_pointee_unsigned(unique_name_dup, is_unsigned);
            }
            
            hashmap_put(&p->unsigned_vars, unique_name_dup, nullptr, is_unsigned);
            hashmap_put(&p->bool_vars, unique_name_dup, nullptr, is_bool && !is_pointer);
            hashmap_put(&p->value_sizes, unique_name_dup, nullptr, is_pointer ? p->target_scale : base_size);
            hashmap_put(&p->const_vars, unique_name_dup, nullptr, is_const);
            hashmap_put(&p->volatile_vars, unique_name_dup, nullptr, is_volatile);
            hashmap_put(&p->float_vars, unique_name_dup, nullptr, base_float && !is_pointer);
            if (has_func_ptr_signature) {
                ir_set_function_pointer_signature(unique_name_dup, &func_ptr_param_floats, func_ptr_return_float);
            }
            node->is_float = base_float && !is_pointer;
            node->type_size = is_pointer ? p->target_scale : base_size;
            node->is_unsigned = is_unsigned && !is_pointer;
            node->is_bool = is_bool && !is_pointer;

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
                        take_star(p);
                        next_stars++;
                    }
                    skip_type_qualifiers(p);
                    skip_attribute_and_asm(p);
                    
                    const char *next_name = "";
                    int next_is_func_ptr = 0;
                    IntArray next_func_ptr_param_floats;
                    int_array_init(&next_func_ptr_param_floats);
                    int next_has_func_ptr_signature = 0;
                    if (strcmp(peek(p), "(") == 0) {
                        take(p, "(");
                        while (strcmp(peek(p), "*") == 0) {
                            take_star(p);
                        }
                        next_name = take(p, nullptr);
                        take(p, ")");
                        parse_function_pointer_param_floats(p, &next_func_ptr_param_floats);
                        next_has_func_ptr_signature = 1;
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

                    if (strcmp(peek(p), "[") == 0) {
                        LongArray next_dims;
                        long_array_init(&next_dims);
                        Node *next_array = parse_local_array_decl(p, next_name, next_unique_name_dup, next_stars, base_size, is_unsigned, is_bool, base_float, is_const, is_volatile, struct_tag, next_is_func_ptr, tok_line, tok_col, &next_dims);
                        long_array_free(&next_dims);
                        node_array_push(&block->body, next_array);
                        int_array_free(&next_func_ptr_param_floats);
                        continue;
                    }
                    
                    Node *next_node = create_node(p, "decl", tok_line, tok_col);
                    next_node->name = next_unique_name_dup;
                    next_node->alignment = p->last_type_alignment;
                    
                    int next_is_pointer = (next_stars > 0 || next_is_func_ptr);
                    int next_elem_scale = next_is_func_ptr ? p->target_scale : (next_stars == 1 ? base_size : (next_stars > 1 ? p->target_scale : 1));
                    
                    char next_local_key[512];
                    snprintf(next_local_key, sizeof(next_local_key), "%s$%s", p->current_func_name, next_unique_name_dup);
                    const char *next_local_key_dup = arena_strdup(p->arena, next_local_key);
                    
                    ir_set_local_var_is_pointer(next_local_key_dup, next_is_pointer);
                    ir_set_local_var_elem_scale(next_local_key_dup, next_elem_scale);
                    if (next_is_pointer) {
                        hashmap_put(&p->pointer_pointee_unsigned, next_unique_name_dup, nullptr, is_unsigned);
                        ir_set_var_pointee_unsigned(next_unique_name_dup, is_unsigned);
                    }
                    
                    hashmap_put(&p->unsigned_vars, next_unique_name_dup, nullptr, is_unsigned);
                    hashmap_put(&p->bool_vars, next_unique_name_dup, nullptr, is_bool && !next_is_pointer);
                    hashmap_put(&p->value_sizes, next_unique_name_dup, nullptr, next_is_pointer ? p->target_scale : base_size);
                    hashmap_put(&p->float_vars, next_unique_name_dup, nullptr, base_float && !next_is_pointer);
                    if (next_has_func_ptr_signature) {
                        ir_set_function_pointer_signature(next_unique_name_dup, &next_func_ptr_param_floats, func_ptr_return_float);
                    }
                    next_node->is_float = base_float && !next_is_pointer;
                    next_node->type_size = next_is_pointer ? p->target_scale : base_size;
                    next_node->is_unsigned = is_unsigned && !next_is_pointer;
                    next_node->is_bool = is_bool && !next_is_pointer;

                    if (strcmp(peek(p), "=") == 0) {
                        take(p, "=");
                        if (strcmp(peek(p), "{") == 0) {
                            take(p, "{");
                            if (strcmp(peek(p), "}") != 0) {
                                next_node->lhs = expr(p);
                                while (strcmp(peek(p), ",") == 0) {
                                    take(p, ",");
                                    if (strcmp(peek(p), "}") == 0) break;
                                    expr(p);
                                }
                            }
                            take(p, "}");
                        } else {
                            next_node->lhs = expr(p);
                            if (is_bool && !next_is_pointer) {
                                next_node->lhs = bool_normalize(p, next_node->lhs);
                            }
                        }
                    }
                    node_array_push(&block->body, next_node);
                    int_array_free(&next_func_ptr_param_floats);
                }
                take(p, ";");
                int_array_free(&func_ptr_param_floats);
                return block;
            }
            take(p, ";");
            int_array_free(&func_ptr_param_floats);
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
        Node *cond = comma_expr(p);
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
        Node *cond = comma_expr(p);
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
        Node *cond = comma_expr(p);
        take(p, ")");
        Node *body_node = stmt(p);
        Node *n = create_node(p, "while", tok_line, tok_col);
        n->lhs = bool_normalize(p, cond);
        n->rhs = body_node;
        return n;
    }
    if (strcmp(peek(p), "do") == 0) {
        take(p, "do");
        Node *body_node = stmt(p);
        take(p, "while");
        take(p, "(");
        Node *cond = comma_expr(p);
        take(p, ")");
        take(p, ";");
        Node *n = create_node(p, "do_while", tok_line, tok_col);
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
                strcmp(t_for, "const") == 0 || strcmp(t_for, "volatile") == 0 || strcmp(t_for, "restrict") == 0 || strcmp(t_for, "__restrict") == 0 || strcmp(t_for, "__restrict__") == 0 || strcmp(t_for, "register") == 0 || strcmp(t_for, "_Thread_local") == 0 || strcmp(t_for, "thread_local") == 0 ||
                strcmp(t_for, "float") == 0 || strcmp(t_for, "double") == 0 || strcmp(t_for, "struct") == 0 || strcmp(t_for, "union") == 0 ||
                hashmap_has(&p->global_typedefs, t_for)) {
                
                const char *struct_tag = "";
                int base_size = parse_base_type(p);
                if (p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
                    struct_tag = p->last_parsed_struct_tag;
                }
                int orig_base_size = base_size;
                int is_unsigned = p->last_type_unsigned;
                int is_bool = p->last_type_bool || strcmp(t_for, "_Bool") == 0 || strcmp(t_for, "bool") == 0;
                int stars = 0;
                while (strcmp(peek(p), "*") == 0) {
                    take(p, "*");
                    base_size = p->target_scale;
                    is_unsigned = 1;
                    stars++;
                }
                int is_pointer = stars > 0;
                int elem_scale = stars == 1 ? orig_base_size : (stars > 1 ? p->target_scale : 1);
                const char *name = take(p, nullptr);
                char local_name[256];
                snprintf(local_name, sizeof(local_name), "%s.local.%d", name, p->local_var_counter++);
                const char *local_name_dup = arena_strdup(p->arena, local_name);
                hashmap_put(&p->scopes[p->scope_count - 1], name, (void *)local_name_dup, 0);
                
                char local_key[512];
                snprintf(local_key, sizeof(local_key), "%s$%s", p->current_func_name, local_name_dup);
                const char *local_key_dup = arena_strdup(p->arena, local_key);
                ir_set_local_var_is_pointer(local_key_dup, is_pointer);
                ir_set_local_var_elem_scale(local_key_dup, elem_scale);
                if (is_pointer) {
                    hashmap_put(&p->pointer_pointee_unsigned, local_name_dup, nullptr, is_unsigned);
                    ir_set_var_pointee_unsigned(local_name_dup, is_unsigned);
                }

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
                init_node->alignment = p->last_type_alignment;
                init_node->lhs = val;
                init_node->type_size = base_size;
                init_node->is_unsigned = is_unsigned;
                init_node->is_bool = is_bool;
            } else {
                init_node = create_node(p, "expr", tok_line, tok_col);
                init_node->lhs = comma_expr(p);
            }
        }
        take(p, ";");

        Node *cond_node = nullptr;
        if (strcmp(peek(p), ";") != 0) {
            cond_node = bool_normalize(p, comma_expr(p));
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
                step_node = create_node(p, "expr", tok_line, tok_col);
                step_node->lhs = comma_expr(p);
            } else {
                step_node = create_node(p, "expr", tok_line, tok_col);
                step_node->lhs = comma_expr(p);
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
    Node *node = create_node(p, "expr", tok_line, tok_col);
    node->lhs = comma_expr(p);
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
    const char *ret_struct_tag = "";
    if ((strcmp(peek(p), "struct") == 0 || strcmp(peek(p), "union") == 0) &&
        p->pos + 1 < p->tokens.count &&
        strcmp(p->tokens.data[p->pos + 1].text, "{") != 0) {
        ret_struct_tag = p->tokens.data[p->pos + 1].text;
    }
    int ret_base = parse_base_type(p);
    int ret_is_float = p->last_type_float;
    if (!ret_is_struct && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
        ret_is_struct = 1;
    }
    if (!ret_struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
        ret_struct_tag = p->last_parsed_struct_tag;
    }
    int ret_stars = 0;
    while (strcmp(peek(p), "*") == 0) {
        take_star(p);
        ret_stars++;
    }
    skip_type_qualifiers(p);
    skip_attribute(p);
    int ret_size = (ret_stars > 0) ? p->target_scale : ret_base;
    /* float/double returns are scalar FP (st0/xmm0/v0), never aggregates, even
       though a double is wider than the 32-bit i386 word. */
    int ret_aggregate_size = (ret_stars == 0 && !ret_is_float && ret_is_struct) ? ret_base : 0;
    int ret_aggregate_float_class = 0;

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
    if (ret_aggregate_size > 0 && ret_struct_tag[0]) {
        HashMapEntry *hfa = hashmap_get(&p->global_struct_float_aggregate_classes, ret_struct_tag);
        if (hfa) ret_aggregate_float_class = hfa->val_int;
    }
    fn_node->aggregate_float_class = ret_aggregate_float_class;
    fn_node->is_float = ret_is_float && ret_stars == 0;
    fn_node->is_unsigned = (ret_stars == 0) ? p->last_type_unsigned : 0;
    fn_node->is_static = is_static;
    hashmap_put(&p->function_return_sizes, fn_node->name, nullptr, ret_size);
    hashmap_put(&p->function_return_unsigned, fn_node->name, nullptr, fn_node->is_unsigned);

    parser_enter_scope(p);

    if (strcmp(peek(p), "void") == 0 && p->pos + 1 < p->tokens.count && strcmp(p->tokens.data[p->pos + 1].text, ")") == 0) {
        take(p, "void");
    } else if (strcmp(peek(p), ")") != 0) {
        while (1) {
            if (strcmp(peek(p), "...") == 0) {
                take(p, "...");
                string_array_push(&fn_node->params, "...");
                int_array_push(&fn_node->param_aggregate_sizes, 0);
                int_array_push(&fn_node->param_aggregate_float_classes, 0);
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
            int pointee_is_unsigned = is_unsigned;

            int is_param_pointer = 0;
            while (strcmp(peek(p), "*") == 0) {
                param_elem_scale = base_size;
                take_star(p);
                base_size = p->target_scale;
                is_unsigned = 1;
                is_param_pointer = 1;
            }
            skip_type_qualifiers(p);
            const char *param_name = "";
            int is_func_ptr = 0;
            IntArray func_ptr_param_floats;
            int_array_init(&func_ptr_param_floats);
            int has_func_ptr_signature = 0;
            int func_ptr_return_float = base_is_float ? base_size : 0;
            if (strcmp(peek(p), "(") == 0) {
                take(p, "(");
                while (strcmp(peek(p), "*") == 0) {
                    take_star(p);
                }
                skip_type_qualifiers(p);
                if (is_alpha(peek(p)[0])) {
                    param_name = take(p, nullptr);
                } else {
                    char dummy_name[64];
                    static int dummy_counter = 0;
                    snprintf(dummy_name, sizeof(dummy_name), "__dummy_ptr_param_%d", dummy_counter++);
                    param_name = arena_strdup(p->arena, dummy_name);
                }
                take(p, ")");
                parse_function_pointer_param_floats(p, &func_ptr_param_floats);
                has_func_ptr_signature = 1;
                base_size = p->target_scale;
                param_elem_scale = p->target_scale;
                is_unsigned = 1;
                is_func_ptr = 1;
                is_param_pointer = 1;
            } else {
                if (is_alpha(peek(p)[0])) {
                    param_name = take(p, nullptr);
                } else {
                    char dummy_name[64];
                    static int dummy_counter = 0;
                    snprintf(dummy_name, sizeof(dummy_name), "__dummy_param_%d", dummy_counter++);
                    param_name = arena_strdup(p->arena, dummy_name);
                }
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
            if (is_param_pointer || is_param_arr) {
                hashmap_put(&p->pointer_pointee_unsigned, local_name_dup, nullptr, pointee_is_unsigned);
                ir_set_var_pointee_unsigned(local_name_dup, pointee_is_unsigned);
            }
            hashmap_put(&p->bool_vars, local_name_dup, nullptr, is_bool);
            hashmap_put(&p->float_vars, local_name_dup, nullptr, is_param_float);
            hashmap_put(&p->value_sizes, local_name_dup, nullptr, type_size);
            if (has_func_ptr_signature) {
                ir_set_function_pointer_signature(local_name_dup, &func_ptr_param_floats, func_ptr_return_float);
            }
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
                                     struct_tag[0];
            int_array_push(&fn_node->param_aggregate_sizes, is_param_aggregate ? base_size : 0);
            int param_hfa_class = 0;
            if (is_param_aggregate) {
                HashMapEntry *hfa = hashmap_get(&p->global_struct_float_aggregate_classes, struct_tag);
                if (hfa) param_hfa_class = hfa->val_int;
            }
            int_array_push(&fn_node->param_aggregate_float_classes, param_hfa_class);
            int_array_push(&fn_node->param_floats, is_param_float ? type_size : 0);

            if (strcmp(peek(p), ",") == 0) {
                take(p, ",");
                int_array_free(&func_ptr_param_floats);
            } else {
                int_array_free(&func_ptr_param_floats);
                break;
            }
        }
    }
    take(p, ")");

    skip_attribute_and_asm(p);

    if (strcmp(peek(p), ";") == 0) {
        take(p, ";");
        hashmap_put(&ir_function_return_aggregate_sizes, fn_node->name, nullptr, fn_node->aggregate_size);
        hashmap_put(&ir_function_return_aggregate_float_classes, fn_node->name, nullptr, fn_node->aggregate_float_class);
        IntArray *param_sizes = malloc(sizeof(IntArray));
        int_array_init(param_sizes);
        for (int p_i = 0; p_i < fn_node->param_aggregate_sizes.count; ++p_i) {
            int_array_push(param_sizes, fn_node->param_aggregate_sizes.data[p_i]);
        }
        hashmap_put(&ir_function_param_aggregate_sizes, fn_node->name, param_sizes, 0);

        IntArray *param_hfa = malloc(sizeof(IntArray));
        int_array_init(param_hfa);
        for (int p_i = 0; p_i < fn_node->param_aggregate_float_classes.count; ++p_i) {
            int_array_push(param_hfa, fn_node->param_aggregate_float_classes.data[p_i]);
        }
        hashmap_put(&ir_function_param_aggregate_float_classes, fn_node->name, param_hfa, 0);

        IntArray *param_fl = malloc(sizeof(IntArray));
        int_array_init(param_fl);
        for (int p_i = 0; p_i < fn_node->param_floats.count; ++p_i) {
            int_array_push(param_fl, fn_node->param_floats.data[p_i]);
        }
        hashmap_put(&ir_function_param_floats, fn_node->name, param_fl, 0);
        hashmap_put(&ir_function_return_floats, fn_node->name, nullptr,
                    fn_node->is_float ? (int)fn_node->value : 0);
        hashmap_put(&ir_function_return_int_sizes, fn_node->name, nullptr,
                    (!fn_node->is_float && fn_node->aggregate_size == 0) ? (int)fn_node->value : 0);

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
    int saved_alignment = p->last_type_alignment;
    int saved_thread_local = p->last_type_thread_local;
    int base_size = parse_base_type(p);
    if (p->last_type_alignment < saved_alignment) {
        p->last_type_alignment = saved_alignment;
    }
    if (p->last_type_thread_local == 0) {
        p->last_type_thread_local = saved_thread_local;
    }
    if (!struct_tag[0] && p->last_parsed_struct_tag && p->last_parsed_struct_tag[0]) {
        struct_tag = p->last_parsed_struct_tag;
    }
    int is_unsigned = p->last_type_unsigned;
    int is_bool = p->last_type_bool;
    int base_const = p->last_type_const;
    int base_volatile = p->last_type_volatile;
    int base_float = p->last_type_float;

    while (strcmp(peek(p), "static") == 0 || strcmp(peek(p), "extern") == 0) {
        if (strcmp(peek(p), "static") == 0) {
            take(p, "static");
            is_static = 1;
        } else {
            take(p, "extern");
            is_extern = 1;
        }
    }

    if (strcmp(peek(p), ";") == 0) {
        take(p, ";");
        return;
    }

    while (1) {
        int stars = 0;
        while (strcmp(peek(p), "*") == 0) {
            take_star(p);
            stars++;
        }
        p->last_type_const = 0;
        p->last_type_volatile = 0;
        skip_type_qualifiers(p);
        int is_const = (stars == 0) ? (base_const || p->last_type_const) : p->last_type_const;
        int is_volatile = (stars == 0) ? (base_volatile || p->last_type_volatile) : p->last_type_volatile;
        int type_size = (stars > 0) ? p->target_scale : base_size;
        int is_unsigned_var = (stars > 0) ? 1 : is_unsigned;
        int is_func_ptr = 0;
        LongArray func_ptr_dims;
        long_array_init(&func_ptr_dims);
        const char *name = "";
        if (strcmp(peek(p), "(") == 0 &&
            p->pos + 1 < p->tokens.count &&
            strcmp(p->tokens.data[p->pos + 1].text, "*") == 0) {
            take(p, "(");
            take_star(p);
            skip_type_qualifiers(p);
            name = take(p, nullptr);
            while (strcmp(peek(p), "[") == 0) {
                take(p, "[");
                long dim = 0;
                if (strcmp(peek(p), "]") != 0) {
                    Node *dim_expr = expr(p);
                    dim = eval_const(p, dim_expr);
                }
                take(p, "]");
                long_array_push(&func_ptr_dims, dim);
            }
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
            is_func_ptr = 1;
            type_size = p->target_scale;
            is_unsigned_var = 1;
        } else {
            name = take(p, nullptr);
        }
        skip_attribute_and_asm(p);

        long total_arr_count = 1;
        LongArray arr_dims;
        long_array_init(&arr_dims);
        for (int fd_i = 0; fd_i < func_ptr_dims.count; ++fd_i) {
            long_array_push(&arr_dims, func_ptr_dims.data[fd_i]);
            if (func_ptr_dims.data[fd_i] > 0) {
                total_arr_count *= func_ptr_dims.data[fd_i];
            }
        }
        long_array_free(&func_ptr_dims);
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
        skip_attribute_and_asm(p);

        hashmap_put(&p->unsigned_vars, name, nullptr, is_unsigned_var);
        if (stars > 0) {
            hashmap_put(&p->pointer_pointee_unsigned, name, nullptr, is_unsigned);
            ir_set_var_pointee_unsigned(name, is_unsigned);
        }
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

        int elem_size = (stars > 0 || is_func_ptr) ? p->target_scale : base_size;
        long global_size = (arr_dims.count > 0) ? total_arr_count : 1;
        if (struct_tag[0] && stars == 0) {
            elem_size = 1;
            global_size = type_size;
        }
        int is_storage_array = arr_dims.count > 0 || (struct_tag[0] && stars == 0);
        ir_declare_global(name, is_storage_array, global_size, is_static, is_extern, elem_size, p->target_scale);
        if (p->last_type_thread_local) {
            ir_set_global_thread_local(name, 1);
        }
        ir_set_global_var_is_pointer(name, stars > 0 || is_func_ptr);
        ir_set_global_var_elem_scale(name, is_func_ptr ? p->target_scale : (stars == 1 ? base_size : (stars > 1 ? p->target_scale : elem_size)));
        if (arr_dims.count > 0 && stars == 0) {
            ir_set_var_pointee_unsigned(name, is_unsigned);
        }
        int alignment = alignof_type(p, base_size, struct_tag, stars);
        if (p->last_type_alignment > alignment) {
            alignment = p->last_type_alignment;
        }
        int align_exp = 0;
        int temp_align = alignment;
        while (temp_align > 1) {
            temp_align >>= 1;
            align_exp++;
        }
        ir_set_global_align(name, align_exp);

        if (arr_dims.count > 0) {
            ir_set_global_array_dims(name, arr_dims);
            ir_set_global_array_base_size(name, (struct_tag[0] && stars == 0) ? base_size : elem_size);
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
                    long original_dim0 = (arr_dims.count > 0) ? arr_dims.data[0] : -1;
                    InitElementArray inits;
                    init_element_array_init(&inits);
                    const char *init_struct_tag = (stars > 0) ? "" : struct_tag;
                    parse_aggregate_init(p, 0, init_struct_tag, &arr_dims, 0, (stars > 0) ? p->target_scale : base_size, &inits);
                    
                    if (original_dim0 == 0) {
                        long total_elements = 1;
                        for (int d_i = 0; d_i < arr_dims.count; ++d_i) {
                            if (arr_dims.data[d_i] > 0) {
                                total_elements *= arr_dims.data[d_i];
                            }
                        }
                        global_size = total_elements;
                        if (struct_tag[0] && stars == 0) {
                            type_size = (int)(total_elements * base_size);
                            ir_declare_global(name, 1, type_size, is_static, is_extern, 1, p->target_scale);
                        } else {
                            type_size = (int)(total_elements * elem_size);
                            ir_declare_global(name, 1, global_size, is_static, is_extern, elem_size, p->target_scale);
                        }
                        ir_set_global_array_dims(name, arr_dims);
                        ir_set_global_array_base_size(name, (struct_tag[0] && stars == 0) ? base_size : elem_size);
                        hashmap_put(&p->value_sizes, name, nullptr, type_size);
                    }
                    
                    LongArray init_vals;
                    long_array_init(&init_vals);
                    IntArray is_string;
                    int_array_init(&is_string);
                    StringPairArray strings;
                    string_pair_array_init(&strings);

                    if (struct_tag[0] && stars == 0) {
                        char *byte_buf = calloc(type_size + 1, 1);
                        int *byte_is_str = calloc(type_size + 1, sizeof(int));
                        int *byte_str_idx = calloc(type_size + 1, sizeof(int));
                        for (int k = 0; k < inits.count; ++k) {
                            Node *init_val = inits.data[k].val;
                            if (init_val->op_enum == OP_CAST && init_val->lhs && init_val->lhs->op_enum == OP_STR) {
                                init_val = init_val->lhs;
                            }
                            char addr_ref[512];
                            if (init_val->op_enum == OP_STR) {
                                char label[256];
                                snprintf(label, sizeof(label), ".Lstr_glob_%s_%d", name, strings.count);
                                StringPair sp;
                                sp.first = arena_strdup(p->arena, label);
                                sp.second = arena_strdup(p->arena, init_val->name);
                                string_pair_array_push(&strings, &sp);
                                int str_idx = strings.count - 1;
                                if (inits.data[k].offset < type_size) {
                                    byte_is_str[inits.data[k].offset] = 1;
                                    byte_str_idx[inits.data[k].offset] = str_idx;
                                }
                            } else if (format_address_ref(p, init_val, addr_ref, sizeof(addr_ref))) {
                                StringPair sp;
                                sp.first = arena_strdup(p->arena, addr_ref);
                                sp.second = nullptr;
                                string_pair_array_push(&strings, &sp);
                                int sym_idx = strings.count - 1;
                                if (inits.data[k].offset < type_size) {
                                    byte_is_str[inits.data[k].offset] = 1;
                                    byte_str_idx[inits.data[k].offset] = sym_idx;
                                }
                            } else {
                                long val = eval_const(p, inits.data[k].val);
                                for (int b = 0; b < inits.data[k].size; ++b) {
                                    if (inits.data[k].offset + b < type_size) {
                                        byte_buf[inits.data[k].offset + b] = (val >> (b * 8)) & 0xff;
                                    }
                                }
                            }
                        }
                        for (long b = 0; b < type_size; ++b) {
                            if (byte_is_str[b]) {
                                long_array_push(&init_vals, byte_str_idx[b]);
                                int_array_push(&is_string, 1);
                            } else {
                                long_array_push(&init_vals, byte_buf[b]);
                                int_array_push(&is_string, 0);
                            }
                        }
                        free(byte_buf);
                        free(byte_is_str);
                        free(byte_str_idx);
                    } else {
                        for (long offset = 0; offset < type_size; offset += elem_size) {
                            long val = 0;
                            int element_is_str = 0;
                            int str_idx = 0;
                            for (int k = 0; k < inits.count; ++k) {
                                if (inits.data[k].offset == offset) {
                                    Node *init_val = inits.data[k].val;
                                    if (init_val->op_enum == OP_CAST && init_val->lhs && init_val->lhs->op_enum == OP_STR) {
                                        init_val = init_val->lhs;
                                    }
                                    char addr_ref[512];
                                    if (init_val->op_enum == OP_STR) {
                                        char label[256];
                                        snprintf(label, sizeof(label), ".Lstr_glob_%s_%d", name, strings.count);
                                        StringPair sp;
                                        sp.first = arena_strdup(p->arena, label);
                                        sp.second = arena_strdup(p->arena, init_val->name);
                                        string_pair_array_push(&strings, &sp);
                                        str_idx = strings.count - 1;
                                        element_is_str = 1;
                                    } else if (format_address_ref(p, init_val, addr_ref, sizeof(addr_ref))) {
                                        StringPair sp;
                                        sp.first = arena_strdup(p->arena, addr_ref);
                                        sp.second = nullptr;
                                        string_pair_array_push(&strings, &sp);
                                        str_idx = strings.count - 1;
                                        element_is_str = 1;
                                    } else {
                                        val = eval_const(p, inits.data[k].val);
                                    }
                                    break;
                                }
                            }
                            if (element_is_str) {
                                long_array_push(&init_vals, str_idx);
                                int_array_push(&is_string, 1);
                            } else {
                                long_array_push(&init_vals, val);
                                int_array_push(&is_string, 0);
                            }
                        }
                    }
                    init_element_array_free(&inits);
                    ir_set_global_initializers_with_strings(name, init_vals, is_string, strings);
                    long_array_free(&init_vals);
                    int_array_free(&is_string);
                    string_pair_array_free(&strings);
                }
            } else if (arr_dims.count > 0 && base_size == 1 && stars == 0 && peek(p)[0] == '"') {
                LongArray init_vals;
                long_array_init(&init_vals);
                while (peek(p)[0] == '"') {
                    append_string_literal_bytes(p, &init_vals, take(p, nullptr));
                }
                long_array_push(&init_vals, 0);
                if (arr_dims.data[0] == 0) {
                    arr_dims.data[0] = init_vals.count;
                    global_size = init_vals.count;
                    type_size = (int)global_size;
                    ir_declare_global(name, 1, global_size, is_static, is_extern, 1, p->target_scale);
                    ir_set_global_array_dims(name, arr_dims);
                    hashmap_put(&p->value_sizes, name, nullptr, type_size);
                }
                ir_set_global_initializers(name, init_vals);
                long_array_free(&init_vals);
            } else {
                Node *val_node = expr(p);
                LongArray init_vals;
                long_array_init(&init_vals);
                int fp_init = base_float && stars == 0;
                double fp_d = 0.0;
                int got_fp = 0;
                if (fp_init && val_node->op_enum == OP_FNUM) {
                    fp_d = val_node->fvalue; got_fp = 1;
                } else if (fp_init && val_node->op_enum == OP_UNARY_MINUS &&
                           val_node->lhs && val_node->lhs->op_enum == OP_FNUM) {
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
                    IntArray is_string;
                    int_array_init(&is_string);
                    int_array_push(&is_string, 0);
                    StringPairArray strings;
                    string_pair_array_init(&strings);
                    ir_set_global_initializers_with_strings(name, init_vals, is_string, strings);
                    int_array_free(&is_string);
                    string_pair_array_free(&strings);
                } else if (val_node->op_enum == OP_STR ||
                           ((stars > 0 || is_func_ptr) && val_node->op_enum == OP_CAST && val_node->lhs && val_node->lhs->op_enum == OP_STR)) {
                    Node *str_node = val_node->op_enum == OP_STR ? val_node : val_node->lhs;
                    char label[256];
                    snprintf(label, sizeof(label), ".Lstr_glob_%s_0", name);
                    StringPair sp;
                    sp.first = arena_strdup(p->arena, label);
                    sp.second = arena_strdup(p->arena, str_node->name);
                    StringPairArray strings;
                    string_pair_array_init(&strings);
                    string_pair_array_push(&strings, &sp);
                    
                    long_array_push(&init_vals, 0);
                    IntArray is_string;
                    int_array_init(&is_string);
                    int_array_push(&is_string, 1);
                    ir_set_global_initializers_with_strings(name, init_vals, is_string, strings);
                    int_array_free(&is_string);
                    string_pair_array_free(&strings);
                } else if (stars > 0 || is_func_ptr) {
                    char addr_ref[512];
                    if (!format_address_ref(p, val_node, addr_ref, sizeof(addr_ref))) {
                        long_array_push(&init_vals, eval_const(p, val_node));
                        ir_set_global_initializers(name, init_vals);
                    } else {
                    StringPair sp;
                    sp.first = arena_strdup(p->arena, addr_ref);
                    sp.second = nullptr;
                    StringPairArray strings;
                    string_pair_array_init(&strings);
                    string_pair_array_push(&strings, &sp);

                    long_array_push(&init_vals, 0);
                    IntArray is_string;
                    int_array_init(&is_string);
                    int_array_push(&is_string, 1);
                    ir_set_global_initializers_with_strings(name, init_vals, is_string, strings);
                    int_array_free(&is_string);
                    string_pair_array_free(&strings);
                    }
                } else {
                    long_array_push(&init_vals, eval_const(p, val_node));
                    ir_set_global_initializers(name, init_vals);
                }
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
    hashmap_free(&state->pointer_pointee_unsigned);
    hashmap_free(&state->var_struct_tags);

    hashmap_free(&state->global_typedefs);
    hashmap_free(&state->global_typedef_sizes);
    hashmap_free(&state->global_typedef_unsigned);
    hashmap_free(&state->global_typedef_float);
    hashmap_free(&state->global_typedef_bool);
    hashmap_free(&state->function_return_sizes);
    hashmap_free(&state->function_return_unsigned);
    hashmap_free(&state->global_typedef_struct_tags);
    hashmap_free(&state->global_typedef_dims);
    hashmap_free(&state->global_enums);
    hashmap_free(&state->constexpr_vars);
    
    for (int b = 0; b < state->global_structs.bucket_count; ++b) {
        HashMapEntry *curr = &state->global_structs.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            HashMap *fields = (HashMap *)curr->val_ptr;
            hashmap_free(fields);
            free(fields);
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
    hashmap_free(&state->global_struct_field_float_sizes_by_tag);
    hashmap_free(&state->global_struct_field_elem_sizes_by_tag);
    hashmap_free(&state->global_struct_field_is_pointer_by_tag);
    hashmap_free(&state->global_struct_field_unsigned_by_tag);
    hashmap_free(&state->global_struct_field_total_sizes_by_tag);
    hashmap_free(&state->global_struct_field_bit_offsets_by_tag);
    hashmap_free(&state->global_struct_field_bit_widths_by_tag);
    hashmap_free(&state->global_struct_float_aggregate_classes);

    for (int b = 0; b < state->global_struct_field_dims_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state->global_struct_field_dims_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            LongArray *arr = (LongArray *)curr->val_ptr;
            long_array_free(arr);
            free(arr);
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
    hashmap_init(&state.pointer_pointee_unsigned, 64);
    hashmap_init(&state.var_struct_tags, 64);
    state.local_var_counter = 0;
    state.last_type_unsigned = 0;
    state.last_type_bool = 0;

    hashmap_init(&state.global_typedefs, 64);
    hashmap_init(&state.global_typedef_sizes, 64);
    hashmap_init(&state.global_typedef_unsigned, 64);
    hashmap_init(&state.global_typedef_float, 64);
    hashmap_init(&state.global_typedef_bool, 64);
    hashmap_init(&state.function_return_sizes, 128);
    hashmap_init(&state.function_return_unsigned, 128);
    hashmap_init(&state.global_typedef_struct_tags, 64);
    hashmap_init(&state.global_typedef_dims, 64);
    hashmap_put(&state.global_typedefs, "__builtin_va_list", nullptr, 1);
    hashmap_put(&state.global_typedef_sizes, "__builtin_va_list", nullptr, state.target_scale);
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
    hashmap_init(&state.global_struct_field_float_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_elem_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_is_pointer_by_tag, 64);
    hashmap_init(&state.global_struct_field_unsigned_by_tag, 64);
    hashmap_init(&state.global_struct_field_total_sizes_by_tag, 64);
    hashmap_init(&state.global_struct_field_dims_by_tag, 64);
    hashmap_init(&state.global_struct_field_bit_offsets_by_tag, 64);
    hashmap_init(&state.global_struct_field_bit_widths_by_tag, 64);
    hashmap_init(&state.global_struct_float_aggregate_classes, 64);

    state.last_parsed_struct_tag = "";
    state.arena = arena;

    NodeArray funcs;
    node_array_init(&funcs);

    while (strcmp(peek(&state), "EOF") != 0) {
        state.last_type_alignment = 0;
        state.last_type_thread_local = 0;
        if (strcmp(peek(&state), "__asm__") == 0 || strcmp(peek(&state), "__asm") == 0 || strcmp(peek(&state), "asm") == 0) {
            ir_add_global_asm(parse_gnu_asm(&state, 1));
            continue;
        }
        int is_static = 0;
        int is_extern = 0;
        int is_constexpr = 0;
        while (strcmp(peek(&state), "static") == 0 || strcmp(peek(&state), "extern") == 0 || strcmp(peek(&state), "inline") == 0 || strcmp(peek(&state), "__inline") == 0 || strcmp(peek(&state), "__inline__") == 0 || strcmp(peek(&state), "_Thread_local") == 0 || strcmp(peek(&state), "thread_local") == 0 || strcmp(peek(&state), "_Noreturn") == 0 || strcmp(peek(&state), "noreturn") == 0 || strcmp(peek(&state), "constexpr") == 0 ||
               strcmp(peek(&state), "__attribute__") == 0 || strcmp(peek(&state), "__attribute") == 0 ||
               strcmp(peek(&state), "_Alignas") == 0 || strcmp(peek(&state), "alignas") == 0 ||
               (strcmp(peek(&state), "[") == 0 && state.pos + 1 < state.tokens.count && strcmp(state.tokens.data[state.pos + 1].text, "[") == 0) ||
               is_calling_convention_qualifier(peek(&state))) {
            if (strcmp(peek(&state), "static") == 0) {
                take(&state, "static");
                is_static = 1;
            } else if (strcmp(peek(&state), "extern") == 0) {
                take(&state, "extern");
                is_extern = 1;
            } else if (strcmp(peek(&state), "constexpr") == 0) {
                take(&state, "constexpr");
                is_constexpr = 1;
            } else if (strcmp(peek(&state), "_Thread_local") == 0 || strcmp(peek(&state), "thread_local") == 0) {
                take(&state, nullptr);
                state.last_type_thread_local = 1;
            } else if (strcmp(peek(&state), "_Alignas") == 0 || strcmp(peek(&state), "alignas") == 0) {
                take(&state, nullptr);
                int align_val = 0;
                if (paren_starts_type_name(&state)) {
                    take(&state, "(");
                    const char *s_tag = "";
                    if (strcmp(peek(&state), "struct") == 0 || strcmp(peek(&state), "union") == 0) {
                        if (state.pos + 1 < state.tokens.count && strcmp(state.tokens.data[state.pos + 1].text, "*") != 0 && strcmp(state.tokens.data[state.pos + 1].text, ")") != 0) {
                            s_tag = state.tokens.data[state.pos + 1].text;
                        }
                    }
                    int base_size = parse_base_type(&state);
                    int stars = 0;
                    while (strcmp(peek(&state), "*") == 0) {
                        take_star(&state);
                        stars++;
                        base_size = state.target_scale;
                    }
                    take(&state, ")");
                    align_val = alignof_type(&state, base_size, s_tag, stars);
                } else {
                    take(&state, "(");
                    Node *expr_node = expr(&state);
                    take(&state, ")");
                    align_val = (int)eval_const(&state, expr_node);
                }
                if (align_val > state.last_type_alignment) {
                    state.last_type_alignment = align_val;
                }
            } else if (strcmp(peek(&state), "__attribute__") == 0 || strcmp(peek(&state), "__attribute") == 0 || strcmp(peek(&state), "[") == 0) {
                skip_attribute(&state);
            } else if (is_calling_convention_qualifier(peek(&state))) {
                skip_calling_convention_qualifiers(&state);
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
            while (1) {
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
                        alias_size = state.target_scale;
                        alias_struct_tag = "";
                    }
                    LongArray *alias_dims = nullptr;
                    while (strcmp(peek(&state), "[") == 0) {
                        take(&state, "[");
                        if (strcmp(peek(&state), "]") != 0) {
                            Node *dim_expr = expr(&state);
                            long dim = eval_const(&state, dim_expr);
                            if (dim > 0) {
                                alias_size = (int)(alias_size * dim);
                                if (!alias_dims) {
                                    alias_dims = arena_alloc(state.arena, sizeof(LongArray));
                                    long_array_init(alias_dims);
                                }
                                long_array_push(alias_dims, dim);
                            }
                        }
                        take(&state, "]");
                    }
                    if (alias_dims) {
                        hashmap_put(&state.global_typedef_dims, alias, alias_dims, 0);
                    }
                }
                if ((!alias_struct_tag || !alias_struct_tag[0]) && typedef_stars == 0 && hashmap_has(&state.global_structs, alias)) {
                    alias_struct_tag = alias;
                    HashMapEntry *struct_size = hashmap_get(&state.global_struct_sizes, alias);
                    if (struct_size) {
                        alias_size = struct_size->val_int;
                    }
                }
                hashmap_put(&state.global_typedefs, alias, nullptr, 1);
                hashmap_put(&state.global_typedef_sizes, alias, nullptr, alias_size);
                hashmap_put(&state.global_typedef_unsigned, alias, nullptr, typedef_stars == 0 ? state.last_type_unsigned : 0);
                hashmap_put(&state.global_typedef_float, alias, nullptr, typedef_stars == 0 ? state.last_type_float : 0);
                hashmap_put(&state.global_typedef_bool, alias, nullptr, typedef_stars == 0 ? state.last_type_bool : 0);
                if (alias_struct_tag && alias_struct_tag[0]) {
                    hashmap_put(&state.global_typedef_struct_tags, alias, (void *)alias_struct_tag, 0);
                }
                if (strcmp(peek(&state), ",") == 0) {
                    take(&state, ",");
                    continue;
                }
                break;
            }
            take(&state, ";");
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
        HashMapEntry *curr = &state.unsigned_vars.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_var_unsigned(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.bool_vars.bucket_count; ++b) {
        HashMapEntry *curr = &state.bool_vars.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_var_bool(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.value_sizes.bucket_count; ++b) {
        HashMapEntry *curr = &state.value_sizes.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_var_type_size(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.float_vars.bucket_count; ++b) {
        HashMapEntry *curr = &state.float_vars.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_var_float(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.var_struct_tags.bucket_count; ++b) {
        HashMapEntry *curr = &state.var_struct_tags.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_var_struct_tag(curr->key, (const char *)curr->val_ptr);
        }
    }

    // copy struct offsets/sizes for IR
    for (int b = 0; b < state.global_struct_field_offsets_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_offsets_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_offset(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.global_struct_field_sizes_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_sizes_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_size(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.global_struct_field_total_sizes_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_total_sizes_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_total_size(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.global_struct_field_dims_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_dims_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            LongArray *dims = (LongArray *)curr->val_ptr;
            ir_set_struct_field_dims(curr->key, *dims);
        }
    }
    for (int b = 0; b < state.global_struct_field_tags.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_tags.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_tag(curr->key, (const char *)curr->val_ptr);
        }
    }
    for (int b = 0; b < state.global_struct_field_bit_offsets_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_bit_offsets_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_bit_offset(curr->key, curr->val_int);
        }
    }
    for (int b = 0; b < state.global_struct_field_bit_widths_by_tag.bucket_count; ++b) {
        HashMapEntry *curr = &state.global_struct_field_bit_widths_by_tag.entries[b];
        if (curr->key && curr->key != TOMBSTONE) {
            ir_set_struct_field_bit_width(curr->key, curr->val_int);
        }
    }

    parser_state_cleanup(&state);
    return funcs;
}

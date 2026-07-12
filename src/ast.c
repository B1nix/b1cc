#include "ast.h"
#include <string.h>

void *realloc(void *ptr, size_t size);
void free(void *ptr);

typedef struct OpMapping {
    const char *str;
    OpKind kind;
} OpMapping;

static const OpMapping op_table[] = {
    {"num", OP_NUM}, {"fnum", OP_FNUM}, {"str", OP_STR}, {"char", OP_CHAR},
    {"var", OP_VAR},
    {"unary_-", OP_UNARY_MINUS}, {"unary_~", OP_UNARY_TILDE},
    {"unary_!", OP_UNARY_NOT}, {"unary_&", OP_UNARY_ADDR}, {"unary_*", OP_UNARY_DEREF},
    {"prefix_++", OP_PREFIX_INC}, {"prefix_--", OP_PREFIX_DEC},
    {"postfix_++", OP_POSTFIX_INC}, {"postfix_--", OP_POSTFIX_DEC},
    {"+", OP_ADD}, {"-", OP_SUB}, {"*", OP_MUL}, {"/", OP_DIV}, {"%", OP_MOD},
    {"==", OP_EQ}, {"!=", OP_NE}, {"<", OP_LT}, {">", OP_GT}, {"<=", OP_LE}, {">=", OP_GE},
    {"&", OP_BAND}, {"|", OP_BOR}, {"^", OP_BXOR}, {"<<", OP_SHL}, {">>", OP_SHR},
    {"&&", OP_LAND}, {"||", OP_LOR}, {",", OP_COMMA},
    {"?", OP_TERNARY},
    {"assign", OP_ASSIGN}, {"store_index", OP_STORE_INDEX},
    {"cast", OP_CAST}, {"bool_cast", OP_BOOL_CAST},
    {"call", OP_CALL}, {"return", OP_RETURN}, {"func", OP_FUNC},
    {"if", OP_IF}, {"while", OP_WHILE}, {"do_while", OP_DO_WHILE}, {"for", OP_FOR},
    {"break", OP_BREAK}, {"continue", OP_CONTINUE},
    {"switch", OP_SWITCH}, {"case", OP_CASE}, {"default", OP_DEFAULT},
    {"goto", OP_GOTO}, {"label_stmt", OP_LABEL_STMT},
    {"decl", OP_DECL}, {"array_decl", OP_ARRAY_DECL},
    {"expr", OP_EXPR}, {"block", OP_BLOCK}, {"empty", OP_EMPTY},
    {"index", OP_INDEX}, {"member", OP_MEMBER}, {"member_ptr", OP_MEMBER_PTR},
    {"asm", OP_ASM},
    {"complex_real", OP_COMPLEX_REAL}, {"complex_imag", OP_COMPLEX_IMAG},
    {"compound_literal", OP_COMPOUND_LITERAL}, {"init_item", OP_INIT_ITEM},
    {"sizeof", OP_SIZEOF},
    {"va_start", OP_VA_START}, {"va_arg", OP_VA_ARG}, {"va_copy", OP_VA_COPY},
    {nullptr, OP_COUNT}
};

OpKind str_to_op(const char *s) {
    if (!s) return OP_COUNT;
    for (const OpMapping *m = op_table; m->str; ++m) {
        if (strcmp(s, m->str) == 0) return m->kind;
    }
    return OP_COUNT;
}

const char *op_to_str(OpKind op) {
    for (const OpMapping *m = op_table; m->str; ++m) {
        if (m->kind == op) return m->str;
    }
    return "?";
}

void token_array_init(TokenArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void token_array_push(TokenArray *arr, const Token *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Token));
    }
    arr->data[arr->count].text = val->text;
    arr->data[arr->count].line = val->line;
    arr->data[arr->count].col = val->col;
    arr->count = arr->count + 1;
}

void token_array_free(TokenArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void node_array_init(NodeArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void node_array_push(NodeArray *arr, Node *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Node *));
    }
    arr->data[arr->count] = val;
    arr->count = arr->count + 1;
}

void node_array_free(NodeArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

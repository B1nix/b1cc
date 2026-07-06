#ifndef AST_H
#define AST_H

#include "common.h"

typedef enum OpKind {
    OP_NUM, OP_FNUM, OP_STR, OP_CHAR,
    OP_VAR,
    OP_UNARY_MINUS, OP_UNARY_TILDE, OP_UNARY_NOT, OP_UNARY_ADDR, OP_UNARY_DEREF,
    OP_PREFIX_INC, OP_PREFIX_DEC, OP_POSTFIX_INC, OP_POSTFIX_DEC,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
    OP_LAND, OP_LOR, OP_COMMA,
    OP_TERNARY,
    OP_ASSIGN, OP_STORE_INDEX,
    OP_CAST, OP_BOOL_CAST,
    OP_CALL, OP_RETURN, OP_FUNC,
    OP_IF, OP_WHILE, OP_DO_WHILE, OP_FOR,
    OP_BREAK, OP_CONTINUE, OP_SWITCH, OP_CASE, OP_DEFAULT,
    OP_GOTO, OP_LABEL_STMT,
    OP_DECL, OP_ARRAY_DECL, OP_EXPR, OP_BLOCK, OP_EMPTY,
    OP_INDEX, OP_MEMBER, OP_MEMBER_PTR,
    OP_ASM,
    OP_COMPOUND_LITERAL, OP_INIT_ITEM,
    OP_SIZEOF,
    OP_VA_START, OP_VA_ARG, OP_VA_COPY,
    OP_COUNT
} OpKind;

const char *op_to_str(OpKind op);
OpKind str_to_op(const char *s);

typedef struct Token {
    const char *text;
    int line;
    int col;
} Token;

typedef struct TokenArray {
    Token *data;
    int count;
    int capacity;
} TokenArray;

void token_array_init(TokenArray *arr);
void token_array_push(TokenArray *arr, const Token *val);
void token_array_free(TokenArray *arr);

typedef struct Node Node;

typedef struct NodeArray {
    Node **data;
    int count;
    int capacity;
} NodeArray;

void node_array_init(NodeArray *arr);
void node_array_push(NodeArray *arr, Node *val);
void node_array_free(NodeArray *arr);

struct Node {
    const char *op;
    int op_enum;
    const char *name;
    long value;
    int is_static;
    Node *lhs;
    Node *rhs;
    NodeArray body;
    StringArray params;
    IntArray param_aggregate_sizes;
    IntArray param_aggregate_float_classes;
    IntArray param_floats;
    int aggregate_size;
    int aggregate_float_class;
    const char *type_tag;
    LongArray array_dims;
    int elem_size;
    int pointee_size;
    int pointee_unsigned;
    int pointee_unsigned_known;
    int is_unsigned;
    int compare_unsigned;
    int type_size;
    int is_bool;
    int is_float;     /* node yields a floating-point (float/double) value */
    double fvalue;    /* literal value when op == "fnum" */
    int bit_offset;
    int bit_width;
    int alignment;
    Node *vla_dim_expr;
    int line;
    int col;
};

#endif // AST_H

#ifndef AST_H
#define AST_H

#include "common.h"

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

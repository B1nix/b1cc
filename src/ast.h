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
    int aggregate_size;
    const char *type_tag;
    LongArray array_dims;
    int elem_size;
    int pointee_size;
    int is_unsigned;
    int type_size;
    int is_bool;
    int bit_offset;
    int bit_width;
    int line;
    int col;
};

#endif // AST_H

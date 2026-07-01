#include "ast.h"

void *realloc(void *ptr, size_t size);
void free(void *ptr);

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

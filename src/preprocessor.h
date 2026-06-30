#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include "common.h"

typedef struct Macro {
    int is_function_like;
    StringArray params;
    const char *body;
} Macro;

typedef struct CondState {
    int condition_met;
    int active;
} CondState;

typedef struct CondStateArray {
    CondState *data;
    int count;
    int capacity;
} CondStateArray;

void cond_state_array_init(CondStateArray *arr);
void cond_state_array_push(CondStateArray *arr, CondState val);
void cond_state_array_free(CondStateArray *arr);

extern StringArray preprocessor_driver_include_dirs;
extern HashMap preprocessor_driver_macros;

const char *preprocessor_preprocess(const char *src, const char *filepath, StringArray *include_dirs, HashMap *macros, HashMap *included_files, Arena *arena);

#endif // PREPROCESSOR_H

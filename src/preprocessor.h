#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include "common.h"

typedef struct Macro {
    bool is_function_like;
    StringArray params;
    const char *body;
} Macro;

typedef struct CondState {
    bool condition_met;
    bool active;
} CondState;

typedef struct CondStateArray {
    CondState *data;
    int count;
    int capacity;
} CondStateArray;

void cond_state_array_init(CondStateArray *arr);
void cond_state_array_push(CondStateArray *arr, const CondState *val);
void cond_state_array_free(CondStateArray *arr);

extern StringArray preprocessor_driver_include_dirs;
extern HashMap preprocessor_driver_macros;
extern int preprocessor_current_line;
extern const char *preprocessor_current_file;
extern int preprocessor_counter;

const char *preprocessor_preprocess(const char *src, const char *filepath, StringArray *include_dirs, HashMap *macros, HashMap *included_files, Arena *arena);

#endif // PREPROCESSOR_H

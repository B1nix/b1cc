#ifndef IR_H
#define IR_H

#include "ast.h"
#include "common.h"

typedef struct {
    const char *name;
    int is_array;
    long size;
    LongArray initializers;
    int is_static;
    int is_extern;
    int elem_size;
    int align;
} IrGlobalVar;

typedef struct {
    IrGlobalVar *data;
    int count;
    int capacity;
} IrGlobalVarArray;

void ir_global_var_array_init(IrGlobalVarArray *arr);
void ir_global_var_array_push(IrGlobalVarArray *arr, IrGlobalVar val);
void ir_global_var_array_free(IrGlobalVarArray *arr);

typedef struct {
    const char *op;
    const char *arg;
    long value;
} IrInst;

typedef struct {
    IrInst *data;
    int count;
    int capacity;
} IrInstArray;

void ir_inst_array_init(IrInstArray *arr);
void ir_inst_array_push(IrInstArray *arr, IrInst val);
void ir_inst_array_free(IrInstArray *arr);

typedef struct {
    const char *first;
    const char *second;
} StringPair;

typedef struct {
    StringPair *data;
    int count;
    int capacity;
} StringPairArray;

void string_pair_array_init(StringPairArray *arr);
void string_pair_array_push(StringPairArray *arr, StringPair val);
void string_pair_array_free(StringPairArray *arr);

typedef struct {
    const char *name;
    StringArray params;
    IntArray param_aggregate_sizes;
    IrInstArray code;
    HashMap locals;
    StringPairArray strings;
    int has_call;
    int label_id;
    int is_static;
    int return_aggregate_size;
} IrFunction;

typedef struct {
    IrFunction *data;
    int count;
    int capacity;
} IrFunctionArray;

void ir_function_array_init(IrFunctionArray *arr);
void ir_function_array_push(IrFunctionArray *arr, IrFunction val);
void ir_function_array_free(IrFunctionArray *arr);

typedef struct {
    const char *break_label;
    const char *continue_label;
} LoopContext;

void ir_reset_state(void);
void ir_declare_global(const char *name, int is_array, long size, int is_static, int is_extern, int elem_size, int target_scale);
void ir_mark_global_struct(const char *name);
void ir_set_global_array_dims(const char *name, LongArray dims);
void ir_set_global_initializers(const char *name, LongArray inits);
void ir_set_global_array_base_size(const char *name, int val);
void ir_set_local_array_dims(const char *name, LongArray dims);
void ir_set_local_array_base_size(const char *name, int val);
int ir_get_global_array_base_size(const char *name);
int ir_get_local_array_base_size(const char *name);
void ir_set_local_var_is_pointer(const char *name, int val);
void ir_set_local_var_elem_scale(const char *name, int val);
int ir_get_local_var_elem_scale(const char *name);
int ir_get_global_var_elem_scale(const char *name);
void ir_set_global_var_is_pointer(const char *name, int val);
void ir_set_global_var_elem_scale(const char *name, int val);


void ir_set_var_unsigned(const char *name, int val);
void ir_set_var_bool(const char *name, int val);
void ir_set_var_type_size(const char *name, int val);
void ir_set_var_struct_tag(const char *name, const char *tag);

void ir_set_struct_field_offset(const char *key, int val);
void ir_set_struct_field_size(const char *key, int val);
void ir_set_struct_field_total_size(const char *key, int val);
void ir_set_struct_field_dims(const char *key, LongArray dims);
void ir_set_struct_field_tag(const char *key, const char *tag);

int ir_get_var_unsigned(const char *name);
int ir_get_var_bool(const char *name);
int ir_get_var_type_size(const char *name);
const char *ir_get_var_struct_tag(const char *name);
int ir_get_struct_field_offset(const char *key);
int ir_get_struct_field_size(const char *key);
int ir_get_struct_field_total_size(const char *key);
LongArray *ir_get_struct_field_dims(const char *key);
const char *ir_get_struct_field_tag(const char *key);

extern IrGlobalVarArray ir_global_decls;
extern HashMap ir_global_vars;
extern HashMap ir_global_arrays;
extern HashMap ir_global_struct_vars;
extern HashMap ir_global_array_dims;
extern HashMap ir_local_array_dims;
extern HashMap ir_global_array_base_sizes;
extern HashMap ir_local_array_base_sizes;
extern HashMap ir_global_var_elem_scales;
extern HashMap ir_global_var_is_pointer;
extern HashMap ir_local_var_elem_scales;
extern HashMap ir_local_var_is_pointer;
extern HashMap ir_function_return_aggregate_sizes;
extern HashMap ir_function_param_aggregate_sizes;
extern HashMap ir_function_vararg_fixed_counts;
extern int ir_current_target_scale;

IrFunctionArray ir_lower_program(NodeArray ast, const char *target, Arena *arena);

#endif // IR_H

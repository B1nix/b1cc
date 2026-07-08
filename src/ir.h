#ifndef IR_H
#define IR_H

#include "ast.h"
#include "common.h"

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
void string_pair_array_push(StringPairArray *arr, const StringPair *val);
void string_pair_array_free(StringPairArray *arr);

typedef struct {
    const char *name;
    int is_array;
    long size;
    LongArray initializers;
    IntArray initializer_is_string;
    StringPairArray strings;
    int is_static;
    int is_extern;
    int elem_size;
    int align;
    int is_thread_local;
} IrGlobalVar;

typedef struct {
    IrGlobalVar *data;
    int count;
    int capacity;
} IrGlobalVarArray;

void ir_global_var_array_init(IrGlobalVarArray *arr);
void ir_global_var_array_push(IrGlobalVarArray *arr, const IrGlobalVar *val);
void ir_global_var_array_free(IrGlobalVarArray *arr);

typedef enum {
    IR_NOP = 0,
    IR_NOT,
    IR_NOTEQ,
    IR_NOTEQ64,
    IR_MOD,
    IR_MOD64,
    IR_AND,
    IR_AND64,
    IR_MUL,
    IR_MUL64,
    IR_ADD,
    IR_ADD64,
    IR_SUB,
    IR_SUB64,
    IR_DIV,
    IR_DIV64,
    IR_LT,
    IR_LT64,
    IR_LTLT,
    IR_LTLT64,
    IR_LTEQ,
    IR_LTEQ64,
    IR_EQEQ,
    IR_EQEQ64,
    IR_GT,
    IR_GT64,
    IR_GTEQ,
    IR_GTEQ64,
    IR_GTGT,
    IR_GTGT64,
    IR_XOR,
    IR_XOR64,
    IR_ADDR,
    IR_ASM,
    IR_CALL,
    IR_CAST,
    IR_CONST,
    IR_CONST64,
    IR_COPY,
    IR_D2F,
    IR_DUP,
    IR_EXTRACT_BITS,
    IR_FNOTEQ,
    IR_F2I,
    IR_FLT,
    IR_FLTEQ,
    IR_FEQEQ,
    IR_FGT,
    IR_FGTEQ,
    IR_FADD,
    IR_FCONST,
    IR_FDIV,
    IR_FGLOAD,
    IR_FGSTORE,
    IR_FLOAD4,
    IR_FLOAD8,
    IR_FLOAD_ADDR4,
    IR_FLOAD_ADDR8,
    IR_FMUL,
    IR_FNEG,
    IR_FRET,
    IR_FSTORE4,
    IR_FSTORE8,
    IR_FSTORE_ADDR4,
    IR_FSTORE_ADDR8,
    IR_FSUB,
    IR_GADDR,
    IR_GLOAD,
    IR_GLOAD64,
    IR_GSTORE,
    IR_GSTORE64,
    IR_I2F,
    IR_ICALL,
    IR_INDEX,
    IR_INSERT_BITS,
    IR_JMP,
    IR_JZ,
    IR_JZ64,
    IR_LABEL,
    IR_LOAD,
    IR_LOAD64,
    IR_LOAD_ADDR,
    IR_NEG,
    IR_NEG64,
    IR_POP,
    IR_POP64,
    IR_RET,
    IR_RET64,
    IR_RET_AGG,
    IR_SEXT64,
    IR_SEXT_BITS,
    IR_STORE,
    IR_STORE64,
    IR_STORE_AGG,
    IR_STORE_INDEX,
    IR_STORE_INDEX_KEEP,
    IR_STR,
    IR_TRUNC32,
    IR_UMOD,
    IR_UMOD64,
    IR_UDIV,
    IR_UDIV64,
    IR_ULT,
    IR_ULT64,
    IR_ULTEQ,
    IR_ULTEQ64,
    IR_UGT,
    IR_UGT64,
    IR_UGTEQ,
    IR_UGTEQ64,
    IR_UGTGT,
    IR_UGTGT64,
    IR_UCAST,
    IR_VA_START,
    IR_VLA_ALLOC,
    IR_ZEXT64,
    IR_OR,
    IR_OR64,
    IR_TILDE,
    IR_TILDE64,
} IrOp;

typedef struct {
    IrOp op;
    int arg;      /* index into the interned arg-string pool (see ir_arg_str) */
    long value;
    int line;
    int col;
} IrInst;

const char *ir_op_to_string(IrOp op);
IrOp ir_string_to_op(const char *op);

/* Operand strings are interned into a global pool; IrInst.arg holds the index.
   ir_intern_arg is used at lowering time, ir_arg_str at emit time. */
int ir_intern_arg(const char *s);
const char *ir_arg_str(int idx);

typedef struct {
    IrInst *data;
    int count;
    int capacity;
} IrInstArray;

void ir_inst_array_init(IrInstArray *arr);
void ir_inst_array_push(IrInstArray *arr, const IrInst *val);
void ir_inst_array_free(IrInstArray *arr);

typedef struct {
    const char *name;
    StringArray params;
    IntArray param_aggregate_sizes;
    IntArray param_aggregate_float_classes;
    IrInstArray code;
    HashMap locals;
    StringPairArray strings;
    int has_call;
    int label_id;
    int is_static;
    int return_aggregate_size;
    int return_aggregate_float_class;
    int max_align;
} IrFunction;

typedef struct {
    IrFunction *data;
    int count;
    int capacity;
} IrFunctionArray;

void ir_function_array_init(IrFunctionArray *arr);
void ir_function_array_push(IrFunctionArray *arr, const IrFunction *val);
void ir_function_array_free(IrFunctionArray *arr);

typedef struct {
    const char *break_label;
    const char *continue_label;
} LoopContext;

void ir_reset_state(void);
void ir_declare_global(const char *name, int is_array, long size, int is_static, int is_extern, int elem_size, int target_scale);
void ir_mark_global_struct(const char *name);
void ir_set_global_align(const char *name, int align);
void ir_set_global_array_dims(const char *name, LongArray dims);
void ir_set_global_initializers(const char *name, LongArray inits);
void ir_set_global_initializers_with_strings(const char *name, LongArray inits, IntArray is_string, StringPairArray strings);
void ir_set_global_array_base_size(const char *name, int val);
void ir_set_local_array_dims(const char *name, LongArray dims);
void ir_set_local_array_base_size(const char *name, int val);
int ir_get_global_array_base_size(const char *name);
int ir_get_local_array_base_size(const char *name);
int ir_get_global_storage_size(const char *name);
void ir_set_local_var_is_pointer(const char *name, int val);
void ir_set_local_var_elem_scale(const char *name, int val);
int ir_get_local_var_elem_scale(const char *name);
int ir_get_global_var_elem_scale(const char *name);
void ir_set_global_var_is_pointer(const char *name, int val);
void ir_set_global_var_elem_scale(const char *name, int val);
void ir_set_global_thread_local(const char *name, int val);
int ir_is_global_var_thread_local(const char *name);


void ir_set_var_unsigned(const char *name, int val);
void ir_set_var_pointee_unsigned(const char *name, int val);
void ir_set_var_bool(const char *name, int val);
void ir_set_var_float(const char *name, int val);
int ir_get_var_float(const char *name);
void ir_set_var_type_size(const char *name, int val);
void ir_set_var_struct_tag(const char *name, const char *tag);
void ir_set_struct_float_aggregate_class(const char *tag, int val);
int ir_get_struct_float_aggregate_class(const char *tag);

void ir_set_struct_field_offset(const char *key, int val);
void ir_set_struct_field_size(const char *key, int val);
void ir_set_struct_field_total_size(const char *key, int val);
void ir_set_struct_field_dims(const char *key, LongArray dims);
void ir_set_struct_field_tag(const char *key, const char *tag);
void ir_set_struct_field_bit_offset(const char *key, int val);
void ir_set_struct_field_bit_width(const char *key, int val);

int ir_get_var_unsigned(const char *name);
int ir_get_var_bool(const char *name);
int ir_get_var_type_size(const char *name);
const char *ir_get_var_struct_tag(const char *name);
int ir_get_struct_field_offset(const char *key);
int ir_get_struct_field_size(const char *key);
int ir_get_struct_field_total_size(const char *key);
LongArray *ir_get_struct_field_dims(const char *key);
const char *ir_get_struct_field_tag(const char *key);
int ir_get_struct_field_bit_offset(const char *key);
int ir_get_struct_field_bit_width(const char *key);

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
extern HashMap ir_function_return_aggregate_float_classes;
extern HashMap ir_function_param_aggregate_float_classes;
extern HashMap ir_function_vararg_fixed_counts;
extern HashMap ir_function_param_floats;
extern HashMap ir_function_return_floats;
extern HashMap ir_function_pointer_param_floats;
extern HashMap ir_function_pointer_return_floats;
extern HashMap ir_function_return_int_sizes;
extern HashMap ir_function_param_int_sizes;
extern int ir_current_target_scale;
extern int ir_code_model; /* 0=small (default), 1=kernel */
extern int ir_pic_mode;  /* 0=non-PIC (default), 1=PIC/PIE */
extern StringArray ir_global_asm_blocks;

void ir_set_function_pointer_signature(const char *name, const IntArray *param_floats, int return_float);
void ir_add_global_asm(const char *asm_text);

IrFunctionArray ir_lower_program(const NodeArray *ast, const char *target, Arena *arena);

#endif // IR_H

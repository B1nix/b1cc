#include "ir.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IrGlobalVarArray ir_global_decls;
HashMap ir_global_vars;
HashMap ir_global_arrays;
HashMap ir_global_struct_vars;
HashMap ir_global_array_dims;
HashMap ir_local_array_dims;
HashMap ir_global_array_base_sizes;
HashMap ir_local_array_base_sizes;
HashMap ir_global_var_elem_scales;
HashMap ir_global_var_is_pointer;
HashMap ir_local_var_elem_scales;
HashMap ir_local_var_is_pointer;
HashMap ir_function_return_aggregate_sizes;
HashMap ir_function_param_aggregate_sizes;
HashMap ir_function_vararg_fixed_counts;
int ir_current_target_scale = 8;

typedef struct {
    LoopContext *data;
    int count;
    int capacity;
} LoopContextArray;

static B1CC_THREAD_LOCAL LoopContextArray loop_contexts;
static B1CC_THREAD_LOCAL int current_func_ret_size = 0;
static B1CC_THREAD_LOCAL int current_func_ret_aggregate_size = 0;

static HashMap ir_var_unsigned;
static HashMap ir_var_bool;
static HashMap ir_var_type_size;
static HashMap ir_var_struct_tag;
static HashMap ir_struct_field_offsets;
static HashMap ir_struct_field_sizes;
static HashMap ir_struct_field_total_sizes;
static HashMap ir_struct_field_dims;
static HashMap ir_struct_field_tags;

void ir_global_var_array_init(IrGlobalVarArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
void ir_global_var_array_push(IrGlobalVarArray *arr, const IrGlobalVar *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(IrGlobalVar));
    }
    arr->data[arr->count].name = val->name;
    arr->data[arr->count].is_array = val->is_array;
    arr->data[arr->count].size = val->size;
    arr->data[arr->count].initializers = val->initializers;
    arr->data[arr->count].is_static = val->is_static;
    arr->data[arr->count].is_extern = val->is_extern;
    arr->data[arr->count].elem_size = val->elem_size;
    arr->data[arr->count].align = val->align;
    arr->count = arr->count + 1;
}
void ir_global_var_array_free(IrGlobalVarArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void ir_inst_array_init(IrInstArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
void ir_inst_array_push(IrInstArray *arr, const IrInst *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(IrInst));
    }
    arr->data[arr->count].op = val->op;
    arr->data[arr->count].arg = val->arg;
    arr->data[arr->count].value = val->value;
    arr->count = arr->count + 1;
}
void ir_inst_array_free(IrInstArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void string_pair_array_init(StringPairArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
void string_pair_array_push(StringPairArray *arr, const StringPair *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(StringPair));
    }
    arr->data[arr->count].first = val->first;
    arr->data[arr->count].second = val->second;
    arr->count = arr->count + 1;
}
void string_pair_array_free(StringPairArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void ir_function_array_init(IrFunctionArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
void ir_function_array_push(IrFunctionArray *arr, const IrFunction *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(IrFunction));
    }
    arr->data[arr->count].name = val->name;
    arr->data[arr->count].params = val->params;
    arr->data[arr->count].param_aggregate_sizes = val->param_aggregate_sizes;
    arr->data[arr->count].code = val->code;
    arr->data[arr->count].locals = val->locals;
    arr->data[arr->count].strings = val->strings;
    arr->data[arr->count].has_call = val->has_call;
    arr->data[arr->count].label_id = val->label_id;
    arr->data[arr->count].is_static = val->is_static;
    arr->data[arr->count].return_aggregate_size = val->return_aggregate_size;
    arr->count = arr->count + 1;
}
void ir_function_array_free(IrFunctionArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

static void loop_context_array_init(LoopContextArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
static void loop_context_array_push(LoopContextArray *arr, const LoopContext *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(LoopContext));
    }
    arr->data[arr->count].break_label = val->break_label;
    arr->data[arr->count].continue_label = val->continue_label;
    arr->count = arr->count + 1;
}
static void loop_context_array_free(LoopContextArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

static void push_loop_ctx(LoopContextArray *arr, const char *break_label, const char *continue_label) {
    LoopContext ctx;
    ctx.break_label = break_label;
    ctx.continue_label = continue_label;
    loop_context_array_push(arr, &ctx);
}

typedef struct {
    long first;
    const char *second;
} LongStringPair;

typedef struct {
    LongStringPair *data;
    int count;
    int capacity;
} LongStringPairArray;

static void long_string_pair_array_init(LongStringPairArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
static void long_string_pair_array_push(LongStringPairArray *arr, const LongStringPair *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(LongStringPair));
    }
    arr->data[arr->count].first = val->first;
    arr->data[arr->count].second = val->second;
    arr->count = arr->count + 1;
}
static void long_string_pair_array_free(LongStringPairArray *arr) {
    free(arr->data);
}

void ir_reset_state(void) {
    for (int i = 0; i < ir_global_decls.count; ++i) {
        long_array_free(&ir_global_decls.data[i].initializers);
    }
    ir_global_var_array_free(&ir_global_decls);
    ir_global_var_array_init(&ir_global_decls);

    hashmap_free(&ir_global_vars);
    hashmap_init(&ir_global_vars, 64);
    
    hashmap_free(&ir_global_arrays);
    hashmap_init(&ir_global_arrays, 64);

    hashmap_free(&ir_global_struct_vars);
    hashmap_init(&ir_global_struct_vars, 64);

    for (int b = 0; b < ir_global_array_dims.bucket_count; ++b) {
        HashMapEntry *curr = ir_global_array_dims.buckets[b];
        while (curr) {
            LongArray *arr = (LongArray *)curr->val_ptr;
            long_array_free(arr);
            free(arr);
            curr = curr->next;
        }
    }
    hashmap_free(&ir_global_array_dims);
    hashmap_init(&ir_global_array_dims, 64);

    for (int b = 0; b < ir_local_array_dims.bucket_count; ++b) {
        HashMapEntry *curr = ir_local_array_dims.buckets[b];
        while (curr) {
            LongArray *arr = (LongArray *)curr->val_ptr;
            long_array_free(arr);
            free(arr);
            curr = curr->next;
        }
    }
    hashmap_free(&ir_local_array_dims);
    hashmap_init(&ir_local_array_dims, 64);

    hashmap_free(&ir_global_array_base_sizes);
    hashmap_init(&ir_global_array_base_sizes, 64);

    hashmap_free(&ir_local_array_base_sizes);
    hashmap_init(&ir_local_array_base_sizes, 64);

    hashmap_free(&ir_global_var_elem_scales);
    hashmap_init(&ir_global_var_elem_scales, 64);

    hashmap_free(&ir_global_var_is_pointer);
    hashmap_init(&ir_global_var_is_pointer, 64);

    hashmap_free(&ir_local_var_elem_scales);
    hashmap_init(&ir_local_var_elem_scales, 64);

    hashmap_free(&ir_local_var_is_pointer);
    hashmap_init(&ir_local_var_is_pointer, 64);

    hashmap_free(&ir_function_return_aggregate_sizes);
    hashmap_init(&ir_function_return_aggregate_sizes, 64);

    for (int b = 0; b < ir_function_param_aggregate_sizes.bucket_count; ++b) {
        HashMapEntry *curr = ir_function_param_aggregate_sizes.buckets[b];
        while (curr) {
            IntArray *arr = (IntArray *)curr->val_ptr;
            int_array_free(arr);
            free(arr);
            curr = curr->next;
        }
    }
    hashmap_free(&ir_function_param_aggregate_sizes);
    hashmap_init(&ir_function_param_aggregate_sizes, 64);

    hashmap_free(&ir_function_vararg_fixed_counts);
    hashmap_init(&ir_function_vararg_fixed_counts, 64);

    hashmap_free(&ir_var_unsigned);
    hashmap_init(&ir_var_unsigned, 64);

    hashmap_free(&ir_var_bool);
    hashmap_init(&ir_var_bool, 64);

    hashmap_free(&ir_var_type_size);
    hashmap_init(&ir_var_type_size, 64);

    hashmap_free(&ir_var_struct_tag);
    hashmap_init(&ir_var_struct_tag, 64);

    hashmap_free(&ir_struct_field_offsets);
    hashmap_init(&ir_struct_field_offsets, 64);

    hashmap_free(&ir_struct_field_sizes);
    hashmap_init(&ir_struct_field_sizes, 64);

    hashmap_free(&ir_struct_field_total_sizes);
    hashmap_init(&ir_struct_field_total_sizes, 64);

    for (int b = 0; b < ir_struct_field_dims.bucket_count; ++b) {
        HashMapEntry *curr = ir_struct_field_dims.buckets[b];
        while (curr) {
            LongArray *arr = (LongArray *)curr->val_ptr;
            long_array_free(arr);
            free(arr);
            curr = curr->next;
        }
    }
    hashmap_free(&ir_struct_field_dims);
    hashmap_init(&ir_struct_field_dims, 64);

    hashmap_free(&ir_struct_field_tags);
    hashmap_init(&ir_struct_field_tags, 64);
}

void ir_declare_global(const char *name, int is_array, long size, int is_static, int is_extern, int elem_size, int target_scale) {
    (void)target_scale;
    for (int i = 0; i < ir_global_decls.count; ++i) {
        if (strcmp(ir_global_decls.data[i].name, name) == 0) {
            ir_global_decls.data[i].is_array = is_array;
            ir_global_decls.data[i].size = size;
            ir_global_decls.data[i].is_static = is_static;
            if (!is_extern) {
                ir_global_decls.data[i].is_extern = 0;
            }
            ir_global_decls.data[i].elem_size = elem_size;
            return;
        }
    }
    IrGlobalVar v;
    v.name = name;
    v.is_array = is_array;
    v.size = size;
    long_array_init(&v.initializers);
    v.is_static = is_static;
    v.is_extern = is_extern;
    v.elem_size = elem_size;
    v.align = 0;
    ir_global_var_array_push(&ir_global_decls, &v);

    hashmap_put(&ir_global_vars, name, nullptr, 1);
    hashmap_put(&ir_global_var_elem_scales, name, nullptr, elem_size);
    if (is_array) {
        hashmap_put(&ir_global_arrays, name, nullptr, 1);
    }
}

void ir_mark_global_struct(const char *name) {
    hashmap_put(&ir_global_struct_vars, name, nullptr, 1);
}

void ir_set_global_array_dims(const char *name, LongArray dims) {
    LongArray *arr = malloc(sizeof(LongArray));
    long_array_init(arr);
    for (int idx = 0; idx < dims.count; ++idx) {
        long_array_push(arr, dims.data[idx]);
    }
    hashmap_put(&ir_global_array_dims, name, arr, 0);
}

void ir_set_global_initializers(const char *name, LongArray inits) {
    // Find declaration
    for (int idx = 0; idx < ir_global_decls.count; ++idx) {
        if (strcmp(ir_global_decls.data[idx].name, name) == 0) {
            long_array_free(&ir_global_decls.data[idx].initializers);
            long_array_init(&ir_global_decls.data[idx].initializers);
            for (int k = 0; k < inits.count; ++k) {
                long_array_push(&ir_global_decls.data[idx].initializers, inits.data[k]);
            }
            break;
        }
    }
}

void ir_set_global_array_base_size(const char *name, int val) {
    hashmap_put(&ir_global_array_base_sizes, name, nullptr, val);
}

void ir_set_local_array_dims(const char *name, LongArray dims) {
    LongArray *arr = malloc(sizeof(LongArray));
    long_array_init(arr);
    for (int idx = 0; idx < dims.count; ++idx) {
        long_array_push(arr, dims.data[idx]);
    }
    hashmap_put(&ir_local_array_dims, name, arr, 0);
}

void ir_set_local_array_base_size(const char *name, int val) {
    hashmap_put(&ir_local_array_base_sizes, name, nullptr, val);
}

int ir_get_global_array_base_size(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_global_array_base_sizes, name);
    return entry ? entry->val_int : 0;
}

int ir_get_local_array_base_size(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_local_array_base_sizes, name);
    return entry ? entry->val_int : 0;
}

void ir_set_local_var_is_pointer(const char *name, int val) {
    hashmap_put(&ir_local_var_is_pointer, name, nullptr, val);
}

void ir_set_local_var_elem_scale(const char *name, int val) {
    hashmap_put(&ir_local_var_elem_scales, name, nullptr, val);
}

int ir_get_local_var_elem_scale(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_local_var_elem_scales, name);
    return entry ? entry->val_int : 0;
}

int ir_get_global_var_elem_scale(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_global_var_elem_scales, name);
    return entry ? entry->val_int : 0;
}

void ir_set_global_var_is_pointer(const char *name, int val) {
    hashmap_put(&ir_global_var_is_pointer, name, nullptr, val);
}

void ir_set_global_var_elem_scale(const char *name, int val) {
    hashmap_put(&ir_global_var_elem_scales, name, nullptr, val);
}


void ir_set_var_unsigned(const char *name, int val) {
    hashmap_put(&ir_var_unsigned, name, nullptr, val);
}
void ir_set_var_bool(const char *name, int val) {
    hashmap_put(&ir_var_bool, name, nullptr, val);
}
void ir_set_var_type_size(const char *name, int val) {
    hashmap_put(&ir_var_type_size, name, nullptr, val);
}
void ir_set_var_struct_tag(const char *name, const char *tag) {
    hashmap_put(&ir_var_struct_tag, name, (void *)tag, 0);
}

void ir_set_struct_field_offset(const char *key, int val) {
    hashmap_put(&ir_struct_field_offsets, key, nullptr, val);
}
void ir_set_struct_field_size(const char *key, int val) {
    hashmap_put(&ir_struct_field_sizes, key, nullptr, val);
}
void ir_set_struct_field_total_size(const char *key, int val) {
    hashmap_put(&ir_struct_field_total_sizes, key, nullptr, val);
}
void ir_set_struct_field_dims(const char *key, LongArray dims) {
    LongArray *arr = malloc(sizeof(LongArray));
    long_array_init(arr);
    for (int idx = 0; idx < dims.count; ++idx) {
        long_array_push(arr, dims.data[idx]);
    }
    hashmap_put(&ir_struct_field_dims, key, arr, 0);
}
void ir_set_struct_field_tag(const char *key, const char *tag) {
    hashmap_put(&ir_struct_field_tags, key, (void *)tag, 0);
}

int ir_get_var_unsigned(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_var_unsigned, name);
    return entry ? entry->val_int : 0;
}
int ir_get_var_bool(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_var_bool, name);
    return entry ? entry->val_int : 0;
}
int ir_get_var_type_size(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_var_type_size, name);
    return entry ? entry->val_int : 8;
}
const char *ir_get_var_struct_tag(const char *name) {
    HashMapEntry *entry = hashmap_get(&ir_var_struct_tag, name);
    return entry ? (const char *)entry->val_ptr : "";
}
int ir_get_struct_field_offset(const char *key) {
    HashMapEntry *entry = hashmap_get(&ir_struct_field_offsets, key);
    return entry ? entry->val_int : 0;
}
int ir_get_struct_field_size(const char *key) {
    HashMapEntry *entry = hashmap_get(&ir_struct_field_sizes, key);
    return entry ? entry->val_int : 0;
}
int ir_get_struct_field_total_size(const char *key) {
    HashMapEntry *entry = hashmap_get(&ir_struct_field_total_sizes, key);
    return entry ? entry->val_int : 0;
}
LongArray *ir_get_struct_field_dims(const char *key) {
    HashMapEntry *entry = hashmap_get(&ir_struct_field_dims, key);
    return entry ? (LongArray *)entry->val_ptr : nullptr;
}
const char *ir_get_struct_field_tag(const char *key) {
    HashMapEntry *entry = hashmap_get(&ir_struct_field_tags, key);
    return entry ? (const char *)entry->val_ptr : "";
}

static void lower_addr(const Node *node, IrFunction *fn, int target_scale, Arena *arena);
static void lower_expr(const Node *node, IrFunction *fn, int target_scale, Arena *arena);

static const char *get_base_var_name(const Node *node) {
    if (strcmp(node->op, "var") == 0) return node->name;
    if (strcmp(node->op, "index") == 0) return get_base_var_name(node->lhs);
    return "";
}

static LongArray get_node_dims(const Node *node, const IrFunction *fn) {
    if (node->array_dims.count > 0) {
        LongArray out;
        long_array_init(&out);
        for (int idx = 0; idx < node->array_dims.count; ++idx) {
            long_array_push(&out, node->array_dims.data[idx]);
        }
        return out;
    }
    if (strcmp(node->op, "index") == 0) {
        if (node->lhs && node->lhs->array_dims.count > 0) {
            if (node->lhs->array_dims.count <= 1) {
                LongArray out;
                long_array_init(&out);
                return out;
            }
            LongArray out;
            long_array_init(&out);
            for (int idx = 1; idx < node->lhs->array_dims.count; ++idx) {
                long_array_push(&out, node->lhs->array_dims.data[idx]);
            }
            return out;
        }
        const Node *base = node;
        int index_count = 0;
        while (strcmp(base->op, "index") == 0) {
            index_count++;
            base = base->lhs;
        }
        if (base->array_dims.count > 0) {
            if (index_count >= base->array_dims.count) {
                LongArray out;
                long_array_init(&out);
                return out;
            }
            LongArray out;
            long_array_init(&out);
            for (int idx = index_count; idx < base->array_dims.count; ++idx) {
                long_array_push(&out, base->array_dims.data[idx]);
            }
            return out;
        }
    }
    const char *base_name = get_base_var_name(node);
    if (!base_name || !base_name[0]) {
        LongArray out;
        long_array_init(&out);
        return out;
    }
    char local_key[512];
    snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, base_name);
    
    LongArray dims;
    long_array_init(&dims);
    HashMapEntry *entry = hashmap_get(&ir_local_array_dims, local_key);
    if (entry) {
        LongArray *ptr = (LongArray *)entry->val_ptr;
        for (int idx = 0; idx < ptr->count; ++idx) {
            long_array_push(&dims, ptr->data[idx]);
        }
    } else {
        entry = hashmap_get(&ir_global_array_dims, base_name);
        if (entry) {
            LongArray *ptr = (LongArray *)entry->val_ptr;
            for (int idx = 0; idx < ptr->count; ++idx) {
                long_array_push(&dims, ptr->data[idx]);
            }
        } else {
            return dims;
        }
    }
    int index_count = 0;
    const Node *curr = node;
    while (strcmp(curr->op, "index") == 0) {
        index_count++;
        curr = curr->lhs;
    }
    if (index_count >= dims.count) {
        long_array_free(&dims);
        LongArray out;
        long_array_init(&out);
        return out;
    }
    LongArray out;
    long_array_init(&out);
    for (int idx = index_count; idx < dims.count; ++idx) {
        long_array_push(&out, dims.data[idx]);
    }
    long_array_free(&dims);
    return out;
}

static void ir_push(IrFunction *fn, const char *op, const char *arg, long value) {
    IrInst inst;
    inst.op = op;
    inst.arg = arg;
    inst.value = value;
    ir_inst_array_push(&fn->code, &inst);
}

static int get_expr_pointer_scale(const Node *node, const IrFunction *fn) {
    if (!node) return 0;
    if (strcmp(node->op, "var") == 0) {
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, node->name);
        HashMapEntry *is_ptr = hashmap_get(&ir_local_var_is_pointer, local_key);
        if (is_ptr && is_ptr->val_int) {
            HashMapEntry *scale = hashmap_get(&ir_local_var_elem_scales, local_key);
            return scale ? scale->val_int : 0;
        }
        is_ptr = hashmap_get(&ir_global_var_is_pointer, node->name);
        if (is_ptr && is_ptr->val_int) {
            HashMapEntry *scale = hashmap_get(&ir_global_var_elem_scales, node->name);
            return scale ? scale->val_int : 0;
        }
        HashMapEntry *dims_entry = hashmap_get(&ir_local_array_dims, local_key);
        if (dims_entry) {
            LongArray *dims = (LongArray *)dims_entry->val_ptr;
            long mult = 1;
            for (int idx = 1; idx < dims->count; ++idx) mult *= dims->data[idx];
            HashMapEntry *base_entry = hashmap_get(&ir_local_array_base_sizes, local_key);
            int base_size = base_entry ? base_entry->val_int : 8;
            return mult * base_size;
        }
        dims_entry = hashmap_get(&ir_global_array_dims, node->name);
        if (dims_entry) {
            LongArray *dims = (LongArray *)dims_entry->val_ptr;
            long mult = 1;
            for (int idx = 1; idx < dims->count; ++idx) mult *= dims->data[idx];
            HashMapEntry *base_entry = hashmap_get(&ir_global_array_base_sizes, node->name);
            int base_size = base_entry ? base_entry->val_int : 8;
            return mult * base_size;
        }
    }
    if (strcmp(node->op, "index") == 0) {
        if (node->lhs && node->lhs->pointee_size > 0) {
            return node->lhs->pointee_size;
        }
        LongArray dims = get_node_dims(node, fn);
        const char *base_name = get_base_var_name(node);
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, base_name);
        if (dims.count > 0) {
            int base_size = 8;
            HashMapEntry *base_entry = hashmap_get(&ir_local_array_base_sizes, local_key);
            if (base_entry) {
                base_size = base_entry->val_int;
            } else {
                base_entry = hashmap_get(&ir_global_array_base_sizes, base_name);
                if (base_entry) {
                    base_size = base_entry->val_int;
                } else {
                    base_entry = hashmap_get(&ir_local_var_elem_scales, local_key);
                    if (base_entry) {
                        base_size = base_entry->val_int;
                    } else {
                        base_entry = hashmap_get(&ir_global_var_elem_scales, base_name);
                        if (base_entry) {
                            base_size = base_entry->val_int;
                        }
                    }
                }
            }
            long mult = 1;
            for (int idx = 1; idx < dims.count; ++idx) mult *= dims.data[idx];
            long_array_free(&dims);
            return mult * base_size;
        }
        long_array_free(&dims);
        return 0;
    }
    if (strcmp(node->op, "cast") == 0) {
        if (strncmp(node->name, "ptr:", 4) == 0) {
            return atoi(node->name + 4);
        }
    }
    if (strcmp(node->op, "+") == 0 || strcmp(node->op, "-") == 0) {
        int left = get_expr_pointer_scale(node->lhs, fn);
        if (left > 0) return left;
        if (strcmp(node->op, "+") == 0) {
            int right = get_expr_pointer_scale(node->rhs, fn);
            if (right > 0) return right;
        }
    }
    if (strcmp(node->op, "?") == 0) {
        int left = get_expr_pointer_scale(node->body.data[0], fn);
        if (left > 0) return left;
        return get_expr_pointer_scale(node->body.data[1], fn);
    }
    return 0;
}

static void lower_expr(const Node *node, IrFunction *fn, int target_scale, Arena *arena) {
    if (!node) return;
    if (strcmp(node->op, "num") == 0) {
        ir_push(fn, "const", "", node->value);
        return;
    }
    if (strcmp(node->op, "cast") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        if (node->value > 0 && node->value < 8) {
            ir_push(fn, "cast", "", node->value);
        }
        return;
    }
    if (strcmp(node->op, "bool_cast") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "const", "", 0);
        ir_push(fn, "!=", "", 0);
        return;
    }
    if (strcmp(node->op, "prefix_++") == 0 || strcmp(node->op, "prefix_--") == 0) {
        HashMapEntry *loc_entry = hashmap_get(&fn->locals, node->name);
        if (loc_entry) {
            int slot = loc_entry->val_int;
            ir_push(fn, "load", "", slot);
            ir_push(fn, "const", "", 1);
            ir_push(fn, (strcmp(node->op, "prefix_++") == 0) ? "+" : "-", "", 0);
            ir_push(fn, "store", "", slot);
            ir_push(fn, "load", "", slot);
        } else if (hashmap_has(&ir_global_vars, node->name)) {
            ir_push(fn, "gload", node->name, 0);
            ir_push(fn, "const", "", 1);
            ir_push(fn, (strcmp(node->op, "prefix_++") == 0) ? "+" : "-", "", 0);
            ir_push(fn, "gstore", node->name, 0);
            ir_push(fn, "gload", node->name, 0);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown variable %s", node->name);
            diagnostics_error(node->line, node->col, msg);
        }
        return;
    }
    if (strcmp(node->op, "postfix_++") == 0 || strcmp(node->op, "postfix_--") == 0) {
        if (node->lhs) {
            lower_expr(node->lhs, fn, target_scale, arena);
            ir_push(fn, "dup", "", 0);
            ir_push(fn, "const", "", 1);
            ir_push(fn, (strcmp(node->op, "postfix_++") == 0) ? "+" : "-", "", 0);
            ir_push(fn, "const", "", 0);
            lower_addr(node->lhs, fn, target_scale, arena);
            int scale = get_expr_pointer_scale(node->lhs, fn);
            if (strcmp(node->lhs->op, "index") == 0 && strcmp(node->lhs->name, "byte_offset") == 0) {
                if (node->lhs->elem_size > 0) {
                    scale = node->lhs->elem_size;
                }
            }
            if (scale == 0) {
                if (node->lhs->type_size > 0) {
                    scale = node->lhs->type_size;
                } else {
                    scale = target_scale;
                }
            }
            ir_push(fn, "store_index", "", scale);
            return;
        }
        HashMapEntry *loc_entry = hashmap_get(&fn->locals, node->name);
        if (loc_entry) {
            int slot = loc_entry->val_int;
            ir_push(fn, "load", "", slot);
            ir_push(fn, "load", "", slot);
            ir_push(fn, "const", "", 1);
            ir_push(fn, (strcmp(node->op, "postfix_++") == 0) ? "+" : "-", "", 0);
            ir_push(fn, "store", "", slot);
        } else if (hashmap_has(&ir_global_vars, node->name)) {
            ir_push(fn, "gload", node->name, 0);
            ir_push(fn, "gload", node->name, 0);
            ir_push(fn, "const", "", 1);
            ir_push(fn, (strcmp(node->op, "postfix_++") == 0) ? "+" : "-", "", 0);
            ir_push(fn, "gstore", node->name, 0);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown variable %s", node->name);
            diagnostics_error(node->line, node->col, msg);
        }
        return;
    }
    if (strcmp(node->op, "unary_~") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "~", "", 0);
        return;
    }
    if (strcmp(node->op, "unary_!") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "!", "", 0);
        return;
    }
    if (strcmp(node->op, "unary_-") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "neg", "", 0);
        return;
    }
    if (strcmp(node->op, "var") == 0) {
        HashMapEntry *loc_entry = hashmap_get(&fn->locals, node->name);
        if (loc_entry) {
            ir_push(fn, "load", "", loc_entry->val_int);
        } else if (hashmap_has(&ir_global_struct_vars, node->name)) {
            ir_push(fn, "gaddr", node->name, 0);
        } else if (hashmap_has(&ir_global_arrays, node->name)) {
            ir_push(fn, "gaddr", node->name, 0);
        } else if (hashmap_has(&ir_global_vars, node->name)) {
            ir_push(fn, "gload", node->name, 0);
        } else {
            ir_push(fn, "gaddr", node->name, 0);
        }
        return;
    }
    if (strcmp(node->op, "str") == 0) {
        char label[128];
        snprintf(label, sizeof(label), ".Lstr_%s_%d", fn->name, fn->strings.count);
        const char *label_dup = arena_strdup(arena, label);
        {
        StringPair sp;
        sp.first = label_dup;
        sp.second = node->name;
        string_pair_array_push(&fn->strings, &sp);
    }
        ir_push(fn, "str", label_dup, 0);
        return;
    }
    if (strcmp(node->op, "call") == 0) {
        const char *call_name = node->name;
        int indirect = (!call_name[0]) || hashmap_has(&fn->locals, call_name);
        if (node->lhs && strcmp(node->lhs->op, "var") == 0 &&
            !hashmap_has(&fn->locals, node->lhs->name) &&
            !hashmap_has(&ir_global_vars, node->lhs->name) &&
            !hashmap_has(&ir_global_arrays, node->lhs->name) &&
            !hashmap_has(&ir_global_struct_vars, node->lhs->name)) {
            call_name = node->lhs->name;
            indirect = 0;
        } else if (node->lhs) {
            const Node *callee = node->lhs;
            if (strcmp(callee->op, "unary_*") == 0 && callee->lhs) {
                callee = callee->lhs;
            }
            lower_expr(callee, fn, target_scale, arena);
            indirect = 1;
        } else if (indirect) {
            HashMapEntry *entry = hashmap_get(&fn->locals, call_name);
            ir_push(fn, "load", "", entry->val_int);
        }
        IntArray *agg_sizes = nullptr;
        if (!indirect) {
            HashMapEntry *entry = hashmap_get(&ir_function_param_aggregate_sizes, call_name);
            if (entry) agg_sizes = (IntArray *)entry->val_ptr;
        }
        for (int k = 0; k < node->body.count; ++k) {
            int agg_size = (agg_sizes && k < agg_sizes->count) ? agg_sizes->data[k] : 0;
            if (agg_size > 0) {
                lower_addr(node->body.data[k], fn, target_scale, arena);
            } else {
                lower_expr(node->body.data[k], fn, target_scale, arena);
            }
        }
        fn->has_call = 1;
        ir_push(fn, indirect ? "icall" : "call", call_name, node->body.count);
        return;
    }
    if (strcmp(node->op, "&&") == 0) {
        char false_label[128];
        snprintf(false_label, sizeof(false_label), ".L%s_and_false%d", fn->name, fn->label_id++);
        const char *fl_dup = arena_strdup(arena, false_label);
        
        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_and_end%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "jz", fl_dup, 0);
        lower_expr(node->rhs, fn, target_scale, arena);
        ir_push(fn, "jz", fl_dup, 0);
        ir_push(fn, "const", "", 1);
        ir_push(fn, "jmp", el_dup, 0);
        ir_push(fn, "label", fl_dup, 0);
        ir_push(fn, "const", "", 0);
        ir_push(fn, "label", el_dup, 0);
        return;
    }
    if (strcmp(node->op, "||") == 0) {
        char false_label_or[128];
        snprintf(false_label_or, sizeof(false_label_or), ".L%s_or_false_or%d", fn->name, fn->label_id++);
        const char *fl_or_dup = arena_strdup(arena, false_label_or);

        char false_label[128];
        snprintf(false_label, sizeof(false_label), ".L%s_or_false%d", fn->name, fn->label_id++);
        const char *fl_dup = arena_strdup(arena, false_label);

        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_or_end%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "jz", fl_or_dup, 0);
        ir_push(fn, "const", "", 1);
        ir_push(fn, "jmp", el_dup, 0);
        ir_push(fn, "label", fl_or_dup, 0);
        lower_expr(node->rhs, fn, target_scale, arena);
        ir_push(fn, "jz", fl_dup, 0);
        ir_push(fn, "const", "", 1);
        ir_push(fn, "jmp", el_dup, 0);
        ir_push(fn, "label", fl_dup, 0);
        ir_push(fn, "const", "", 0);
        ir_push(fn, "label", el_dup, 0);
        return;
    }
    if (strcmp(node->op, "?") == 0) {
        char false_label[128];
        snprintf(false_label, sizeof(false_label), ".L%s_ternary_false%d", fn->name, fn->label_id++);
        const char *fl_dup = arena_strdup(arena, false_label);

        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_ternary_end%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "jz", fl_dup, 0);
        lower_expr(node->body.data[0], fn, target_scale, arena);
        ir_push(fn, "jmp", el_dup, 0);
        ir_push(fn, "label", fl_dup, 0);
        lower_expr(node->body.data[1], fn, target_scale, arena);
        ir_push(fn, "label", el_dup, 0);
        return;
    }
    if (strcmp(node->op, "unary_*") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        ir_push(fn, "const", "", 0);
        int scale = get_expr_pointer_scale(node->lhs, fn);
        if (scale == 0 && strcmp(node->lhs->op, "index") == 0) {
            const char *base_name = get_base_var_name(node->lhs);
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, base_name);
            HashMapEntry *entry = hashmap_get(&ir_local_var_is_pointer, local_key);
            if (entry && entry->val_int) {
                HashMapEntry *sc_entry = hashmap_get(&ir_local_var_elem_scales, local_key);
                scale = sc_entry ? sc_entry->val_int : 0;
            } else {
                entry = hashmap_get(&ir_global_var_is_pointer, base_name);
                if (entry && entry->val_int) {
                    HashMapEntry *sc_entry = hashmap_get(&ir_global_var_elem_scales, base_name);
                    scale = sc_entry ? sc_entry->val_int : 0;
                }
            }
        }
        if (scale == 0) scale = ir_current_target_scale;
        ir_push(fn, "index", "", scale);
        return;
    }
    if (strcmp(node->op, "unary_&") == 0) {
        lower_addr(node->lhs, fn, target_scale, arena);
        return;
    }
    if (strcmp(node->op, "index") == 0) {
        if (strcmp(node->name, "byte_offset") == 0) {
            lower_addr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            ir_push(fn, "+", "", 0);
            if (node->array_dims.count > 0) {
                return;
            }
            int field_sz = (node->value > 0) ? (int)node->value : target_scale;
            ir_push(fn, "const", "", 0);
            ir_push(fn, "index", "", field_sz);
            return;
        }
        lower_expr(node->lhs, fn, target_scale, arena);
        lower_expr(node->rhs, fn, target_scale, arena);
        LongArray parent_dims = get_node_dims(node->lhs, fn);
        long mult = 1;
        if (parent_dims.count > 1) {
            for (int idx = 1; idx < parent_dims.count; ++idx) {
                mult *= parent_dims.data[idx];
            }
        }
        if (mult > 1) {
            ir_push(fn, "const", "", mult);
            ir_push(fn, "*", "", 0);
        }
        long_array_free(&parent_dims);

        const char *base_name = get_base_var_name(node);
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, base_name);
        int base_size = target_scale;
        if (node->lhs && node->lhs->pointee_size > 0) {
            base_size = node->lhs->pointee_size;
        } else if (node->lhs && node->lhs->elem_size > 0) {
            base_size = node->lhs->elem_size;
        } else {
            HashMapEntry *entry = hashmap_get(&ir_local_array_base_sizes, local_key);
            if (entry) {
                base_size = entry->val_int;
            } else {
                entry = hashmap_get(&ir_global_array_base_sizes, base_name);
                if (entry) {
                    base_size = entry->val_int;
                } else {
                    int scale = get_expr_pointer_scale(node->lhs, fn);
                    if (scale > 0) base_size = scale;
                }
            }
        }
        ir_push(fn, "const", "", base_size);
        ir_push(fn, "*", "", 0);
        ir_push(fn, "+", "", 0);

        LongArray current_dims = get_node_dims(node, fn);
        if (current_dims.count == 0) {
            ir_push(fn, "const", "", 0);
            ir_push(fn, "index", "", base_size);
        }
        long_array_free(&current_dims);
        return;
    }
    if (strcmp(node->op, "+") == 0 || strcmp(node->op, "-") == 0) {
        int left_scale = get_expr_pointer_scale(node->lhs, fn);
        int right_scale = get_expr_pointer_scale(node->rhs, fn);
        if (left_scale > 0 && right_scale == 0) {
            lower_expr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            if (left_scale > 1) {
                ir_push(fn, "const", "", left_scale);
                ir_push(fn, "*", "", 0);
            }
            ir_push(fn, node->op, "", 0);
        } else if (strcmp(node->op, "+") == 0 && right_scale > 0 && left_scale == 0) {
            lower_expr(node->lhs, fn, target_scale, arena);
            if (right_scale > 1) {
                ir_push(fn, "const", "", right_scale);
                ir_push(fn, "*", "", 0);
            }
            lower_expr(node->rhs, fn, target_scale, arena);
            ir_push(fn, "+", "", 0);
        } else if (strcmp(node->op, "-") == 0 && left_scale > 0 && right_scale > 0) {
            lower_expr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            ir_push(fn, "-", "", 0);
            if (left_scale > 1) {
                ir_push(fn, "const", "", left_scale);
                ir_push(fn, "/", "", 0);
            }
        } else {
            lower_expr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            ir_push(fn, node->op, "", 0);
        }
        return;
    }
    
    lower_expr(node->lhs, fn, target_scale, arena);
    lower_expr(node->rhs, fn, target_scale, arena);
    const char *op = node->op;
    if (node->is_unsigned && (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 || strcmp(op, ">>") == 0)) {
        char *uop = arena_alloc(arena, strlen(op) + 2);
        uop[0] = 'u';
        strcpy(uop + 1, op);
        op = uop;
    }
    ir_push(fn, op, "", 0);
}

static void collect_cases(const Node *node, LongStringPairArray *cases, const char **default_label, IrFunction *fn, Arena *arena) {
    if (strcmp(node->op, "case") == 0) {
        char label[128];
        snprintf(label, sizeof(label), ".Lcase_%s_%d", fn->name, fn->label_id++);
        const char *label_dup = arena_strdup(arena, label);
        Node *nc_node = (Node *)node;
        nc_node->name = label_dup;
        LongStringPair lsp;
        lsp.first = node->value;
        lsp.second = label_dup;
        long_string_pair_array_push(cases, &lsp);
    } else if (strcmp(node->op, "default") == 0) {
        char label[128];
        snprintf(label, sizeof(label), ".Ldefault_%s_%d", fn->name, fn->label_id++);
        const char *label_dup = arena_strdup(arena, label);
        Node *nc_node = (Node *)node;
        nc_node->name = label_dup;
        *default_label = label_dup;
    }
    for (int k = 0; k < node->body.count; ++k) {
        if (node->body.data[k]) collect_cases(node->body.data[k], cases, default_label, fn, arena);
    }
    if (node->lhs) collect_cases(node->lhs, cases, default_label, fn, arena);
    if (node->rhs) collect_cases(node->rhs, cases, default_label, fn, arena);
}

static void lower_stmt(const Node *stmt, IrFunction *fn, int target_scale, Arena *arena);

static void lower_block(const Node *block, IrFunction *fn, int target_scale, Arena *arena) {
    for (int k = 0; k < block->body.count; ++k) {
        lower_stmt(block->body.data[k], fn, target_scale, arena);
    }
}

static void lower_addr(const Node *node, IrFunction *fn, int target_scale, Arena *arena) {
    if (strcmp(node->op, "unary_*") == 0) {
        lower_expr(node->lhs, fn, target_scale, arena);
        return;
    }
    if (strcmp(node->op, "index") == 0) {
        if (strcmp(node->name, "byte_offset") == 0) {
            lower_addr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            ir_push(fn, "+", "", 0);
        } else {
            lower_expr(node->lhs, fn, target_scale, arena);
            lower_expr(node->rhs, fn, target_scale, arena);
            LongArray parent_dims = get_node_dims(node->lhs, fn);
            long mult = 1;
            if (parent_dims.count > 1) {
                for (int idx = 1; idx < parent_dims.count; ++idx) {
                    mult *= parent_dims.data[idx];
                }
            }
            if (mult > 1) {
                ir_push(fn, "const", "", mult);
                ir_push(fn, "*", "", 0);
            }
            long_array_free(&parent_dims);

            const char *base_name = get_base_var_name(node);
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, base_name);
            int base_size = target_scale;
            if (node->lhs && node->lhs->pointee_size > 0) {
                base_size = node->lhs->pointee_size;
            } else if (node->lhs && node->lhs->elem_size > 0) {
                base_size = node->lhs->elem_size;
            } else {
                HashMapEntry *entry = hashmap_get(&ir_local_array_base_sizes, local_key);
                if (entry) {
                    base_size = entry->val_int;
                } else {
                    entry = hashmap_get(&ir_global_array_base_sizes, base_name);
                    if (entry) {
                        base_size = entry->val_int;
                    } else {
                        int scale = get_expr_pointer_scale(node->lhs, fn);
                        if (scale > 0) base_size = scale;
                    }
                }
            }
            ir_push(fn, "const", "", base_size);
            ir_push(fn, "*", "", 0);
            ir_push(fn, "+", "", 0);
        }
    } else if (strcmp(node->op, "var") == 0) {
        HashMapEntry *entry = hashmap_get(&fn->locals, node->name);
        if (entry) {
            char local_key[512];
            snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, node->name);
            if (hashmap_has(&ir_local_array_dims, local_key)) {
                ir_push(fn, "load", "", entry->val_int);
            } else {
                int slot = entry->val_int;
                for (int p_i = 0; p_i < fn->params.count; ++p_i) {
                    if (strcmp(fn->params.data[p_i], node->name) == 0) {
                        int agg_size = fn->param_aggregate_sizes.data[p_i];
                        if (agg_size > 0) {
                            int num_slots = (agg_size + 15) / 16;
                            slot += num_slots - 1;
                        }
                        break;
                    }
                }
                ir_push(fn, "addr", "", slot);
            }
        } else if (hashmap_has(&ir_global_vars, node->name) || hashmap_has(&ir_global_arrays, node->name)) {
            ir_push(fn, "gaddr", node->name, 0);
        } else {
            ir_push(fn, "gaddr", node->name, 0);
        }
    } else {
        fprintf(stderr, "[DEBUG] lower_addr failed for node op='%s' name='%s'\n", node->op, node->name ? node->name : "");
        diagnostics_error(node->line, node->col, "lvalue required");
    }
}

static void lower_stmt(const Node *stmt, IrFunction *fn, int target_scale, Arena *arena) {
    if (strcmp(stmt->op, "block") == 0) {
        lower_block(stmt, fn, target_scale, arena);
    } else if (strcmp(stmt->op, "decl") == 0) {
        if (hashmap_has(&fn->locals, stmt->name)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "duplicate local %s", stmt->name);
            diagnostics_error(stmt->line, stmt->col, msg);
        }
        int slot = fn->locals.size;
        hashmap_put(&fn->locals, stmt->name, nullptr, slot);
        if (stmt->lhs) {
            lower_expr(stmt->lhs, fn, target_scale, arena);
            ir_push(fn, "store", "", slot);
        }
    } else if (strcmp(stmt->op, "assign") == 0) {
        HashMapEntry *loc_entry = hashmap_get(&fn->locals, stmt->name);
        if (loc_entry) {
            int size = ir_get_var_type_size(stmt->name);
            int rhs_is_call = (strcmp(stmt->lhs->op, "call") == 0);
            if (size > 8 && !rhs_is_call) {
                lower_addr(stmt->lhs, fn, target_scale, arena);
                ir_push(fn, "load", "", loc_entry->val_int);
                ir_push(fn, "copy", "", size);
            } else {
                lower_expr(stmt->lhs, fn, target_scale, arena);
                if (size > 8) {
                    ir_push(fn, "load", "", loc_entry->val_int);
                    ir_push(fn, "store_agg", "", size);
                } else {
                    ir_push(fn, "store", "", loc_entry->val_int);
                }
            }
        } else if (hashmap_has(&ir_global_vars, stmt->name)) {
            int size = ir_get_var_type_size(stmt->name);
            int rhs_is_call = (strcmp(stmt->lhs->op, "call") == 0);
            if (size > 8 && !rhs_is_call) {
                lower_addr(stmt->lhs, fn, target_scale, arena);
                ir_push(fn, "gaddr", stmt->name, 0);
                ir_push(fn, "copy", "", size);
            } else {
                lower_expr(stmt->lhs, fn, target_scale, arena);
                if (size > 8) {
                    ir_push(fn, "gaddr", stmt->name, 0);
                    ir_push(fn, "store_agg", "", size);
                } else {
                    ir_push(fn, "gstore", stmt->name, 0);
                }
            }
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown variable %s", stmt->name);
            diagnostics_error(stmt->line, stmt->col, msg);
        }
    } else if (strcmp(stmt->op, "return") == 0) {
        if (current_func_ret_aggregate_size > 0) {
            lower_addr(stmt->lhs, fn, target_scale, arena);
            ir_push(fn, "ret_agg", "", current_func_ret_aggregate_size);
            return;
        }
        if (stmt->lhs) {
            lower_expr(stmt->lhs, fn, target_scale, arena);
        }
        if (current_func_ret_size > 0 && current_func_ret_size < 8) {
            ir_push(fn, "cast", "", current_func_ret_size);
        }
        ir_push(fn, "ret", "", 0);
    } else if (strcmp(stmt->op, "expr") == 0) {
        lower_expr(stmt->lhs, fn, target_scale, arena);
        ir_push(fn, "pop", "", 0);
    } else if (strcmp(stmt->op, "if") == 0) {
        char else_label[128];
        snprintf(else_label, sizeof(else_label), ".L%s_else%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, else_label);

        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_endif%d", fn->name, fn->label_id++);
        const char *endl_dup = arena_strdup(arena, end_label);

        lower_expr(stmt->lhs, fn, target_scale, arena);
        ir_push(fn, "jz", el_dup, 0);
        lower_stmt(stmt->body.data[0], fn, target_scale, arena);
        ir_push(fn, "jmp", endl_dup, 0);
        ir_push(fn, "label", el_dup, 0);
        if (stmt->body.count > 1) {
            lower_stmt(stmt->body.data[1], fn, target_scale, arena);
        }
        ir_push(fn, "label", endl_dup, 0);
    } else if (strcmp(stmt->op, "while") == 0) {
        char start_label[128];
        snprintf(start_label, sizeof(start_label), ".L%s_while%d", fn->name, fn->label_id++);
        const char *sl_dup = arena_strdup(arena, start_label);

        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_endwhile%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        ir_push(fn, "label", sl_dup, 0);
        lower_expr(stmt->lhs, fn, target_scale, arena);
        ir_push(fn, "jz", el_dup, 0);
        
        push_loop_ctx(&loop_contexts, el_dup, sl_dup);
        lower_stmt(stmt->rhs, fn, target_scale, arena);
        loop_contexts.count--;

        ir_push(fn, "jmp", sl_dup, 0);
        ir_push(fn, "label", el_dup, 0);
    } else if (strcmp(stmt->op, "for") == 0) {
        char start_label[128];
        snprintf(start_label, sizeof(start_label), ".L%s_for%d", fn->name, fn->label_id++);
        const char *sl_dup = arena_strdup(arena, start_label);

        char step_label[128];
        snprintf(step_label, sizeof(step_label), ".L%s_forstep%d", fn->name, fn->label_id++);
        const char *stepl_dup = arena_strdup(arena, step_label);

        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_endfor%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        if (stmt->body.data[0]) {
            lower_stmt(stmt->body.data[0], fn, target_scale, arena);
        }
        ir_push(fn, "label", sl_dup, 0);
        if (stmt->lhs) {
            lower_expr(stmt->lhs, fn, target_scale, arena);
            ir_push(fn, "jz", el_dup, 0);
        }
        
        push_loop_ctx(&loop_contexts, el_dup, stepl_dup);
        lower_stmt(stmt->rhs, fn, target_scale, arena);
        loop_contexts.count--;

        ir_push(fn, "label", stepl_dup, 0);
        if (stmt->body.data[1]) {
            lower_stmt(stmt->body.data[1], fn, target_scale, arena);
        }
        ir_push(fn, "jmp", sl_dup, 0);
        ir_push(fn, "label", el_dup, 0);
    } else if (strcmp(stmt->op, "break") == 0) {
        if (loop_contexts.count == 0) {
            diagnostics_error(stmt->line, stmt->col, "break statement not within loop or switch");
        }
        ir_push(fn, "jmp", loop_contexts.data[loop_contexts.count - 1].break_label, 0);
    } else if (strcmp(stmt->op, "continue") == 0) {
        const char *cont_label = nullptr;
        for (int idx = loop_contexts.count - 1; idx >= 0; --idx) {
            if (loop_contexts.data[idx].continue_label && loop_contexts.data[idx].continue_label[0]) {
                cont_label = loop_contexts.data[idx].continue_label;
                break;
            }
        }
        if (!cont_label) {
            diagnostics_error(stmt->line, stmt->col, "continue statement not within loop");
        }
        ir_push(fn, "jmp", cont_label, 0);
    } else if (strcmp(stmt->op, "switch") == 0) {
        char end_label[128];
        snprintf(end_label, sizeof(end_label), ".L%s_endswitch%d", fn->name, fn->label_id++);
        const char *el_dup = arena_strdup(arena, end_label);

        LongStringPairArray cases;
        long_string_pair_array_init(&cases);
        const char *default_label = nullptr;
        collect_cases(stmt, &cases, &default_label, fn, arena);

        int temp_slot = fn->locals.size;
        char temp_name[64];
        snprintf(temp_name, sizeof(temp_name), ".Lswitch_temp_%d", temp_slot);
        hashmap_put(&fn->locals, arena_strdup(arena, temp_name), nullptr, temp_slot);

        lower_expr(stmt->lhs, fn, target_scale, arena);
        ir_push(fn, "store", "", temp_slot);

        for (int k = 0; k < cases.count; ++k) {
            ir_push(fn, "load", "", temp_slot);
            ir_push(fn, "const", "", cases.data[k].first);
            ir_push(fn, "==", "", 0);
            ir_push(fn, "!", "", 0);
            ir_push(fn, "jz", cases.data[k].second, 0);
        }
        long_string_pair_array_free(&cases);

        if (default_label) {
            ir_push(fn, "jmp", default_label, 0);
        } else {
            ir_push(fn, "jmp", el_dup, 0);
        }

        push_loop_ctx(&loop_contexts, el_dup, "");
        lower_stmt(stmt->rhs, fn, target_scale, arena);
        loop_contexts.count--;

        ir_push(fn, "label", el_dup, 0);
    } else if (strcmp(stmt->op, "case") == 0 || strcmp(stmt->op, "default") == 0) {
        ir_push(fn, "label", stmt->name, 0);
    } else if (strcmp(stmt->op, "array_decl") == 0) {
        if (hashmap_has(&fn->locals, stmt->name)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "duplicate local %s", stmt->name);
            diagnostics_error(stmt->line, stmt->col, msg);
        }
        int base_slot = fn->locals.size;
        hashmap_put(&fn->locals, stmt->name, nullptr, base_slot);
        
        char local_key[512];
        snprintf(local_key, sizeof(local_key), "%s$%s", fn->name, stmt->name);
        
        int base_size = target_scale;
        HashMapEntry *base_entry = hashmap_get(&ir_local_array_base_sizes, local_key);
        if (base_entry) base_size = base_entry->val_int;

        long buf_size = stmt->value * base_size;
        long num_slots = (buf_size + 15) / 16;
        int buf_start_slot = fn->locals.size;

        for (long k = 0; k < num_slots; ++k) {
            char buf_name[512];
            snprintf(buf_name, sizeof(buf_name), "%s$buf%ld", stmt->name, k);
            hashmap_put(&fn->locals, arena_strdup(arena, buf_name), nullptr, buf_start_slot + (int)k);
        }

        ir_push(fn, "addr", "", buf_start_slot + (int)num_slots - 1);
        ir_push(fn, "store", "", base_slot);

        for (int k = 0; k < stmt->body.count; ++k) {
            if (strcmp(stmt->body.data[k]->op, "init_item") == 0) {
                int size = atoi(stmt->body.data[k]->name);
                long offset = stmt->body.data[k]->value;
                lower_expr(stmt->body.data[k]->lhs, fn, target_scale, arena);
                if (size > target_scale) {
                    ir_push(fn, "load", "", base_slot);
                    ir_push(fn, "const", "", offset);
                    ir_push(fn, "+", "", 0);
                    ir_push(fn, "store_agg", "", size);
                } else {
                    ir_push(fn, "const", "", 0);
                    ir_push(fn, "load", "", base_slot);
                    ir_push(fn, "const", "", offset);
                    ir_push(fn, "+", "", 0);
                    ir_push(fn, "store_index", "", size);
                }
            } else {
                lower_expr(stmt->body.data[k], fn, target_scale, arena);
                ir_push(fn, "const", "", k);
                ir_push(fn, "load", "", base_slot);
                ir_push(fn, "store_index", "", base_size);
            }
        }
    } else if (strcmp(stmt->op, "store_index") == 0) {
        int scale = get_expr_pointer_scale(stmt->lhs, fn);
        if (strcmp(stmt->lhs->op, "index") == 0 && strcmp(stmt->lhs->name, "byte_offset") == 0) {
            if (stmt->lhs->elem_size > 0) {
                scale = stmt->lhs->elem_size;
            }
        }
        if (scale == 0) {
            if (strcmp(stmt->lhs->op, "index") == 0 || strcmp(stmt->lhs->op, "unary_*") == 0) {
                scale = get_expr_pointer_scale(stmt->lhs->lhs, fn);
            }
        }
        if (scale == 0) scale = target_scale;

        int rhs_is_call = (strcmp(stmt->rhs->op, "call") == 0);

        if (scale > 8 && !rhs_is_call) {
            lower_addr(stmt->rhs, fn, target_scale, arena);
            lower_addr(stmt->lhs, fn, target_scale, arena);
            ir_push(fn, "copy", "", scale);
        } else {
            lower_expr(stmt->rhs, fn, target_scale, arena);
            ir_push(fn, "const", "", 0);
            lower_addr(stmt->lhs, fn, target_scale, arena);
            ir_push(fn, "store_index", "", scale);
        }
    } else if (strcmp(stmt->op, "empty") == 0) {
        // Empty statement: do nothing
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "unknown AST statement %s", stmt->op);
        diagnostics_error(stmt->line, stmt->col, msg);
    }
}

static void lower_func(const Node *ast, int target_scale, Arena *arena, IrFunction *fn) {
    current_func_ret_size = (int)ast->value;
    current_func_ret_aggregate_size = ast->aggregate_size;
    
    fn->name = ast->name;
    
    string_array_init(&fn->params);
    for (int k = 0; k < ast->params.count; ++k) {
        string_array_push(&fn->params, ast->params.data[k]);
    }
    
    int_array_init(&fn->param_aggregate_sizes);
    for (int k = 0; k < ast->param_aggregate_sizes.count; ++k) {
        int_array_push(&fn->param_aggregate_sizes, ast->param_aggregate_sizes.data[k]);
    }

    ir_inst_array_init(&fn->code);
    hashmap_init(&fn->locals, 32);
    string_pair_array_init(&fn->strings);
    fn->has_call = 0;
    fn->label_id = 0;
    fn->is_static = ast->is_static;
    fn->return_aggregate_size = ast->aggregate_size;

    for (int k = 0; k < fn->params.count; ++k) {
        const char *param = fn->params.data[k];
        if (hashmap_has(&fn->locals, param)) {
            diagnostics_error(ast->line, ast->col, "duplicate parameter");
        }
        int base_slot = fn->locals.size;
        hashmap_put(&fn->locals, param, nullptr, base_slot);
        int agg_size = fn->param_aggregate_sizes.data[k];
        if (agg_size > 0) {
            int num_slots = (agg_size + 15) / 16;
            for (int s = 1; s < num_slots; ++s) {
                char buf_name[512];
                snprintf(buf_name, sizeof(buf_name), "%s$param_buf%d", param, s);
                hashmap_put(&fn->locals, arena_strdup(arena, buf_name), nullptr, base_slot + s);
            }
        }
    }

    lower_block(ast->body.data[0], fn, target_scale, arena);
}

IrFunctionArray ir_lower_program(const NodeArray *ast, const char *target, Arena *arena) {
    int target_scale = (strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0) ? 4 : 8;
    ir_current_target_scale = target_scale;
    IrFunctionArray out;
    ir_function_array_init(&out);

    for (int k = 0; k < ast->count; ++k) {
        hashmap_put(&ir_function_return_aggregate_sizes, ast->data[k]->name, nullptr, ast->data[k]->aggregate_size);
        
        IntArray *param_sizes = malloc(sizeof(IntArray));
        int_array_init(param_sizes);
        for (int p_i = 0; p_i < ast->data[k]->param_aggregate_sizes.count; ++p_i) {
            int_array_push(param_sizes, ast->data[k]->param_aggregate_sizes.data[p_i]);
        }
        hashmap_put(&ir_function_param_aggregate_sizes, ast->data[k]->name, param_sizes, 0);

        int vararg_fixed = -1;
        for (int p_i = 0; p_i < ast->data[k]->params.count; ++p_i) {
            if (strcmp(ast->data[k]->params.data[p_i], "...") == 0) {
                vararg_fixed = p_i;
                break;
            }
        }
        hashmap_put(&ir_function_vararg_fixed_counts, ast->data[k]->name, nullptr, vararg_fixed);
    }

    loop_context_array_init(&loop_contexts);

    for (int k = 0; k < ast->count; ++k) {
        IrFunction fn_lowered;
        lower_func(ast->data[k], target_scale, arena, &fn_lowered);
        ir_function_array_push(&out, &fn_lowered);
    }

    loop_context_array_free(&loop_contexts);
    return out;
}

#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "common.h"

typedef struct {
    TokenArray tokens;
    int pos;
    int target_scale;
    int ld_size;        /* sizeof(long double): 16 x86_64-b1nix, 8 arm64-darwin */
    const char *current_func_name;
    const char *last_parsed_typedef;
    HashMap current_static_locals;
    HashMap labels;
    
    HashMap *scopes;
    int scope_count;
    int scope_capacity;

    HashMap unsigned_vars;
    HashMap bool_vars;
    HashMap const_vars;
    HashMap volatile_vars;
    HashMap float_vars;
    HashMap complex_vars;
    HashMap imaginary_vars;
    HashMap value_sizes;
    HashMap pointer_pointee_unsigned;
    HashMap pointer_pointee_const;
    HashMap var_struct_tags;
    int local_var_counter;
    int last_type_unsigned;
    int last_type_bool;
    int last_type_const;
    int last_type_volatile;
    int last_type_float;
    int last_type_complex;
    int last_type_imaginary;
    int last_type_void;
    int last_type_alignment;
    int last_type_thread_local;
    int pending_weak;
    const char *pending_section;

    HashMap global_typedefs;
    HashMap global_typedef_sizes;
    HashMap global_typedef_unsigned;
    HashMap global_typedef_float;
    HashMap global_typedef_bool;
    HashMap global_decl_sizes;
    HashMap global_decl_elem_sizes;
    HashMap global_decl_arrays;
    HashMap global_decl_incomplete_arrays;
    HashMap global_decl_pointers;
    HashMap global_decl_floats;
    HashMap global_decl_tags;
    HashMap function_return_sizes;
    HashMap function_return_unsigned;
    HashMap function_return_complex;
    HashMap function_return_pointee_sizes;
    HashMap function_return_pointee_unsigned;
    HashMap function_param_counts;
    HashMap function_varargs;
    HashMap global_typedef_struct_tags;
    HashMap global_typedef_dims;
    HashMap global_enums;
    HashMap constexpr_vars;
    HashMap global_structs;
    HashMap global_field_offsets;
    HashMap global_field_sizes;
    HashMap global_struct_sizes;
    HashMap global_struct_alignments;
    HashMap global_struct_field_tags;
    HashMap global_struct_field_offsets_by_tag;
    HashMap global_struct_field_sizes_by_tag;
    HashMap global_struct_field_float_sizes_by_tag;
    HashMap global_struct_field_elem_sizes_by_tag;
    HashMap global_struct_field_is_pointer_by_tag;
    HashMap global_struct_field_unsigned_by_tag;
    HashMap global_struct_field_total_sizes_by_tag;
    HashMap global_struct_field_dims_by_tag;
    HashMap global_struct_field_bit_offsets_by_tag;
    HashMap global_struct_field_bit_widths_by_tag;
    HashMap global_struct_float_aggregate_classes;

    const char *last_parsed_struct_tag;
    Arena *arena;
} ParserState;

NodeArray parser_parse(const TokenArray *tokens, int target_scale, int ld_size, Arena *arena);

#endif // PARSER_H

#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "common.h"

typedef struct {
    TokenArray tokens;
    int pos;
    int target_scale;
    const char *current_func_name;
    HashMap current_static_locals;
    
    HashMap *scopes;
    int scope_count;
    int scope_capacity;

    HashMap unsigned_vars;
    HashMap bool_vars;
    HashMap value_sizes;
    HashMap var_struct_tags;
    int local_var_counter;
    int last_type_unsigned;
    int last_type_bool;

    HashMap global_typedefs;
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
    HashMap global_struct_field_total_sizes_by_tag;
    HashMap global_struct_field_dims_by_tag;

    const char *last_parsed_struct_tag;
    Arena *arena;
} ParserState;

NodeArray parser_parse(TokenArray tokens, int target_scale, Arena *arena);

#endif // PARSER_H

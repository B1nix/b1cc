#ifndef IR_H
#define IR_H

#include "ast.h"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace IR {
  struct IrGlobalVar {
    std::string name;
    bool is_array;
    long size;
    std::vector<long> initializers;
    bool is_static = false;
    int elem_size = 8;
    int align = 0;
  };

  struct IrInst {
    std::string op;
    std::string arg;
    long value = 0;
  };

  struct IrFunction {
    std::string name;
    std::vector<std::string> params;
    std::vector<int> param_aggregate_sizes;
    std::vector<IrInst> code;
    std::map<std::string, int> locals;
    std::vector<std::pair<std::string, std::string>> strings;
    bool has_call = false;
    int label_id = 0;
    bool is_static = false;
    int return_aggregate_size = 0;
  };

  struct LoopContext {
    std::string break_label;
    std::string continue_label;
  };

  extern thread_local std::vector<IrGlobalVar> global_decls;
  extern thread_local std::set<std::string> global_vars;
  extern thread_local std::set<std::string> global_arrays;
  extern thread_local std::set<std::string> global_struct_vars;
  extern thread_local std::map<std::string, std::vector<long>> global_array_dims;
  extern thread_local std::map<std::string, std::vector<long>> local_array_dims;
  extern thread_local std::map<std::string, int> global_array_base_sizes;
  extern thread_local std::map<std::string, int> local_array_base_sizes;
  extern thread_local std::map<std::string, int> global_var_elem_scales;
  extern thread_local std::map<std::string, bool> global_var_is_pointer;
  extern thread_local std::map<std::string, int> local_var_elem_scales;
  extern thread_local std::map<std::string, bool> local_var_is_pointer;
  extern thread_local std::map<std::string, int> function_return_aggregate_sizes;
  extern thread_local std::map<std::string, std::vector<int>> function_param_aggregate_sizes;
  extern thread_local std::map<std::string, int> function_vararg_fixed_counts;
  extern thread_local int current_target_scale;

  std::vector<IrFunction> lower_program(std::vector<std::unique_ptr<AST::Node>> ast, const std::string &target);
}

#endif // IR_H

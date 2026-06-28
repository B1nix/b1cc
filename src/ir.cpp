#include "ir.h"
#include "diagnostics.h"

namespace IR {
  using namespace AST;

  thread_local std::vector<IrGlobalVar> global_decls;
  thread_local std::set<std::string> global_vars;
  thread_local std::set<std::string> global_arrays;
  thread_local std::map<std::string, std::vector<long>> global_array_dims;
  thread_local std::map<std::string, std::vector<long>> local_array_dims;
  thread_local std::map<std::string, int> global_array_base_sizes;
  thread_local std::map<std::string, int> local_array_base_sizes;
  thread_local std::map<std::string, int> global_var_elem_scales;
  thread_local std::map<std::string, bool> global_var_is_pointer;
  thread_local std::map<std::string, int> local_var_elem_scales;
  thread_local std::map<std::string, bool> local_var_is_pointer;
  thread_local int current_target_scale = 8;

  static thread_local std::vector<LoopContext> loop_contexts;

  static void lower_addr(const AST::Node &node, IrFunction &fn, int target_scale);

  static std::string get_base_var_name(const Node &node) {
    if (node.op == "var") return node.name;
    if (node.op == "index") return get_base_var_name(*node.lhs);
    return "";
  }

  static std::vector<long> get_node_dims(const Node &node, const IrFunction &fn) {
    std::string base_name = get_base_var_name(node);
    if (base_name.empty()) return {};
    std::vector<long> dims;
    std::string local_key = fn.name + "$" + base_name;
    if (local_array_dims.count(local_key)) {
      dims = local_array_dims.at(local_key);
    } else if (global_array_dims.count(base_name)) {
      dims = global_array_dims.at(base_name);
    } else {
      return {};
    }
    int index_count = 0;
    const Node *curr = &node;
    while (curr->op == "index") {
      index_count++;
      curr = curr->lhs.get();
    }
    if (index_count >= static_cast<int>(dims.size())) {
      return {};
    }
    return std::vector<long>(dims.begin() + index_count, dims.end());
  }

  static int get_expr_pointer_scale(const Node &node, const IrFunction &fn) {
    if (node.op == "var") {
      std::string local_key = fn.name + "$" + node.name;
      if (local_var_is_pointer.count(local_key) && local_var_is_pointer[local_key]) {
        return local_var_elem_scales[local_key];
      }
      if (global_var_is_pointer.count(node.name) && global_var_is_pointer[node.name]) {
        return global_var_elem_scales[node.name];
      }
      if (local_array_dims.count(local_key)) {
        const auto &dims = local_array_dims[local_key];
        long mult = 1;
        for (size_t i = 1; i < dims.size(); ++i) mult *= dims[i];
        int base_size = local_array_base_sizes[local_key];
        return mult * base_size;
      }
      if (global_array_dims.count(node.name)) {
        const auto &dims = global_array_dims[node.name];
        long mult = 1;
        for (size_t i = 1; i < dims.size(); ++i) mult *= dims[i];
        int base_size = global_array_base_sizes[node.name];
        return mult * base_size;
      }
    }
    if (node.op == "index") {
      std::vector<long> dims = get_node_dims(node, fn);
      if (!dims.empty()) {
        std::string base_name = get_base_var_name(node);
        std::string local_key = fn.name + "$" + base_name;
        int base_size = 8;
        if (local_array_base_sizes.count(local_key)) {
          base_size = local_array_base_sizes[local_key];
        } else if (global_array_base_sizes.count(base_name)) {
          base_size = global_array_base_sizes[base_name];
        } else if (local_var_elem_scales.count(local_key)) {
          base_size = local_var_elem_scales[local_key];
        } else if (global_var_elem_scales.count(base_name)) {
          base_size = global_var_elem_scales[base_name];
        }
        long mult = 1;
        for (size_t i = 1; i < dims.size(); ++i) mult *= dims[i];
        return mult * base_size;
      }
      return 0;
    }
    if (node.op == "cast") {
      if (node.name.rfind("ptr:", 0) == 0) {
        return std::stoi(node.name.substr(4));
      }
    }
    if (node.op == "+" || node.op == "-") {
      int left = get_expr_pointer_scale(*node.lhs, fn);
      if (left > 0) return left;
      if (node.op == "+") {
        int right = get_expr_pointer_scale(*node.rhs, fn);
        if (right > 0) return right;
      }
    }
    if (node.op == "?") {
      int left = get_expr_pointer_scale(*node.body[0], fn);
      if (left > 0) return left;
      return get_expr_pointer_scale(*node.body[1], fn);
    }
    return 0;
  }

  static void lower_expr(const Node &node, IrFunction &fn) {
    if (node.op == "num") {
      fn.code.push_back({"const", "", node.value});
      return;
    }
    if (node.op == "cast") {
      lower_expr(*node.lhs, fn);
      // node.value holds the target byte size set by the parser (0 = unknown/no truncation)
      if (node.value > 0 && node.value < 8) {
        fn.code.push_back({"cast", "", node.value});
      }
      return;
    }
    if (node.op == "prefix_++" || node.op == "prefix_--") {
      if (fn.locals.count(node.name)) {
        int slot = fn.locals[node.name];
        fn.code.push_back({"load", "", slot});
        fn.code.push_back({"const", "", 1});
        fn.code.push_back({(node.op == "prefix_++") ? "+" : "-", "", 0});
        fn.code.push_back({"store", "", slot});
        fn.code.push_back({"load", "", slot});
      } else if (global_vars.count(node.name)) {
        fn.code.push_back({"gload", node.name, 0});
        fn.code.push_back({"const", "", 1});
        fn.code.push_back({(node.op == "prefix_++") ? "+" : "-", "", 0});
        fn.code.push_back({"gstore", node.name, 0});
        fn.code.push_back({"gload", node.name, 0});
      } else {
        Diagnostics::error(node.line, node.col, "unknown variable " + node.name);
      }
      return;
    }
    if (node.op == "postfix_++" || node.op == "postfix_--") {
      if (fn.locals.count(node.name)) {
        int slot = fn.locals[node.name];
        fn.code.push_back({"load", "", slot});
        fn.code.push_back({"load", "", slot});
        fn.code.push_back({"const", "", 1});
        fn.code.push_back({(node.op == "postfix_++") ? "+" : "-", "", 0});
        fn.code.push_back({"store", "", slot});
      } else if (global_vars.count(node.name)) {
        fn.code.push_back({"gload", node.name, 0});
        fn.code.push_back({"gload", node.name, 0});
        fn.code.push_back({"const", "", 1});
        fn.code.push_back({(node.op == "postfix_++") ? "+" : "-", "", 0});
        fn.code.push_back({"gstore", node.name, 0});
      } else {
        Diagnostics::error(node.line, node.col, "unknown variable " + node.name);
      }
      return;
    }
    if (node.op == "unary_~") {
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"~", "", 0});
      return;
    }
    if (node.op == "unary_!") {
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"!", "", 0});
      return;
    }
    if (node.op == "unary_-") {
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"neg", "", 0});
      return;
    }
    if (node.op == "var") {
      if (fn.locals.count(node.name)) {
        fn.code.push_back({"load", "", fn.locals[node.name]});
      } else if (global_vars.count(node.name)) {
        fn.code.push_back({"gload", node.name, 0});
      } else if (global_arrays.count(node.name)) {
        fn.code.push_back({"gaddr", node.name, 0});
      } else {
        Diagnostics::error(node.line, node.col, "unknown variable " + node.name);
      }
      return;
    }
    if (node.op == "str") {
      std::string label = ".Lstr_" + fn.name + "_" + std::to_string(fn.strings.size());
      fn.strings.push_back({label, node.name});
      fn.code.push_back({"str", label, 0});
      return;
    }
    if (node.op == "call") {
      if (node.body.size() > 8)
        Diagnostics::error(node.line, node.col, "calls with more than 8 arguments are not supported");
      bool indirect = fn.locals.count(node.name);
      if (indirect)
        fn.code.push_back({"load", "", fn.locals[node.name]});
      for (const auto &arg : node.body)
        lower_expr(*arg, fn);
      fn.has_call = true;
      fn.code.push_back({indirect ? "icall" : "call", node.name, static_cast<long>(node.body.size())});
      return;
    }
    if (node.op == "&&") {
      std::string false_label = ".L" + fn.name + "_and_false" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_and_end" + std::to_string(fn.label_id++);
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"jz", false_label, 0});
      lower_expr(*node.rhs, fn);
      fn.code.push_back({"jz", false_label, 0});
      fn.code.push_back({"const", "", 1});
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", false_label, 0});
      fn.code.push_back({"const", "", 0});
      fn.code.push_back({"label", end_label, 0});
      return;
    }
    if (node.op == "||") {
      std::string false_label_or = ".L" + fn.name + "_or_false_or" + std::to_string(fn.label_id++);
      std::string false_label = ".L" + fn.name + "_or_false" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_or_end" + std::to_string(fn.label_id++);
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"jz", false_label_or, 0});
      fn.code.push_back({"const", "", 1});
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", false_label_or, 0});
      lower_expr(*node.rhs, fn);
      fn.code.push_back({"jz", false_label, 0});
      fn.code.push_back({"const", "", 1});
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", false_label, 0});
      fn.code.push_back({"const", "", 0});
      fn.code.push_back({"label", end_label, 0});
      return;
    }
    if (node.op == "?") {
      std::string false_label = ".L" + fn.name + "_ternary_false" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_ternary_end" + std::to_string(fn.label_id++);
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"jz", false_label, 0});
      lower_expr(*node.body[0], fn);
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", false_label, 0});
      lower_expr(*node.body[1], fn);
      fn.code.push_back({"label", end_label, 0});
      return;
    }
    if (node.op == "unary_*") {
      lower_expr(*node.lhs, fn);
      fn.code.push_back({"const", "", 0});
      int scale = get_expr_pointer_scale(*node.lhs, fn);
      if (scale == 0) scale = current_target_scale;
      fn.code.push_back({"index", "", static_cast<long>(scale)});
      return;
    }
    if (node.op == "unary_&") {
      lower_addr(*node.lhs, fn, current_target_scale);
      return;
    }
    if (node.op == "index") {
      lower_expr(*node.lhs, fn);
      // Check for struct byte_offset access: rhs is a literal byte offset, no scaling needed
      if (node.name == "byte_offset") {
        // rhs is the byte offset literal
        lower_expr(*node.rhs, fn);
        fn.code.push_back({"+", "", 0});
        // node.value holds the field byte size
        int field_sz = (node.value > 0) ? (int)node.value : current_target_scale;
        fn.code.push_back({"const", "", 0});
        fn.code.push_back({"index", "", static_cast<long>(field_sz)});
        return;
      }
      lower_expr(*node.rhs, fn);
      std::vector<long> parent_dims = get_node_dims(*node.lhs, fn);
      long mult = 1;
      if (parent_dims.size() > 1) {
        for (size_t i = 1; i < parent_dims.size(); ++i) {
          mult *= parent_dims[i];
        }
      }
      if (mult > 1) {
        fn.code.push_back({"const", "", mult});
        fn.code.push_back({"*", "", 0});
      }
      std::string base_name = get_base_var_name(node);
      std::string local_key = fn.name + "$" + base_name;
      int base_size = current_target_scale;
      if (local_array_base_sizes.count(local_key)) {
        base_size = local_array_base_sizes[local_key];
      } else if (global_array_base_sizes.count(base_name)) {
        base_size = global_array_base_sizes[base_name];
      } else {
        int scale = get_expr_pointer_scale(*node.lhs, fn);
        if (scale > 0) base_size = scale;
      }
      fn.code.push_back({"const", "", static_cast<long>(base_size)});
      fn.code.push_back({"*", "", 0});
      fn.code.push_back({"+", "", 0});

      std::vector<long> current_dims = get_node_dims(node, fn);
      if (current_dims.empty()) {
        fn.code.push_back({"const", "", 0});
        fn.code.push_back({"index", "", static_cast<long>(base_size)});
      }
      return;
    }
    if (node.op == "+" || node.op == "-") {
      int left_scale = get_expr_pointer_scale(*node.lhs, fn);
      int right_scale = get_expr_pointer_scale(*node.rhs, fn);
      if (left_scale > 0 && right_scale == 0) {
        lower_expr(*node.lhs, fn);
        lower_expr(*node.rhs, fn);
        if (left_scale > 1) {
          fn.code.push_back({"const", "", left_scale});
          fn.code.push_back({"*", "", 0});
        }
        fn.code.push_back({node.op, "", 0});
      } else if (node.op == "+" && right_scale > 0 && left_scale == 0) {
        lower_expr(*node.lhs, fn);
        if (right_scale > 1) {
          fn.code.push_back({"const", "", right_scale});
          fn.code.push_back({"*", "", 0});
        }
        lower_expr(*node.rhs, fn);
        fn.code.push_back({"+", "", 0});
      } else if (node.op == "-" && left_scale > 0 && right_scale > 0) {
        lower_expr(*node.lhs, fn);
        lower_expr(*node.rhs, fn);
        fn.code.push_back({"-", "", 0});
        if (left_scale > 1) {
          fn.code.push_back({"const", "", left_scale});
          fn.code.push_back({"/", "", 0});
        }
      } else {
        lower_expr(*node.lhs, fn);
        lower_expr(*node.rhs, fn);
        fn.code.push_back({node.op, "", 0});
      }
      return;
    }
    lower_expr(*node.lhs, fn);
    lower_expr(*node.rhs, fn);
    fn.code.push_back({node.op, "", 0});
  }

  static void collect_cases(const Node &node, std::vector<std::pair<long, std::string>> &cases, std::string &default_label, IrFunction &fn) {
    if (node.op == "case") {
      std::string label = ".Lcase_" + fn.name + "_" + std::to_string(fn.label_id++);
      const_cast<Node&>(node).name = label;
      cases.push_back({node.value, label});
    } else if (node.op == "default") {
      std::string label = ".Ldefault_" + fn.name + "_" + std::to_string(fn.label_id++);
      const_cast<Node&>(node).name = label;
      default_label = label;
    }
    for (const auto &child : node.body) {
      if (child) collect_cases(*child, cases, default_label, fn);
    }
    if (node.lhs) collect_cases(*node.lhs, cases, default_label, fn);
    if (node.rhs) collect_cases(*node.rhs, cases, default_label, fn);
  }

  static void lower_stmt(const Node &stmt, IrFunction &fn, int target_scale);

  static void lower_block(const Node &block, IrFunction &fn, int target_scale) {
    for (const auto &stmt : block.body)
      lower_stmt(*stmt, fn, target_scale);
  }

  static void lower_addr(const Node &node, IrFunction &fn, int target_scale) {
    if (node.op == "unary_*") {
      lower_expr(*node.lhs, fn);
      return;
    }
    if (node.op == "index") {
      lower_expr(*node.lhs, fn);
      // Struct byte_offset: rhs is a literal byte offset, add directly without scaling
      if (node.name == "byte_offset") {
        lower_expr(*node.rhs, fn);
        fn.code.push_back({"+", "", 0});
      } else {
        lower_expr(*node.rhs, fn);
        std::vector<long> parent_dims = get_node_dims(*node.lhs, fn);
        long mult = 1;
        if (parent_dims.size() > 1) {
          for (size_t i = 1; i < parent_dims.size(); ++i) {
            mult *= parent_dims[i];
          }
        }
        if (mult > 1) {
          fn.code.push_back({"const", "", mult});
          fn.code.push_back({"*", "", 0});
        }
        std::string base_name = get_base_var_name(node);
        std::string local_key = fn.name + "$" + base_name;
        int base_size = target_scale;
        if (local_array_base_sizes.count(local_key)) {
          base_size = local_array_base_sizes[local_key];
        } else if (global_array_base_sizes.count(base_name)) {
          base_size = global_array_base_sizes[base_name];
        } else {
          int scale = get_expr_pointer_scale(*node.lhs, fn);
          if (scale > 0) base_size = scale;
        }
        fn.code.push_back({"const", "", static_cast<long>(base_size)});
        fn.code.push_back({"*", "", 0});
        fn.code.push_back({"+", "", 0});
      }
    } else if (node.op == "var") {
      if (fn.locals.count(node.name)) {
        fn.code.push_back({"addr", "", fn.locals[node.name]});
      } else if (global_vars.count(node.name) || global_arrays.count(node.name)) {
        fn.code.push_back({"gaddr", node.name, 0});
      } else {
        Diagnostics::error(node.line, node.col, "unknown variable " + node.name);
      }
    } else {
      Diagnostics::error(node.line, node.col, "lvalue required");
    }
  }

  static void lower_stmt(const Node &stmt, IrFunction &fn, int target_scale) {
    if (stmt.op == "block") {
      lower_block(stmt, fn, target_scale);
    } else if (stmt.op == "decl") {
      if (fn.locals.count(stmt.name))
        Diagnostics::error(stmt.line, stmt.col, "duplicate local " + stmt.name);
      fn.locals[stmt.name] = static_cast<int>(fn.locals.size());
      if (stmt.lhs) {
        lower_expr(*stmt.lhs, fn);
        fn.code.push_back({"store", "", fn.locals[stmt.name]});
      }
    } else if (stmt.op == "assign") {
      lower_expr(*stmt.lhs, fn);
      if (fn.locals.count(stmt.name)) {
        fn.code.push_back({"store", "", fn.locals[stmt.name]});
      } else if (global_vars.count(stmt.name)) {
        fn.code.push_back({"gstore", stmt.name, 0});
      } else {
        Diagnostics::error(stmt.line, stmt.col, "unknown variable " + stmt.name);
      }
    } else if (stmt.op == "return") {
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"ret", "", 0});
    } else if (stmt.op == "expr") {
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"pop", "", 0});
    } else if (stmt.op == "if") {
      std::string else_label = ".L" + fn.name + "_else" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_endif" + std::to_string(fn.label_id++);
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", else_label, 0});
      lower_stmt(*stmt.body[0], fn, target_scale);
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", else_label, 0});
      if (stmt.body.size() > 1)
        lower_stmt(*stmt.body[1], fn, target_scale);
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "while") {
      std::string start_label = ".L" + fn.name + "_while" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_endwhile" + std::to_string(fn.label_id++);
      fn.code.push_back({"label", start_label, 0});
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", end_label, 0});
      loop_contexts.push_back({end_label, start_label});
      lower_stmt(*stmt.body[0], fn, target_scale);
      loop_contexts.pop_back();
      fn.code.push_back({"jmp", start_label, 0});
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "for") {
      std::string start_label = ".L" + fn.name + "_for" + std::to_string(fn.label_id++);
      std::string step_label = ".L" + fn.name + "_forstep" + std::to_string(fn.label_id++);
      std::string end_label = ".L" + fn.name + "_endfor" + std::to_string(fn.label_id++);
      lower_stmt(*stmt.body[0], fn, target_scale);
      fn.code.push_back({"label", start_label, 0});
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", end_label, 0});
      loop_contexts.push_back({end_label, step_label});
      lower_stmt(*stmt.body[2], fn, target_scale);
      loop_contexts.pop_back();
      fn.code.push_back({"label", step_label, 0});
      lower_stmt(*stmt.body[1], fn, target_scale);
      fn.code.push_back({"jmp", start_label, 0});
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "break") {
      if (loop_contexts.empty())
        Diagnostics::error(stmt.line, stmt.col, "break statement not within loop or switch");
      fn.code.push_back({"jmp", loop_contexts.back().break_label, 0});
    } else if (stmt.op == "continue") {
      std::string cont_label = "";
      for (auto it = loop_contexts.rbegin(); it != loop_contexts.rend(); ++it) {
        if (!it->continue_label.empty()) {
          cont_label = it->continue_label;
          break;
        }
      }
      if (cont_label.empty())
        Diagnostics::error(stmt.line, stmt.col, "continue statement not within loop");
      fn.code.push_back({"jmp", cont_label, 0});
    } else if (stmt.op == "switch") {
      std::string end_label = ".L" + fn.name + "_endswitch" + std::to_string(fn.label_id++);
      std::vector<std::pair<long, std::string>> cases;
      std::string default_label = "";
      collect_cases(stmt, cases, default_label, fn);
      
      int temp_slot = static_cast<int>(fn.locals.size());
      std::string temp_name = ".Lswitch_temp_" + std::to_string(temp_slot);
      fn.locals[temp_name] = temp_slot;
      
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"store", "", temp_slot});
      
      for (const auto &c : cases) {
        fn.code.push_back({"load", "", temp_slot});
        fn.code.push_back({"const", "", c.first});
        fn.code.push_back({"==", "", 0});
        fn.code.push_back({"!", "", 0});
        fn.code.push_back({"jz", c.second, 0});
      }
      if (!default_label.empty()) {
        fn.code.push_back({"jmp", default_label, 0});
      } else {
        fn.code.push_back({"jmp", end_label, 0});
      }
      
      loop_contexts.push_back({end_label, ""});
      lower_stmt(*stmt.body[0], fn, target_scale);
      loop_contexts.pop_back();
      
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "case" || stmt.op == "default") {
      fn.code.push_back({"label", stmt.name, 0});
    } else if (stmt.op == "array_decl") {
      if (fn.locals.count(stmt.name))
        Diagnostics::error(stmt.line, stmt.col, "duplicate local " + stmt.name);
      int base_slot = static_cast<int>(fn.locals.size());
      fn.locals[stmt.name] = base_slot;
      std::string local_key = fn.name + "$" + stmt.name;
      int base_size = target_scale;
      if (local_array_base_sizes.count(local_key)) {
        base_size = local_array_base_sizes[local_key];
      }
      long buf_size = stmt.value * base_size;
      long num_slots = (buf_size + 15) / 16;
      int buf_start_slot = static_cast<int>(fn.locals.size());
      for (long i = 0; i < num_slots; ++i) {
        fn.locals[stmt.name + "$buf" + std::to_string(i)] = buf_start_slot + static_cast<int>(i);
      }
      fn.code.push_back({"addr", "", buf_start_slot + static_cast<int>(num_slots - 1)});
      fn.code.push_back({"store", "", base_slot});
      for (size_t i = 0; i < stmt.body.size(); ++i) {
        lower_expr(*stmt.body[i], fn);
        fn.code.push_back({"const", "", static_cast<long>(i)});
        fn.code.push_back({"load", "", base_slot});
        fn.code.push_back({"store_index", "", static_cast<long>(base_size)});
      }
    } else if (stmt.op == "store_index") {
      lower_expr(*stmt.rhs, fn);
      fn.code.push_back({"const", "", 0});
      lower_addr(*stmt.lhs, fn, target_scale);
      int scale = get_expr_pointer_scale(*stmt.lhs, fn);
      if (scale == 0) {
        if (stmt.lhs->op == "index" || stmt.lhs->op == "unary_*") {
          scale = get_expr_pointer_scale(*stmt.lhs->lhs, fn);
        }
      }
      if (scale == 0) scale = target_scale;
      fn.code.push_back({"store_index", "", static_cast<long>(scale)});
    } else {
      Diagnostics::error(stmt.line, stmt.col, "unknown AST statement " + stmt.op);
    }
  }

  static IrFunction lower_func(const Node &ast, int target_scale) {
    IrFunction fn;
    fn.name = ast.name;
    fn.params = ast.params;
    fn.is_static = ast.is_static;
    for (const std::string &param : fn.params) {
      if (fn.locals.count(param))
        Diagnostics::error(ast.line, ast.col, "duplicate parameter " + param);
      fn.locals[param] = static_cast<int>(fn.locals.size());
    }
    lower_block(ast, fn, target_scale);
    return fn;
  }

  std::vector<IrFunction> lower_program(std::vector<std::unique_ptr<Node>> ast, const std::string &target) {
    std::vector<IrFunction> out;
    int target_scale = (target == "i386-b1nix" || target == "x86-b1nix") ? 4 : 8;
    current_target_scale = target_scale;
    for (const auto &func : ast)
      out.push_back(lower_func(*func, target_scale));
    return out;
  }
}

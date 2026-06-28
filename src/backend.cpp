#include "backend.h"
#include "backend_target.h"
#include "lexer.h"
#include "parser.h"
#include "diagnostics.h"
#include <sstream>
#include <iostream>
#include <memory>

namespace Backend {
  using namespace IR;

  static void dump_ast_node(const AST::Node &node, int indent) {
    std::string ind(indent * 2, ' ');
    std::cerr << ind << "Node(op=" << node.op;
    if (!node.name.empty()) std::cerr << ", name=" << node.name;
    if (node.value != 0) std::cerr << ", value=" << node.value;
    std::cerr << ")\n";
    if (node.lhs) dump_ast_node(*node.lhs, indent + 1);
    if (node.rhs) dump_ast_node(*node.rhs, indent + 1);
    for (const auto &child : node.body) {
      if (child) dump_ast_node(*child, indent + 1);
    }
  }

  static void dump_ast(const std::vector<std::unique_ptr<AST::Node>> &ast) {
    std::cerr << "=== AST DUMP ===\n";
    for (const auto &node : ast) {
      if (node) dump_ast_node(*node, 0);
    }
    std::cerr << "================\n";
  }

  static void dump_ir(const std::vector<IrFunction> &funcs) {
    std::cerr << "=== IR DUMP ===\n";
    for (const auto &fn : funcs) {
      std::cerr << "Function " << fn.name << ":\n";
      for (const auto &inst : fn.code) {
        std::cerr << "  " << inst.op;
        if (!inst.arg.empty()) std::cerr << " " << inst.arg;
        if (inst.value != 0) std::cerr << " " << inst.value;
        std::cerr << "\n";
      }
    }
    std::cerr << "================\n";
  }

  static std::string peephole_optimize(const std::string &asm_text, const std::string &target) {
    std::vector<std::string> lines;
    std::istringstream iss(asm_text);
    std::string line;
    while (std::getline(iss, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
      }
      lines.push_back(line);
    }

    std::vector<std::string> opt_lines;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].empty()) {
        opt_lines.push_back(lines[i]);
        continue;
      }

      bool optimized = false;
      if (i + 1 < lines.size()) {
        std::string cur = lines[i];
        std::string next = lines[i + 1];
        size_t cur_start = cur.find_first_not_of(" \t");
        size_t next_start = next.find_first_not_of(" \t");
        std::string cur_trim = (cur_start == std::string::npos) ? "" : cur.substr(cur_start);
        std::string next_trim = (next_start == std::string::npos) ? "" : next.substr(next_start);

        if (target == "arm64-darwin") {
          if (cur_trim.rfind("str ", 0) == 0 && cur_trim.find(", [sp, #-16]!") != std::string::npos &&
              next_trim.rfind("ldr ", 0) == 0 && next_trim.find(", [sp], #16") != std::string::npos) {
            std::string cur_reg = cur_trim.substr(4, cur_trim.find(",") - 4);
            std::string next_reg = next_trim.substr(4, next_trim.find(",") - 4);
            if (cur_reg == next_reg) {
              i++;
              optimized = true;
            }
          }
        } else if (target == "x86_64-b1nix") {
          if (cur_trim.rfind("pushq ", 0) == 0 && next_trim.rfind("popq ", 0) == 0) {
            std::string cur_reg = cur_trim.substr(6);
            std::string next_reg = next_trim.substr(5);
            if (cur_reg == next_reg) {
              i++;
              optimized = true;
            }
          }
        } else if (target == "i386-b1nix" || target == "x86-b1nix") {
          if (cur_trim.rfind("pushl ", 0) == 0 && next_trim.rfind("popl ", 0) == 0) {
            std::string cur_reg = cur_trim.substr(6);
            std::string next_reg = next_trim.substr(5);
            if (cur_reg == next_reg) {
              i++;
              optimized = true;
            }
          }
        }
      }

      if (!optimized) {
        opt_lines.push_back(lines[i]);
      }
    }

    std::ostringstream oss;
    for (const auto &l : opt_lines) {
      oss << l << "\n";
    }
    return oss.str();
  }

  std::string compile_asm(const std::string &src, const std::string &target, bool dump_ast_flag, bool dump_ir_flag) {
    global_decls.clear();
    global_vars.clear();
    global_arrays.clear();
    global_struct_vars.clear();
    global_array_dims.clear();
    local_array_dims.clear();
    global_array_base_sizes.clear();
    local_array_base_sizes.clear();
    global_var_elem_scales.clear();
    global_var_is_pointer.clear();
    local_var_elem_scales.clear();
    local_var_is_pointer.clear();
    function_return_aggregate_sizes.clear();
    function_param_aggregate_sizes.clear();
    function_vararg_fixed_counts.clear();

    int target_scale = (target == "i386-b1nix" || target == "x86-b1nix") ? 4 : 8;
    std::map<std::string, Preprocessor::Macro> macros = Preprocessor::driver_macros;
    std::set<std::string> included_files;
    included_files.insert(Diagnostics::filepath);
    std::string preprocessed_src = Preprocessor::preprocess(src, Diagnostics::filepath, Preprocessor::driver_include_dirs, macros, included_files);

    std::ostringstream out;
    auto tokens = Lexer::lex(preprocessed_src, macros);
    Parser::Parser parser(std::move(tokens), target_scale);
    auto ast = parser.parse();
    if (dump_ast_flag) {
      dump_ast(ast);
    }
    auto ir_functions = lower_program(std::move(ast), target);
    if (dump_ir_flag) {
      dump_ir(ir_functions);
    }

    std::unique_ptr<TargetBackend> backend;
    if (target == "arm64-darwin") {
      backend.reset(create_arm64_backend());
    } else if (target == "x86_64-b1nix") {
      backend.reset(create_x86_64_backend());
    } else if (target == "i386-b1nix" || target == "x86-b1nix") {
      backend.reset(create_i386_backend());
    } else {
      Diagnostics::fatal("unknown target " + target);
    }

    out << backend->emit_globals(global_decls);

    for (const IrFunction &fn : ir_functions) {
      out << backend->emit_function(fn);
    }

    return peephole_optimize(out.str(), target);
  }
}

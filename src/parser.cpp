#include "parser.h"
#include "diagnostics.h"
#include "ir.h"
#include <stdexcept>
#include <algorithm>

namespace Parser {
  using namespace AST;
  using namespace Lexer;

  Parser::Parser(std::vector<Lexer::Token> tokens, int target_scale)
      : tokens_(std::move(tokens)), target_scale_(target_scale) {}

  std::string Parser::resolve_name(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      if (it->count(name)) {
        return it->at(name);
      }
    }
    return name;
  }

  std::vector<std::unique_ptr<Node>> Parser::parse() {
    local_var_counter_ = 0;
    scopes_.clear();
    std::vector<std::unique_ptr<Node>> funcs;
    while (peek() != "EOF") {
      bool is_static = false;
      bool is_extern = false;
      while (peek() == "static" || peek() == "extern" || peek() == "inline" || peek() == "__inline" || peek() == "__inline__") {
        if (peek() == "static") {
          take("static");
          is_static = true;
        } else if (peek() == "extern") {
          take("extern");
          is_extern = true;
        } else {
          take();
        }
      }

      if (peek() == "typedef") {
        take("typedef");
        type();
        std::string alias = take();
        take(";");
        global_typedefs.insert(alias);
      } else if (is_function_decl()) {
        auto fn = function(is_static);
        if (fn) {
          funcs.push_back(std::move(fn));
        }
      } else {
        global_decl(is_static, is_extern);
      }
    }
    take("EOF");
    return funcs;
  }

  bool Parser::is_function_decl() {
    size_t p = pos_;
    while (p < tokens_.size()) {
      std::string t = tokens_[p].text;
      if (t == "const" || t == "volatile" || t == "inline" || t == "__inline" || t == "__inline__" || t == "register") {
        p++;
      } else if (t == "struct" || t == "enum") {
        p++;
        if (p < tokens_.size()) p++;
      } else if (t == "int" || t == "char" || t == "long" || t == "void" || t == "unsigned" || t == "signed" || t == "float" || t == "double" || global_typedefs.count(t)) {
        p++;
      } else if (t == "*") {
        p++;
      } else {
        break;
      }
    }
    p++;
    if (p < tokens_.size() && tokens_[p].text == "(") {
      return true;
    }
    return false;
  }

  void Parser::skip_attribute() {
    while (peek() == "__attribute__" || peek() == "__attribute") {
      take();
      if (peek() == "(") {
        take("(");
        int depth = 1;
        while (depth > 0 && pos_ < tokens_.size()) {
          if (peek() == "(") depth++;
          else if (peek() == ")") depth--;
          take();
        }
      }
    }
  }

  int Parser::parse_base_type() {
    skip_attribute();
    int base_size = target_scale_;
    while (peek() == "const" || peek() == "volatile" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
      if (peek() == "__attribute__" || peek() == "__attribute") {
        skip_attribute();
      } else {
        take();
      }
    }
    if (peek() == "unsigned" || peek() == "signed") {
      take();
      while (peek() == "const" || peek() == "volatile" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
        if (peek() == "__attribute__" || peek() == "__attribute") {
          skip_attribute();
        } else {
          take();
        }
      }
      if (peek() != "struct" && peek() != "enum" &&
          (peek() == "char" || peek() == "int" || peek() == "long" || peek() == "short" || peek() == "void" ||
           peek() == "float" || peek() == "double" || global_typedefs.count(peek()))) {
        // continue to parse the actual base type below
      } else {
        return target_scale_;
      }
    }
    while (peek() == "const" || peek() == "volatile" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
      if (peek() == "__attribute__" || peek() == "__attribute") {
        skip_attribute();
      } else {
        take();
      }
    }
    if (peek() == "struct") {
      take("struct");
      std::string tag;
      if (peek() != "{") {
        tag = take();
      }
      if (peek() == "{") {
        take("{");
        int offset = 0;
        std::map<std::string, int> fields;
        while (peek() != "}") {
          parse_base_type();
          while (true) {
            while (peek() == "*") {
              take("*");
            }
            std::string field_name = take();
            if (peek() == "[") {
              while (peek() == "[") {
                take("[");
                expr();
                take("]");
              }
            }
            fields[field_name] = offset;
            global_field_offsets[field_name] = offset;
            offset++;
            if (peek() == ",") {
              take(",");
            } else {
              break;
            }
          }
          take(";");
        }
        take("}");
        if (!tag.empty()) {
          global_structs[tag] = fields;
        }
        base_size = offset * target_scale_;
      } else {
        if (global_structs.count(tag)) {
          base_size = global_structs[tag].size() * target_scale_;
        } else {
          base_size = target_scale_;
        }
      }
    } else if (peek() == "enum") {
      take("enum");
      if (peek() != "{") {
        take();
      }
      if (peek() == "{") {
        take("{");
        int val = 0;
        while (true) {
          if (peek() == "}") {
            break;
          }
          std::string name = take();
          if (peek() == "=") {
            take("=");
            val = std::stoi(take());
          }
          global_enums[name] = val++;
          if (peek() == ",")
            take(",");
          else
            break;
        }
        take("}");
      }
      base_size = target_scale_;
    } else {
      std::string t = take();
      if (t == "char") {
        base_size = 1;
      } else if (t == "short") {
        base_size = 2;
      } else if (t == "int") {
        base_size = 4;
      } else if (t == "float") {
        base_size = 4;
      } else if (t == "double") {
        base_size = 8;
      } else if (t == "long") {
        if (peek() == "double") {
          take();
          base_size = 8;
        } else if (peek() == "long") {
          take();
          base_size = 8;
        } else {
          base_size = 8;
        }
      } else {
        base_size = target_scale_;
      }
    }
    return base_size;
  }

  void Parser::global_decl(bool is_static, bool is_extern) {
    int base_size = parse_base_type();
    if (peek() == ";") {
      take(";");
      return;
    }
    while (true) {
      int stars = 0;
      while (peek() == "*") {
        take("*");
        stars++;
      }
      skip_attribute();
      std::string name = take();
      skip_attribute();
      if (peek() == "__asm__" || peek() == "__asm" || peek() == "asm") {
        take();
        take("(");
        take();
        take(")");
      }
      bool is_pointer = (stars > 0);
      int elem_scale = 1;
      if (stars == 1) {
        elem_scale = base_size;
      } else if (stars > 1) {
        elem_scale = target_scale_;
      }

      if (peek() == "[") {
        std::vector<long> dims;
        while (peek() == "[") {
          take("[");
          long dim_size = 0;
          if (peek() != "]") {
            dim_size = std::stol(take());
          }
          take("]");
          dims.push_back(dim_size);
        }
        long total_size = 1;
        for (long d : dims) total_size *= d;

        std::vector<long> inits;
        if (peek() == "=") {
          take("=");
          take("{");
          while (true) {
            inits.push_back(eval_const(*expr()));
            if (peek() == ",")
              take(",");
            else
              break;
          }
          take("}");
        }
        if (total_size == 0) total_size = inits.size();
        if (!is_extern) {
          IR::IrGlobalVar g = {name, true, total_size, inits, is_static};
          IR::global_decls.push_back(g);
        }
        IR::global_arrays.insert(name);
        IR::global_array_dims[name] = dims;
        IR::global_array_base_sizes[name] = base_size;
      } else {
        std::vector<long> inits;
        if (peek() == "=") {
          take("=");
          inits.push_back(eval_const(*expr()));
        }
        if (!is_extern) {
          IR::IrGlobalVar g = {name, false, 1, inits, is_static};
          IR::global_decls.push_back(g);
        }
        IR::global_vars.insert(name);
        IR::global_var_is_pointer[name] = is_pointer;
        IR::global_var_elem_scales[name] = elem_scale;
      }
      skip_attribute();
      if (peek() == ",") {
        take(",");
      } else {
        break;
      }
    }
    take(";");
  }

  long Parser::eval_const(const Node &node) {
    if (node.op == "num") return node.value;
    if (node.op == "cast") return eval_const(*node.lhs);
    if (node.op == "unary_-") return -eval_const(*node.lhs);
    if (node.op == "unary_~") return ~eval_const(*node.lhs);
    if (node.op == "unary_!") return !eval_const(*node.lhs);
    if (node.op == "+") return eval_const(*node.lhs) + eval_const(*node.rhs);
    if (node.op == "-") return eval_const(*node.lhs) - eval_const(*node.rhs);
    if (node.op == "*") return eval_const(*node.lhs) * eval_const(*node.rhs);
    if (node.op == "/") {
      long divisor = eval_const(*node.rhs);
      if (divisor == 0) return 0;
      return eval_const(*node.lhs) / divisor;
    }
    if (node.op == "&") return eval_const(*node.lhs) & eval_const(*node.rhs);
    if (node.op == "|") return eval_const(*node.lhs) | eval_const(*node.rhs);
    if (node.op == "^") return eval_const(*node.lhs) ^ eval_const(*node.rhs);
    if (node.op == "<<") return eval_const(*node.lhs) << eval_const(*node.rhs);
    if (node.op == ">>") return eval_const(*node.lhs) >> eval_const(*node.rhs);
    if (node.op == "==") return eval_const(*node.lhs) == eval_const(*node.rhs);
    if (node.op == "!=") return eval_const(*node.lhs) != eval_const(*node.rhs);
    if (node.op == "<") return eval_const(*node.lhs) < eval_const(*node.rhs);
    if (node.op == ">") return eval_const(*node.lhs) > eval_const(*node.rhs);
    if (node.op == "<=") return eval_const(*node.lhs) <= eval_const(*node.rhs);
    if (node.op == ">=") return eval_const(*node.lhs) >= eval_const(*node.rhs);
    Diagnostics::error(node.line, node.col, "compile-time constant expression required");
    return 0;
  }

  void Parser::error(const std::string &msg) {
    const Token &tok = tokens_[pos_];
    std::cerr << "--- PARSER ERROR CONTEXT ---\n";
    std::cerr << "Current token index: " << pos_ << "\n";
    std::cerr << "Error message: " << msg << "\n";
    std::cerr << "Surrounding tokens:";
    size_t start = (pos_ >= 10) ? pos_ - 10 : 0;
    size_t end = std::min(pos_ + 10, tokens_.size());
    for (size_t i = start; i < end; ++i) {
      if (i == pos_) std::cerr << " >>>" << tokens_[i].text << "<<<";
      else std::cerr << " " << tokens_[i].text;
    }
    std::cerr << "\nRegistered typedefs:";
    for (const auto &t : global_typedefs) {
      std::cerr << " " << t;
    }
    std::cerr << "\n----------------------------\n";
    Diagnostics::error(tok.line, tok.col, msg);
  }

  const std::string &Parser::peek() const { return tokens_[pos_].text; }

  std::string Parser::take(const std::string &want) {
    const std::string got = peek();
    if (!want.empty() && got != want)
      error("expected '" + want + "', got '" + got + "'");
    ++pos_;
    return got;
  }

  void Parser::type() {
    parse_base_type();
    while (peek() == "*")
      take("*");
    skip_attribute();
  }

  std::unique_ptr<Node> Parser::create_node(const std::string &op, int line, int col) {
    auto node = std::make_unique<Node>();
    node->op = op;
    node->line = line;
    node->col = col;
    return node;
  }

  std::unique_ptr<Node> Parser::function(bool is_static) {
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;
    type();
    auto node = create_node("func", line, col);
    node->name = take();
    node->is_static = is_static;
    
    current_func_name_ = node->name;
    current_static_locals_.clear();
    scopes_.push_back({});

    take("(");
    if (peek() == "void" && tokens_[pos_ + 1].text == ")") {
      take("void");
    } else if (peek() != ")") {
      while (true) {
        if (peek() == "...") {
          take("...");
          break;
        }
        int base_size = parse_base_type();
        int stars = 0;
        while (peek() == "*") {
          take("*");
          stars++;
        }
        skip_attribute();
        std::string name;
        if (peek() == "(") {
          take("(");
          while (peek() == "*") {
            take("*");
            stars++;
          }
          if (peek() != ")") {
            name = take();
          }
          take(")");
          take("(");
          int depth = 1;
          while (depth > 0 && pos_ < tokens_.size()) {
            if (peek() == "(") depth++;
            else if (peek() == ")") depth--;
            take();
          }
        } else {
          name = take();
        }
        skip_attribute();
        bool array_param = false;
        while (peek() == "[") {
          array_param = true;
          take("[");
          int depth = 1;
          while (depth > 0 && pos_ < tokens_.size()) {
            if (peek() == "[") depth++;
            else if (peek() == "]") depth--;
            take();
          }
        }
        std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
        scopes_.back()[name] = unique_name;
        node->params.push_back(unique_name);
        
        bool is_pointer = (stars > 0 || array_param);
        int elem_scale = 1;
        if (stars == 1) {
          elem_scale = base_size;
        } else if (stars > 1) {
          elem_scale = target_scale_;
        }
        std::string local_key = node->name + "$" + unique_name;
        IR::local_var_is_pointer[local_key] = is_pointer;
        IR::local_var_elem_scales[local_key] = elem_scale;

        if (peek() != ",")
          break;
        take(",");
      }
    }
    take(")");
    skip_attribute();
    if (peek() == ";") {
      take(";");
      current_func_name_ = "";
      current_static_locals_.clear();
      scopes_.pop_back();
      return nullptr;
    }
    auto block = block_stmt();
    node->body = std::move(block->body);
    
    current_func_name_ = "";
    current_static_locals_.clear();
    scopes_.pop_back();
    return node;
  }

  std::unique_ptr<Node> Parser::block_stmt() {
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;
    take("{");
    scopes_.push_back({});
    auto node = create_node("block", line, col);
    while (peek() != "}")
      node->body.push_back(stmt());
    take("}");
    scopes_.pop_back();
    return node;
  }

  std::unique_ptr<Node> Parser::stmt() {
    if (peek() == "{") {
      return block_stmt();
    }
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;

    bool is_static = false;
    if (peek() == "static") {
      take("static");
      is_static = true;
    }

    if (peek() == "typedef") {
      take("typedef");
      type();
      std::string alias = take();
      take(";");
      global_typedefs.insert(alias);
      return create_node("block", line, col);
    }
    if (peek() == "enum") {
      take("enum");
      if (peek() != "{") {
        take();
      }
      take("{");
      int val = 0;
      while (true) {
        std::string name = take();
        if (peek() == "=") {
          take("=");
          val = std::stoi(take());
        }
        global_enums[name] = val++;
        if (peek() == ",")
          take(",");
        else
          break;
      }
      take("}");
      take(";");
      return create_node("block", line, col);
    }
    if (peek() == "struct" && tokens_[pos_ + 2].text == "{") {
      take("struct");
      std::string tag = take();
      take("{");
      int offset = 0;
      std::map<std::string, int> fields;
      while (peek() != "}") {
        type();
        std::string field_name = take();
        take(";");
        fields[field_name] = offset;
        global_field_offsets[field_name] = offset;
        offset++;
      }
      take("}");
      take(";");
      global_structs[tag] = fields;
      return create_node("block", line, col);
    }
    if (peek() == "struct" && tokens_[pos_ + 2].text != "{") {
      take("struct");
      std::string tag = take();
      std::string name = take();
      long struct_size = 1;
      if (global_structs.count(tag)) {
        struct_size = global_structs[tag].size();
      }
      take(";");
      if (is_static) {
        std::string unique_name = current_func_name_ + "$" + name;
        IR::IrGlobalVar g = {unique_name, true, struct_size, {}, true};
        IR::global_decls.push_back(g);
        IR::global_arrays.insert(unique_name);
        IR::global_array_dims[unique_name] = {struct_size};
        IR::global_array_base_sizes[unique_name] = target_scale_;
        current_static_locals_[name] = unique_name;
        return create_node("block", line, col);
      } else {
        std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
        if (scopes_.back().count(name)) {
          error("redefinition of local " + name);
        }
        scopes_.back()[name] = unique_name;
        auto node = create_node("array_decl", line, col);
        node->name = unique_name;
        node->value = struct_size;
        IR::local_array_dims[current_func_name_ + "$" + unique_name] = {struct_size};
        IR::local_array_base_sizes[current_func_name_ + "$" + unique_name] = target_scale_;
        return node;
      }
    }
    if (peek() == "int" || peek() == "char" || peek() == "long" ||
        peek() == "void" || peek() == "unsigned" || peek() == "signed" ||
        peek() == "const" || peek() == "volatile" || peek() == "register" ||
        peek() == "float" || peek() == "double" ||
        global_typedefs.count(peek())) {
      int base_size = parse_base_type();
      if (peek() == ";") {
        take(";");
        return create_node("block", line, col);
      }
      int stars = 0;
      while (peek() == "*") {
        take("*");
        stars++;
      }
      skip_attribute();
      std::string name = take();
      skip_attribute();
      if (peek() == "__asm__" || peek() == "__asm" || peek() == "asm") {
        take();
        take("(");
        take();
        take(")");
      }
      bool is_pointer = (stars > 0);
      int elem_scale = 1;
      if (stars == 1) {
        elem_scale = base_size;
      } else if (stars > 1) {
        elem_scale = target_scale_;
      }

      if (peek() == "[") {
        std::vector<long> dims;
        while (peek() == "[") {
          take("[");
          long dim_size = 0;
          if (peek() != "]") {
            dim_size = std::stol(take());
          }
          take("]");
          dims.push_back(dim_size);
        }
        long total_size = 1;
        for (long d : dims) total_size *= d;

        if (is_static) {
          std::string unique_name = current_func_name_ + "$" + name;
          std::vector<long> inits;
          if (peek() == "=") {
            take("=");
            take("{");
            while (true) {
              inits.push_back(eval_const(*expr()));
              if (peek() == ",")
                take(",");
              else
                break;
            }
            take("}");
          }
          if (total_size == 0) total_size = inits.size();
          IR::IrGlobalVar g = {unique_name, true, total_size, inits, true};
          IR::global_decls.push_back(g);
          IR::global_arrays.insert(unique_name);
          IR::global_array_dims[unique_name] = dims;
          IR::global_array_base_sizes[unique_name] = base_size;
          current_static_locals_[name] = unique_name;
          take(";");
          return create_node("block", line, col);
        } else {
          std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
          if (scopes_.back().count(name)) {
            error("redefinition of local " + name);
          }
          scopes_.back()[name] = unique_name;
          auto node = create_node("array_decl", line, col);
          node->name = unique_name;
          node->value = total_size;
          IR::local_array_dims[current_func_name_ + "$" + unique_name] = dims;
          IR::local_array_base_sizes[current_func_name_ + "$" + unique_name] = base_size;
          if (peek() == "=") {
            take("=");
            take("{");
            while (true) {
              node->body.push_back(expr());
              if (peek() == ",")
                take(",");
              else
                break;
            }
            take("}");
          }
          take(";");
          return node;
        }
      }

      if (is_static) {
        std::string unique_name = current_func_name_ + "$" + name;
        std::vector<long> inits;
        if (peek() == "=") {
          take("=");
          inits.push_back(eval_const(*expr()));
        }
        IR::IrGlobalVar g = {unique_name, false, 1, inits, true};
        IR::global_decls.push_back(g);
        IR::global_vars.insert(unique_name);
        IR::global_var_is_pointer[unique_name] = is_pointer;
        IR::global_var_elem_scales[unique_name] = elem_scale;
        current_static_locals_[name] = unique_name;
        take(";");
        return create_node("block", line, col);
      } else {
        std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
        if (scopes_.back().count(name)) {
          error("redefinition of local " + name);
        }
        scopes_.back()[name] = unique_name;
        auto node = create_node("decl", line, col);
        node->name = unique_name;
        std::string local_key = current_func_name_ + "$" + unique_name;
        IR::local_var_is_pointer[local_key] = is_pointer;
        IR::local_var_elem_scales[local_key] = elem_scale;
        if (peek() == "=") {
          take("=");
          node->lhs = expr();
        }
        if (peek() == ",") {
          auto block = create_node("block", line, col);
          block->body.push_back(std::move(node));
          while (peek() == ",") {
            take(",");
            int next_stars = 0;
            while (peek() == "*") {
              take("*");
              next_stars++;
            }
            skip_attribute();
            std::string next_name = take();
            skip_attribute();
            std::string next_unique_name = next_name + "$" + std::to_string(local_var_counter_++);
            if (scopes_.back().count(next_name)) {
              error("redefinition of local " + next_name);
            }
            scopes_.back()[next_name] = next_unique_name;
            auto next = create_node("decl", line, col);
            next->name = next_unique_name;
            bool next_is_pointer = (next_stars > 0);
            int next_elem_scale = next_stars == 1 ? base_size : (next_stars > 1 ? target_scale_ : 1);
            std::string next_local_key = current_func_name_ + "$" + next_unique_name;
            IR::local_var_is_pointer[next_local_key] = next_is_pointer;
            IR::local_var_elem_scales[next_local_key] = next_elem_scale;
            if (peek() == "=") {
              take("=");
              next->lhs = expr();
            }
            block->body.push_back(std::move(next));
          }
          take(";");
          return block;
        }
        take(";");
        return node;
      }
    }
    if (peek() == "return") {
      take("return");
      auto node = create_node("return", line, col);
      node->lhs = expr();
      take(";");
      return node;
    }
    if (peek() == "break") {
      take("break");
      auto node = create_node("break", line, col);
      take(";");
      return node;
    }
    if (peek() == "continue") {
      take("continue");
      auto node = create_node("continue", line, col);
      take(";");
      return node;
    }
    if (peek() == "switch") {
      take("switch");
      auto node = create_node("switch", line, col);
      take("(");
      node->lhs = expr();
      take(")");
      node->body.push_back(stmt());
      return node;
    }
    if (peek() == "case") {
      take("case");
      auto node = create_node("case", line, col);
      bool neg = false;
      if (peek() == "-") {
        take("-");
        neg = true;
      }
      node->value = std::stol(take());
      if (neg) node->value = -node->value;
      take(":");
      return node;
    }
    if (peek() == "default") {
      take("default");
      auto node = create_node("default", line, col);
      take(":");
      return node;
    }
    if (peek() == "__asm__" || peek() == "__asm" || peek() == "asm") {
      take();
      if (peek() == "volatile" || peek() == "__volatile__") {
        take();
      }
      take("(");
      int parens = 1;
      while (parens > 0 && peek() != "EOF") {
        std::string t = take();
        if (t == "(") parens++;
        if (t == ")") parens--;
      }
      take(";");
      return create_node("block", line, col);
    }
    if (peek() == "if") {
      take("if");
      auto node = create_node("if", line, col);
      take("(");
      node->lhs = expr();
      take(")");
      node->body.push_back(stmt());
      if (peek() == "else") {
        take("else");
        node->body.push_back(stmt());
      }
      return node;
    }
    if (peek() == "while") {
      take("while");
      auto node = create_node("while", line, col);
      take("(");
      node->lhs = expr();
      take(")");
      node->body.push_back(stmt());
      return node;
    }
    if (peek() == "for") {
      take("for");
      auto node = create_node("for", line, col);
      take("(");
      
      if (peek() == "int" || peek() == "char" || peek() == "long" ||
          peek() == "void" || peek() == "unsigned" || peek() == "signed" ||
          peek() == "const" || peek() == "volatile" || peek() == "register" ||
          peek() == "float" || peek() == "double" ||
          global_typedefs.count(peek())) {
        node->body.push_back(stmt());
      } else {
        bool init_assign = pos_ + 1 < tokens_.size() && 
          (tokens_[pos_ + 1].text == "=" || tokens_[pos_ + 1].text == "+=" || 
           tokens_[pos_ + 1].text == "-=" || tokens_[pos_ + 1].text == "*=" || 
           tokens_[pos_ + 1].text == "/=" || tokens_[pos_ + 1].text == "%=");
        if (init_assign) {
          node->body.push_back(assign_stmt(false));
        } else {
          auto init_expr = create_node("expr", line, col);
          init_expr->lhs = expr();
          node->body.push_back(std::move(init_expr));
        }
        take(";");
      }
      
      node->lhs = expr();
      take(";");
      
      bool step_assign = pos_ + 1 < tokens_.size() && 
        (tokens_[pos_ + 1].text == "=" || tokens_[pos_ + 1].text == "+=" || 
         tokens_[pos_ + 1].text == "-=" || tokens_[pos_ + 1].text == "*=" || 
         tokens_[pos_ + 1].text == "/=" || tokens_[pos_ + 1].text == "%=");
      if (step_assign) {
        node->body.push_back(assign_stmt(false));
      } else {
        auto step_expr = create_node("expr", line, col);
        step_expr->lhs = expr();
        node->body.push_back(std::move(step_expr));
      }
      take(")");
      node->body.push_back(stmt());
      return node;
    }
    if (peek() == "*") {
      size_t p = pos_;
      int parens = 0;
      bool has_eq = false;
      while (p < tokens_.size() && tokens_[p].text != ";") {
        if (tokens_[p].text == "(" || tokens_[p].text == "[") parens++;
        if (tokens_[p].text == ")" || tokens_[p].text == "]") parens--;
        if (tokens_[p].text == "=" && parens == 0) {
          has_eq = true;
          break;
        }
        p++;
      }
      if (has_eq) {
        int line = tokens_[pos_].line;
        int col = tokens_[pos_].col;
        auto lhs_node = expr();
        take("=");
        auto val_node = expr();
        take(";");
        auto node = create_node("store_index", line, col);
        node->lhs = std::move(lhs_node);
        node->rhs = std::move(val_node);
        return node;
      }
    }
    if (peek() != "EOF" && (std::isalpha(static_cast<unsigned char>(peek()[0])) || peek()[0] == '_') &&
        (tokens_[pos_ + 1].text == "[" || tokens_[pos_ + 1].text == "." || tokens_[pos_ + 1].text == "->")) {
      size_t p = pos_ + 1;
      while (p < tokens_.size()) {
        if (tokens_[p].text == "[") {
          int brackets = 0;
          while (p < tokens_.size()) {
            if (tokens_[p].text == "[") brackets++;
            if (tokens_[p].text == "]") brackets--;
            p++;
            if (brackets == 0) break;
          }
        } else if (tokens_[p].text == "." || tokens_[p].text == "->") {
          p += 2;
        } else {
          break;
        }
      }
      if (p < tokens_.size() && tokens_[p].text == "=") {
        auto lhs_node = expr();
        take("=");
        auto val_node = expr();
        take(";");
        auto node = create_node("store_index", line, col);
        node->lhs = std::move(lhs_node);
        node->rhs = std::move(val_node);
        return node;
      }
    }
    if (pos_ + 1 < tokens_.size() && (tokens_[pos_ + 1].text == "=" ||
                                     tokens_[pos_ + 1].text == "+=" ||
                                     tokens_[pos_ + 1].text == "-=" ||
                                     tokens_[pos_ + 1].text == "*=" ||
                                     tokens_[pos_ + 1].text == "/=" ||
                                     tokens_[pos_ + 1].text == "%=")) {
      return assign_stmt(true);
    }
    auto node = create_node("expr", line, col);
    node->lhs = expr();
    take(";");
    return node;
  }

  std::unique_ptr<Node> Parser::assign_stmt(bool semicolon) {
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;
    std::string name = take();
    if (current_static_locals_.count(name)) {
      name = current_static_locals_[name];
    } else {
      name = resolve_name(name);
    }
    std::string op = take();
    auto node = create_node("assign", line, col);
    node->name = name;
    if (op == "=") {
      node->lhs = expr();
    } else {
      std::string bin_op = op.substr(0, 1);
      auto bin_node = create_node(bin_op, line, col);
      auto var_node = create_node("var", line, col);
      var_node->name = name;
      bin_node->lhs = std::move(var_node);
      bin_node->rhs = expr();
      node->lhs = std::move(bin_node);
    }
    if (semicolon)
      take(";");
    return node;
  }

  std::unique_ptr<Node> Parser::expr() {
    auto cond = logical_or();
    if (peek() == "?") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      take("?");
      auto true_expr = expr();
      take(":");
      auto false_expr = expr();
      auto node = create_node("?", line, col);
      node->lhs = std::move(cond);
      node->body.push_back(std::move(true_expr));
      node->body.push_back(std::move(false_expr));
      return node;
    }
    return cond;
  }

  std::unique_ptr<Node> Parser::logical_or() {
    auto node = logical_and();
    while (peek() == "||") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = logical_and();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::logical_and() {
    auto node = bitwise_or();
    while (peek() == "&&") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = bitwise_or();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::bitwise_or() {
    auto node = bitwise_xor();
    while (peek() == "|") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = bitwise_xor();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::bitwise_xor() {
    auto node = bitwise_and();
    while (peek() == "^") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = bitwise_and();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::bitwise_and() {
    auto node = equality();
    while (peek() == "&") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = equality();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::equality() {
    auto node = relational();
    while (peek() == "==" || peek() == "!=") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = relational();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::relational() {
    auto node = shift();
    while (peek() == "<" || peek() == ">" || peek() == "<=" || peek() == ">=") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = shift();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::shift() {
    auto node = add();
    while (peek() == "<<" || peek() == ">>") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = add();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::add() {
    auto node = term();
    while (peek() == "+" || peek() == "-") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = term();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::term() {
    auto node = factor();
    while (peek() == "*" || peek() == "/" || peek() == "%") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto parent = create_node(take(), line, col);
      parent->lhs = std::move(node);
      parent->rhs = factor();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> Parser::factor() {
    auto node = unary();
    while (peek() == "[" || peek() == "." || peek() == "->" || peek() == "++" || peek() == "--") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      std::string op = take();
      if (op == "++" || op == "--") {
        if (node->op != "var")
          Diagnostics::error(line, col, "lvalue required as increment operand");
        auto parent = create_node("postfix_" + op, line, col);
        parent->name = node->name;
        node = std::move(parent);
      } else if (op == "[") {
        auto parent = create_node("index", line, col);
        parent->lhs = std::move(node);
        parent->rhs = expr();
        take("]");
        node = std::move(parent);
      } else {
        std::string field_name = take();
        auto parent = create_node("index", line, col);
        parent->lhs = std::move(node);
        int offset = 0;
        if (global_field_offsets.count(field_name)) {
          offset = global_field_offsets[field_name];
        }
        auto index_val = create_node("num", line, col);
        index_val->value = offset;
        parent->rhs = std::move(index_val);
        node = std::move(parent);
      }
    }
    return node;
  }

  std::unique_ptr<Node> Parser::unary() {
    if (peek() == "++" || peek() == "--") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      std::string op = take();
      if (peek()[0] != '_' && !std::isalpha(static_cast<unsigned char>(peek()[0])))
        Diagnostics::error(line, col, "lvalue required as increment operand");
      auto node = create_node("prefix_" + op, line, col);
      std::string var_name = take();
      if (current_static_locals_.count(var_name)) {
        var_name = current_static_locals_[var_name];
      } else {
        var_name = resolve_name(var_name);
      }
      node->name = var_name;
      return node;
    }
    if (peek() == "~" || peek() == "!" || peek() == "-" || peek() == "*" || peek() == "&") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto node = create_node("unary_" + take(), line, col);
      node->lhs = unary();
      return node;
    }
    return primary();
  }

  std::unique_ptr<Node> Parser::primary() {
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;
    if (peek() == "(") {
      bool is_cast = false;
      std::string next_tok = tokens_[pos_ + 1].text;
      if (next_tok == "int" || next_tok == "char" || next_tok == "long" || next_tok == "void" ||
          next_tok == "unsigned" || next_tok == "signed" ||
          next_tok == "const" || next_tok == "volatile" ||
          next_tok == "float" || next_tok == "double" ||
          global_typedefs.count(next_tok)) {
        is_cast = true;
      } else if (next_tok == "struct" || next_tok == "enum") {
        is_cast = true;
      }
      
      if (is_cast) {
        take("(");
        type();
        take(")");
        auto node = create_node("cast", line, col);
        node->lhs = primary();
        return node;
      }
      
      take("(");
      auto node = expr();
      take(")");
      return node;
    }
    if (peek() == "__builtin_constant_p") {
      take("__builtin_constant_p");
      take("(");
      expr();
      take(")");
      auto node = create_node("num", line, col);
      node->value = 0;
      return node;
    }
    if (std::isdigit(static_cast<unsigned char>(peek()[0]))) {
      auto node = create_node("num", line, col);
      for (char c : take())
        node->value = node->value * 10 + (c - '0');
      return node;
    }
    if (peek()[0] == '\'') {
      std::string lit = take();
      std::string content = lit.substr(1, lit.size() - 2);
      long val = 0;
      if (content.rfind("\\", 0) == 0) {
        if (content == "\\0") val = 0;
        else if (content == "\\n") val = 10;
        else if (content == "\\r") val = 13;
        else if (content == "\\t") val = 9;
        else if (content == "\\\\") val = '\\';
        else if (content == "\\'") val = '\'';
        else if (content == "\\\"") val = '\"';
        else Diagnostics::error(line, col, "unknown escape sequence " + content);
      } else if (content.size() == 1) {
        val = content[0];
      } else {
        Diagnostics::error(line, col, "invalid character literal " + lit);
      }
      auto node = create_node("num", line, col);
      node->value = val;
      return node;
    }
    if (peek()[0] == '"') {
      auto node = create_node("str", line, col);
      node->name = take();
      return node;
    }
    if (std::isalpha(static_cast<unsigned char>(peek()[0])) || peek()[0] == '_') {
      std::string ident = take();
      if (peek() == "(") {
        auto node = create_node("call", line, col);
        node->name = resolve_name(ident);
        take("(");
        if (peek() != ")") {
          while (true) {
            node->body.push_back(expr());
            if (peek() != ",")
              break;
            take(",");
          }
        }
        take(")");
        return node;
      }
      if (current_static_locals_.count(ident)) {
        ident = current_static_locals_[ident];
      } else {
        ident = resolve_name(ident);
      }
      if (global_enums.count(ident)) {
        auto node = create_node("num", line, col);
        node->value = global_enums[ident];
        return node;
      }
      auto node = create_node("var", line, col);
      node->name = ident;
      return node;
    }
    error("expected expression, got '" + peek() + "'");
  }
}

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

  std::string Parser::infer_struct_tag(const AST::Node &node) const {
    if (node.op == "var") {
      auto it = var_struct_tags_.find(node.name);
      return it == var_struct_tags_.end() ? "" : it->second;
    }
    if (node.op == "index" && !node.type_tag.empty()) {
      return node.type_tag;
    }
    if (node.op == "unary_*" && node.lhs) {
      return infer_struct_tag(*node.lhs);
    }
    return "";
  }

  std::vector<std::unique_ptr<Node>> Parser::parse() {
    local_var_counter_ = 0;
    scopes_.clear();
    unsigned_vars_.clear();
    bool_vars_.clear();
    value_sizes_.clear();
    var_struct_tags_.clear();
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
      if (t == "const" || t == "volatile" || t == "restrict" || t == "__restrict" || t == "__restrict__" ||
          t == "inline" || t == "__inline" || t == "__inline__" || t == "register") {
        p++;
      } else if (t == "struct" || t == "enum") {
        p++;
        if (p < tokens_.size()) p++;
      } else if (t == "int" || t == "char" || t == "short" || t == "long" || t == "void" || t == "unsigned" || t == "signed" || t == "_Bool" || t == "float" || t == "double" || global_typedefs.count(t)) {
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
    while (peek() == "__attribute__" || peek() == "__attribute" ||
           peek() == "const" || peek() == "volatile" || peek() == "restrict" ||
           peek() == "__restrict" || peek() == "__restrict__") {
      std::string attr = take();
      if ((attr == "__attribute__" || attr == "__attribute") && peek() == "(") {
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
    last_type_unsigned_ = false;
    last_type_bool_ = false;
    while (peek() == "const" || peek() == "volatile" || peek() == "restrict" || peek() == "__restrict" || peek() == "__restrict__" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
      if (peek() == "__attribute__" || peek() == "__attribute") {
        skip_attribute();
      } else {
        take();
      }
    }
    if (peek() == "unsigned" || peek() == "signed") {
      last_type_unsigned_ = (peek() == "unsigned");
      take();
      while (peek() == "const" || peek() == "volatile" || peek() == "restrict" || peek() == "__restrict" || peek() == "__restrict__" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
        if (peek() == "__attribute__" || peek() == "__attribute") {
          skip_attribute();
        } else {
          take();
        }
      }
      if (peek() != "struct" && peek() != "enum" &&
          (peek() == "char" || peek() == "int" || peek() == "long" || peek() == "short" || peek() == "void" || peek() == "_Bool" ||
           peek() == "float" || peek() == "double" || global_typedefs.count(peek()))) {
        // continue to parse the actual base type below
      } else {
        return target_scale_;
      }
    }
    while (peek() == "const" || peek() == "volatile" || peek() == "restrict" || peek() == "__restrict" || peek() == "__restrict__" || peek() == "register" || peek() == "__attribute__" || peek() == "__attribute") {
      if (peek() == "__attribute__" || peek() == "__attribute") {
        skip_attribute();
      } else {
        take();
      }
    }
    if (peek() == "struct" || peek() == "union") {
      bool is_union = (peek() == "union");
      take();
      std::string tag;
      if (peek() != "{") {
        tag = take();
      }
      if (peek() == "{") {
        take("{");
        int offset = 0;
        int union_max_size = 0;
        std::map<std::string, int> fields;
        while (peek() != "}") {
          std::string f_tag = "";
          if (peek() == "struct" || peek() == "union") {
            if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text != "{") {
              f_tag = tokens_[pos_ + 1].text;
            }
          }
          int field_base_size = parse_base_type();
          while (true) {
            int stars = 0;
            while (peek() == "*") {
              take("*");
              stars++;
            }
            int field_size = (stars > 0) ? target_scale_ : field_base_size;
            std::string field_name = take();
            long arr_count = 1;
            std::vector<long> field_dims;
            if (peek() == "[") {
              while (peek() == "[") {
                take("[");
                long dim = 0;
                if (peek() != "]") {
                  dim = eval_const(*expr());
                  arr_count *= dim;
                }
                take("]");
                field_dims.push_back(dim);
              }
            }
            // Align field offset to field_size boundary (but max 8)
            int align = field_size;
            if (align > 8) align = 8;
            if (!is_union && align > 1 && (offset % align) != 0) {
              offset += align - (offset % align);
            }
            if (!tag.empty() && !f_tag.empty() && stars == 0) {
              global_struct_field_tags[tag + "." + field_name] = f_tag;
            }
            fields[field_name] = offset;
            global_field_offsets[field_name] = offset;
            global_field_sizes[field_name] = field_size;
            int this_field_total = (int)(field_size * arr_count);
            if (!tag.empty()) {
              std::string key = tag + "." + field_name;
              global_struct_field_offsets_by_tag[key] = offset;
              global_struct_field_sizes_by_tag[key] = field_size;
              global_struct_field_total_sizes_by_tag[key] = this_field_total;
              global_struct_field_dims_by_tag[key] = field_dims;
            }
            if (is_union) {
              if (this_field_total > union_max_size) union_max_size = this_field_total;
            } else {
              offset += this_field_total;
            }
            if (peek() == ",") {
              take(",");
            } else {
              break;
            }
          }
          take(";");
        }
        take("}");
        if (is_union) {
          base_size = union_max_size;
        } else {
          // Round total struct size up to alignment of largest field (simplified: to target_scale_)
          if (offset > 0 && (offset % target_scale_) != 0) {
            offset += target_scale_ - (offset % target_scale_);
          }
          base_size = offset;
        }
        if (!tag.empty()) {
          global_structs[tag] = fields;
          global_struct_sizes[tag] = base_size;
        }
      } else {
        if (global_struct_sizes.count(tag)) {
          base_size = global_struct_sizes[tag];
        } else if (global_structs.count(tag)) {
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
      } else if (t == "_Bool") {
        base_size = 1;
        last_type_bool_ = true;
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
    std::string struct_tag = "";
    if (peek() == "struct" || peek() == "union") {
      if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text != "{") {
        struct_tag = tokens_[pos_ + 1].text;
      }
    }
    int base_size = parse_base_type();
    bool type_unsigned = last_type_unsigned_;
    bool type_bool = last_type_bool_;
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
      std::string name;
      bool is_func_ptr = false;
      if (peek() == "(") {
        take("(");
        while (peek() == "*") { take("*"); }
        name = take();
        take(")");
        take("(");
        int depth = 1;
        while (depth > 0 && pos_ < tokens_.size()) {
          if (peek() == "(") depth++;
          else if (peek() == ")") depth--;
          take();
        }
        is_func_ptr = true;
      } else {
        name = take();
      }
      skip_attribute();
      if (peek() == "__asm__" || peek() == "__asm" || peek() == "asm") {
        take();
        take("(");
        take();
        take(")");
      }
      bool is_pointer = (stars > 0 || is_func_ptr);
      int elem_scale = 1;
      if (stars == 1 && !is_func_ptr) {
        elem_scale = base_size;
      } else if (stars > 1 || is_func_ptr) {
        elem_scale = target_scale_;
      }

      std::vector<long> dims;
      if (peek() == "[") {
        while (peek() == "[") {
          take("[");
          long dim_size = 0;
          if (peek() != "]") {
            dim_size = std::stol(take());
          }
          take("]");
          dims.push_back(dim_size);
        }
      }

      long total_size = 1;
      for (long d : dims) total_size *= d;
      if (stars > 0) {
        base_size = target_scale_;
      }
      long total_byte_size = total_size * base_size;

      std::vector<InitElement> parsed_inits;
      if (peek() == "=") {
        take("=");
        if (peek() == "{") {
          parse_aggregate_init(0, struct_tag, dims, 0, base_size, parsed_inits);
        } else {
          auto init = expr();
          if (type_bool && stars == 0) init = bool_normalize(std::move(init));
          parsed_inits.push_back({0, std::move(init), base_size});
        }
      }

      if (!dims.empty()) {
        std::vector<long> inits;
        int elem_size = (stars > 0) ? target_scale_ : base_size;
        int align_val = (elem_size >= 8) ? 3 : (elem_size == 4) ? 2 : (elem_size == 2) ? 1 : 0;
        if (!parsed_inits.empty()) {
          if (stars > 0 || (struct_tag.empty() && dims.size() == 1)) {
            for (auto &item : parsed_inits) {
              inits.push_back(eval_const(*item.val));
            }
            if (total_size == 0) total_size = inits.size();
          } else {
            if (total_size == 0) {
              long max_offset = 0;
              for (const auto &item : parsed_inits) {
                if (item.offset + item.size > max_offset) {
                  max_offset = item.offset + item.size;
                }
              }
              total_byte_size = max_offset;
              total_size = (total_byte_size + base_size - 1) / base_size;
            }
            std::vector<long> byte_buf(total_byte_size, 0);
            for (const auto &item : parsed_inits) {
              long val = eval_const(*item.val);
              for (int b = 0; b < item.size; ++b) {
                if (item.offset + b < total_byte_size) {
                  byte_buf[item.offset + b] = (val >> (b * 8)) & 0xff;
                }
              }
            }
            inits = byte_buf;
            elem_size = 1;
          }
        }
        if (total_size == 0) total_size = 1;
        if (!is_extern) {
          IR::IrGlobalVar g = {name, true, total_size, inits, is_static, elem_size, align_val};
          IR::global_decls.push_back(g);
        }
        IR::global_arrays.insert(name);
        IR::global_array_dims[name] = dims;
        IR::global_array_base_sizes[name] = base_size;
        IR::global_var_is_pointer[name] = stars > 0;
        IR::global_var_elem_scales[name] = elem_scale;
        if (!struct_tag.empty()) {
          var_struct_tags_[name] = struct_tag;
        }
        bool_vars_[name] = type_bool && stars == 0;
        value_sizes_[name] = total_size * base_size;
      } else {
        std::vector<long> inits;
        int elem_size = (stars > 0) ? target_scale_ : base_size;
        int align_val = (elem_size >= 8) ? 3 : (elem_size == 4) ? 2 : (elem_size == 2) ? 1 : 0;
        if (!parsed_inits.empty()) {
          if (stars > 0 || struct_tag.empty()) {
            inits.push_back(eval_const(*parsed_inits[0].val));
          } else {
            std::vector<long> byte_buf(total_byte_size, 0);
            for (const auto &item : parsed_inits) {
              long val = eval_const(*item.val);
              for (int b = 0; b < item.size; ++b) {
                if (item.offset + b < total_byte_size) {
                  byte_buf[item.offset + b] = (val >> (b * 8)) & 0xff;
                }
              }
            }
            inits = byte_buf;
            elem_size = 1;
          }
        }
        if (!is_extern) {
          IR::IrGlobalVar g = {name, false, 1, inits, is_static, elem_size, align_val};
          IR::global_decls.push_back(g);
        }
        IR::global_vars.insert(name);
        if (!struct_tag.empty() && stars == 0) {
          IR::global_struct_vars.insert(name);
        }
        if (!struct_tag.empty()) {
          var_struct_tags_[name] = struct_tag;
        }
        IR::global_var_is_pointer[name] = is_pointer;
        IR::global_var_elem_scales[name] = elem_scale;
        unsigned_vars_[name] = type_unsigned && !is_pointer;
        bool_vars_[name] = type_bool && !is_pointer;
        value_sizes_[name] = is_pointer ? target_scale_ : base_size;
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

  void Parser::parse_aggregate_init(long base_offset, const std::string &struct_tag, const std::vector<long> &array_dims, size_t dim_idx, int base_type_size, std::vector<InitElement> &inits) {
    parse_aggregate_init_internal(base_offset, struct_tag, array_dims, dim_idx, base_type_size, inits, true);
  }

  void Parser::parse_aggregate_init_internal(long base_offset, const std::string &struct_tag, const std::vector<long> &array_dims, size_t dim_idx, int base_type_size, std::vector<InitElement> &inits, bool has_braces) {
    if (has_braces) {
      take("{");
    }
    struct FieldInfo {
      std::string name;
      long offset;
      int size;
      std::string tag;
      std::vector<long> dims;
    };
    std::vector<FieldInfo> struct_fields;
    if (!struct_tag.empty() && global_structs.count(struct_tag)) {
      for (const auto &pair : global_structs[struct_tag]) {
        std::string key = struct_tag + "." + pair.first;
        int sz = global_struct_field_sizes_by_tag.count(key) ? global_struct_field_sizes_by_tag[key] :
                 (global_field_sizes.count(pair.first) ? global_field_sizes[pair.first] : target_scale_);
        std::string field_tag = global_struct_field_tags.count(key) ? global_struct_field_tags[key] : "";
        std::vector<long> field_dims = global_struct_field_dims_by_tag.count(key) ? global_struct_field_dims_by_tag[key] : std::vector<long>{};
        struct_fields.push_back({pair.first, (long)pair.second, sz, field_tag, field_dims});
      }
      std::sort(struct_fields.begin(), struct_fields.end(), [](const FieldInfo &a, const FieldInfo &b) {
        return a.offset < b.offset;
      });
    }

    long elem_size = base_type_size;
    if (!array_dims.empty() && dim_idx + 1 < array_dims.size()) {
      long sub_array_elems = 1;
      for (size_t d = dim_idx + 1; d < array_dims.size(); ++d) {
        sub_array_elems *= array_dims[d];
      }
      elem_size = sub_array_elems * base_type_size;
    }

    size_t field_idx = 0;
    long array_idx = 0;

    while (true) {
      if (peek() == "}") {
        break;
      }
      if (!has_braces) {
        if (!struct_fields.empty() && field_idx >= struct_fields.size()) {
          break;
        }
        if (!array_dims.empty() && array_idx >= array_dims[dim_idx]) {
          break;
        }
      }

      long item_offset = 0;
      int item_size = 0;
      std::string item_struct_tag = "";
      std::vector<long> item_array_dims;
      size_t item_dim_idx = 0;
      int item_base_size = base_type_size;

      if (peek() == ".") {
        take(".");
        std::string field_name = take();
        take("=");
        bool found = false;
        for (size_t idx = 0; idx < struct_fields.size(); ++idx) {
          if (struct_fields[idx].name == field_name) {
            item_offset = struct_fields[idx].offset;
            item_size = struct_fields[idx].size;
            item_struct_tag = struct_fields[idx].tag;
            item_array_dims = struct_fields[idx].dims;
            item_base_size = item_size;
            field_idx = idx + 1;
            found = true;
            break;
          }
        }
        if (!found) error("struct " + struct_tag + " has no field " + field_name);
      } else if (peek() == "[") {
        take("[");
        long idx_val = std::strtol(take().c_str(), nullptr, 0);
        take("]");
        take("=");
        item_offset = idx_val * elem_size;
        item_size = (int)elem_size;
        array_idx = idx_val + 1;
        if (!array_dims.empty() && dim_idx + 1 < array_dims.size()) {
          item_array_dims = array_dims;
          item_dim_idx = dim_idx + 1;
          item_struct_tag = "";
          item_base_size = base_type_size;
        } else {
          item_struct_tag = struct_tag;
          item_base_size = base_type_size;
        }
      } else {
        if (!struct_fields.empty()) {
          if (field_idx >= struct_fields.size()) {
            error("excess initializers for struct " + struct_tag);
          }
          item_offset = struct_fields[field_idx].offset;
          item_size = struct_fields[field_idx].size;
          item_struct_tag = struct_fields[field_idx].tag;
          item_array_dims = struct_fields[field_idx].dims;
          item_base_size = item_size;
          field_idx++;
        } else if (!array_dims.empty()) {
          item_offset = array_idx * elem_size;
          item_size = (int)elem_size;
          array_idx++;
          if (dim_idx + 1 < array_dims.size()) {
            item_array_dims = array_dims;
            item_dim_idx = dim_idx + 1;
            item_struct_tag = "";
            item_base_size = base_type_size;
          } else {
            item_struct_tag = struct_tag;
            item_base_size = base_type_size;
          }
        } else {
          error("excess initializers for primitive type");
        }
      }

      if (peek() == "{") {
        parse_aggregate_init_internal(base_offset + item_offset, item_struct_tag, item_array_dims, item_dim_idx, item_base_size, inits, true);
      } else if (!item_array_dims.empty() && item_dim_idx < item_array_dims.size()) {
        parse_aggregate_init_internal(base_offset + item_offset, item_struct_tag, item_array_dims, item_dim_idx, item_base_size, inits, false);
      } else if (!item_struct_tag.empty() && global_structs.count(item_struct_tag)) {
        parse_aggregate_init_internal(base_offset + item_offset, item_struct_tag, item_array_dims, item_dim_idx, item_base_size, inits, false);
      } else {
        inits.push_back({base_offset + item_offset, expr(), item_size});
      }

      if (peek() == ",") {
        if (!has_braces) {
          if (!struct_fields.empty() && field_idx >= struct_fields.size()) {
            break;
          }
          if (!array_dims.empty() && array_idx >= array_dims[dim_idx]) {
            break;
          }
        }
        take(",");
      } else {
        break;
      }
    }
    if (has_braces) {
      take("}");
    }
  }

  long Parser::eval_const(const Node &node) {
    if (node.op == "num") return node.value;
    if (node.op == "bool_cast") return eval_const(*node.lhs) != 0;
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

  long Parser::sizeof_expr(const Node &node) const {
    if (node.op == "num") return 4;
    if (node.op == "str") return target_scale_;
    if (node.op == "cast") return node.value > 0 ? node.value : target_scale_;
    if (node.op == "var") {
      auto it = value_sizes_.find(node.name);
      if (it != value_sizes_.end()) return it->second;
      return target_scale_;
    }
    if (node.op == "unary_&") return target_scale_;
    if (node.op == "unary_*") {
      int scale = target_scale_;
      if (node.lhs) {
        std::string base;
        const Node *cur = node.lhs.get();
        while (cur && cur->op == "index") cur = cur->lhs.get();
        if (cur && cur->op == "var") base = cur->name;
        std::string local_key = current_func_name_ + "$" + base;
        if (IR::local_var_elem_scales.count(local_key))
          scale = IR::local_var_elem_scales[local_key];
        else if (IR::global_var_elem_scales.count(base))
          scale = IR::global_var_elem_scales[base];
      }
      return scale;
    }
    if (node.op == "index") {
      if (node.name == "byte_offset")
        return node.value > 0 ? node.value : target_scale_;
      std::string base;
      const Node *cur = &node;
      int index_count = 0;
      while (cur && cur->op == "index") {
        index_count++;
        cur = cur->lhs.get();
      }
      if (cur && cur->op == "var") base = cur->name;
      std::string local_key = current_func_name_ + "$" + base;
      std::vector<long> dims;
      int base_size = target_scale_;
      if (IR::local_array_dims.count(local_key)) {
        dims = IR::local_array_dims[local_key];
        base_size = IR::local_array_base_sizes[local_key];
      } else if (IR::global_array_dims.count(base)) {
        dims = IR::global_array_dims[base];
        base_size = IR::global_array_base_sizes[base];
      }
      if (index_count < (int)dims.size()) {
        long total = base_size;
        for (size_t i = index_count; i < dims.size(); ++i)
          total *= dims[i];
        return total;
      }
      return base_size;
    }
    return target_scale_;
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

  void Parser::apply_integer_conversion(AST::Node &node) {
    if (!node.lhs || !node.rhs) return;
    auto promoted_size = [](const AST::Node &n) {
      return n.type_size < 4 ? 4 : n.type_size;
    };
    auto promoted_unsigned = [](const AST::Node &n) {
      return n.type_size >= 4 && n.is_unsigned;
    };
    int lhs_size = promoted_size(*node.lhs);
    int rhs_size = promoted_size(*node.rhs);
    bool lhs_unsigned = promoted_unsigned(*node.lhs);
    bool rhs_unsigned = promoted_unsigned(*node.rhs);

    int result_size = lhs_size > rhs_size ? lhs_size : rhs_size;
    bool result_unsigned = false;
    if (lhs_size == rhs_size) {
      result_unsigned = lhs_unsigned || rhs_unsigned;
    } else if (lhs_size > rhs_size) {
      result_unsigned = lhs_unsigned;
    } else {
      result_unsigned = rhs_unsigned;
    }
    node.type_size = result_size;
    node.is_unsigned = result_unsigned;
  }

  std::unique_ptr<AST::Node> Parser::bool_normalize(std::unique_ptr<AST::Node> node) {
    auto out = create_node("bool_cast", node->line, node->col);
    out->lhs = std::move(node);
    out->type_size = 1;
    out->is_bool = true;
    return out;
  }

  std::unique_ptr<Node> Parser::function(bool is_static) {
    int line = tokens_[pos_].line;
    int col = tokens_[pos_].col;
    bool ret_is_struct = (peek() == "struct" || peek() == "union");
    int ret_base = parse_base_type();
    int ret_stars = 0;
    while (peek() == "*") {
      take("*");
      ret_stars++;
    }
    skip_attribute();
    int ret_size = (ret_stars > 0) ? target_scale_ : ret_base;
    int ret_aggregate_size = (ret_stars == 0 && ret_is_struct) ? ret_base : 0;

    auto node = create_node("func", line, col);
    node->name = take();
    node->value = ret_size;
    node->aggregate_size = ret_aggregate_size;
    node->is_static = is_static;
    
    current_func_name_ = node->name;
    current_static_locals_.clear();
    scopes_.push_back({});

    take("(");
    bool is_vararg = false;
    if (peek() == "void" && tokens_[pos_ + 1].text == ")") {
      take("void");
    } else if (peek() != ")") {
      while (true) {
        if (peek() == "...") {
          take("...");
          is_vararg = true;
          break;
        }
        bool param_is_struct = (peek() == "struct" || peek() == "union");
        std::string param_struct_tag = "";
        if (param_is_struct && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text != "{") {
          param_struct_tag = tokens_[pos_ + 1].text;
        }
        int base_size = parse_base_type();
        bool param_unsigned = last_type_unsigned_;
        bool param_bool = last_type_bool_;
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
        unsigned_vars_[unique_name] = param_unsigned && stars == 0 && !array_param;
        bool_vars_[unique_name] = param_bool && stars == 0 && !array_param;
        value_sizes_[unique_name] = (stars > 0 || array_param) ? target_scale_ : base_size;
        if (!param_struct_tag.empty()) {
          var_struct_tags_[unique_name] = param_struct_tag;
        }
        node->params.push_back(unique_name);
        
        bool is_pointer = (stars > 0 || array_param);
        node->param_aggregate_sizes.push_back((!is_pointer && param_is_struct) ? base_size : 0);
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
      IR::function_return_aggregate_sizes[node->name] = node->aggregate_size;
      IR::function_param_aggregate_sizes[node->name] = node->param_aggregate_sizes;
      if (is_vararg)
        IR::function_vararg_fixed_counts[node->name] = (int)node->params.size();
      current_func_name_ = "";
      current_static_locals_.clear();
      scopes_.pop_back();
      return nullptr;
    }
    auto block = block_stmt();
    node->body = std::move(block->body);
    IR::function_return_aggregate_sizes[node->name] = node->aggregate_size;
    IR::function_param_aggregate_sizes[node->name] = node->param_aggregate_sizes;
    if (is_vararg)
      IR::function_vararg_fixed_counts[node->name] = (int)node->params.size();
    
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
    if ((peek() == "struct" || peek() == "union") && tokens_[pos_ + 2].text == "{") {
      bool is_union_local = (peek() == "union");
      take();
      std::string tag = take();
      take("{");
      int offset = 0;
      int union_max_sz = 0;
      std::map<std::string, int> fields;
      while (peek() != "}") {
        std::string f_tag = "";
        if (peek() == "struct" || peek() == "union") {
          if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text != "{") {
            f_tag = tokens_[pos_ + 1].text;
          }
        }
        int fbase = parse_base_type();
        int fstars = 0;
        while (peek() == "*") { take("*"); fstars++; }
        int fsize = (fstars > 0) ? target_scale_ : fbase;
        std::string field_name = take();
        long arr_cnt = 1;
        std::vector<long> field_dims;
        if (peek() == "[") {
          while (peek() == "[") {
            take("[");
            long dim = 0;
            if (peek() != "]") {
              dim = eval_const(*expr());
              arr_cnt *= dim;
            }
            take("]");
            field_dims.push_back(dim);
          }
        }
        int align = fsize; if (align > 8) align = 8;
        if (!is_union_local && align > 1 && (offset % align) != 0)
          offset += align - (offset % align);
        if (!tag.empty() && !f_tag.empty() && fstars == 0) {
          global_struct_field_tags[tag + "." + field_name] = f_tag;
        }
        fields[field_name] = offset;
        global_field_offsets[field_name] = offset;
        global_field_sizes[field_name] = fsize;
        int total = (int)(fsize * arr_cnt);
        if (!tag.empty()) {
          std::string key = tag + "." + field_name;
          global_struct_field_offsets_by_tag[key] = offset;
          global_struct_field_sizes_by_tag[key] = fsize;
          global_struct_field_total_sizes_by_tag[key] = total;
          global_struct_field_dims_by_tag[key] = field_dims;
        }
        if (is_union_local) { if (total > union_max_sz) union_max_sz = total; }
        else offset += total;
        take(";");
      }
      take("}");
      take(";");
      int struct_byte_size = is_union_local ? union_max_sz : offset;
      if (struct_byte_size > 0 && !is_union_local && (struct_byte_size % target_scale_) != 0)
        struct_byte_size += target_scale_ - (struct_byte_size % target_scale_);
      global_structs[tag] = fields;
      global_struct_sizes[tag] = struct_byte_size;
      return create_node("block", line, col);
    }

    if (peek() == "int" || peek() == "char" || peek() == "short" || peek() == "long" ||
        peek() == "void" || peek() == "unsigned" || peek() == "signed" || peek() == "_Bool" ||
        peek() == "const" || peek() == "volatile" || peek() == "restrict" || peek() == "__restrict" || peek() == "__restrict__" || peek() == "register" ||
        peek() == "float" || peek() == "double" || peek() == "struct" || peek() == "union" ||
        global_typedefs.count(peek())) {
      std::string struct_tag = "";
      if (peek() == "struct" || peek() == "union") {
        if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text != "{") {
          struct_tag = tokens_[pos_ + 1].text;
        }
      }
      int base_size = parse_base_type();
      bool type_unsigned = last_type_unsigned_;
      bool type_bool = last_type_bool_;
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
      std::string name;
      bool is_func_ptr = false;
      if (peek() == "(") {
        take("(");
        while (peek() == "*") { take("*"); }
        name = take();
        take(")");
        take("(");
        int depth = 1;
        while (depth > 0 && pos_ < tokens_.size()) {
          if (peek() == "(") depth++;
          else if (peek() == ")") depth--;
          take();
        }
        is_func_ptr = true;
      } else {
        name = take();
      }
      skip_attribute();
      if (peek() == "__asm__" || peek() == "__asm" || peek() == "asm") {
        take();
        take("(");
        take();
        take(")");
      }
      bool is_pointer = (stars > 0 || is_func_ptr);
      int elem_scale = 1;
      if (stars == 1 && !is_func_ptr) {
        elem_scale = base_size;
      } else if (stars > 1 || is_func_ptr) {
        elem_scale = target_scale_;
      }

      bool is_aggregate = (peek() == "[" || (!struct_tag.empty() && stars == 0));

      if (is_aggregate) {
        std::vector<long> dims;
        if (peek() == "[") {
          while (peek() == "[") {
            take("[");
            long dim_size = 0;
            if (peek() != "]") {
              dim_size = std::stol(take());
            }
            take("]");
            dims.push_back(dim_size);
          }
        }
        long total_size = 1;
        for (long d : dims) total_size *= d;
        
        long orig_base_size = base_size;
        if (!struct_tag.empty()) {
          long struct_slots = (base_size + target_scale_ - 1) / target_scale_;
          total_size = total_size * struct_slots;
          base_size = target_scale_;
          if (dims.empty()) {
            dims = { struct_slots };
          } else {
            dims = { total_size }; 
          }
        }

        if (is_static) {
          std::string unique_name = current_func_name_ + "$" + name;
          std::vector<InitElement> parsed_inits;
          if (peek() == "=") {
            take("=");
            if (peek() == "{") {
              parse_aggregate_init(0, struct_tag, dims, 0, orig_base_size, parsed_inits);
            } else {
              auto init = expr();
              if (type_bool && stars == 0) init = bool_normalize(std::move(init));
              parsed_inits.push_back({0, std::move(init), static_cast<int>(orig_base_size)});
            }
          }
          std::vector<long> inits;
          if (!parsed_inits.empty()) {
            long total_byte_size = total_size * base_size;
            std::vector<long> byte_buf(total_byte_size, 0);
            for (const auto &item : parsed_inits) {
              long val = eval_const(*item.val);
              for (int b = 0; b < item.size; ++b) {
                if (item.offset + b < total_byte_size) {
                  byte_buf[item.offset + b] = (val >> (b * 8)) & 0xff;
                }
              }
            }
            inits = byte_buf;
            base_size = 1; 
          }
          IR::IrGlobalVar g = {unique_name, true, total_size, inits, true, (int)base_size};
          IR::global_decls.push_back(g);
          IR::global_arrays.insert(unique_name);
          IR::global_array_dims[unique_name] = dims;
          IR::global_array_base_sizes[unique_name] = base_size;
          current_static_locals_[name] = unique_name;
          if (!struct_tag.empty()) {
            var_struct_tags_[unique_name] = struct_tag;
          }
          bool_vars_[unique_name] = type_bool && stars == 0;
          take(";");
          return create_node("block", line, col);
        } else {
          std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
          if (scopes_.back().count(name)) {
            error("redefinition of local " + name);
          }
          scopes_.back()[name] = unique_name;
          if (!struct_tag.empty()) {
            var_struct_tags_[unique_name] = struct_tag;
          }
          auto node = create_node("array_decl", line, col);
          node->name = unique_name;
          node->value = total_size;
          IR::local_array_dims[current_func_name_ + "$" + unique_name] = dims;
          if (stars > 0) {
            base_size = target_scale_;
            orig_base_size = target_scale_;
          }
          IR::local_array_base_sizes[current_func_name_ + "$" + unique_name] = base_size;
          IR::local_var_is_pointer[current_func_name_ + "$" + unique_name] = stars > 0;
          IR::local_var_elem_scales[current_func_name_ + "$" + unique_name] = elem_scale;
          value_sizes_[unique_name] = total_size * base_size;
          bool_vars_[unique_name] = type_bool && stars == 0 && dims.empty();

          if (peek() == "=") {
            take("=");
            if (peek() == "{") {
              std::vector<InitElement> local_inits;
              parse_aggregate_init(0, struct_tag, dims, 0, orig_base_size, local_inits);
              for (auto &item : local_inits) {
                auto item_node = create_node("init_item", line, col);
                item_node->value = item.offset;
                item_node->name = std::to_string(item.size);
                item_node->lhs = std::move(item.val);
                node->body.push_back(std::move(item_node));
              }
            } else {
              auto item_node = create_node("init_item", line, col);
              item_node->value = 0;
              item_node->name = std::to_string(orig_base_size);
              auto init = expr();
              if (type_bool && stars == 0) init = bool_normalize(std::move(init));
              item_node->lhs = std::move(init);
              node->body.push_back(std::move(item_node));
            }
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
          auto init = expr();
          if (type_bool && stars == 0) init = bool_normalize(std::move(init));
          inits.push_back(eval_const(*init));
        }
        IR::IrGlobalVar g = {unique_name, false, 1, inits, true};
        IR::global_decls.push_back(g);
        IR::global_vars.insert(unique_name);
        IR::global_var_is_pointer[unique_name] = is_pointer;
        IR::global_var_elem_scales[unique_name] = elem_scale;
        current_static_locals_[name] = unique_name;
        if (!struct_tag.empty()) {
          var_struct_tags_[unique_name] = struct_tag;
        }
        bool_vars_[unique_name] = type_bool && !is_pointer;
        take(";");
        return create_node("block", line, col);
      } else {
        std::string unique_name = name + "$" + std::to_string(local_var_counter_++);
        if (scopes_.back().count(name)) {
          error("redefinition of local " + name);
        }
        scopes_.back()[name] = unique_name;
        if (!struct_tag.empty()) {
          var_struct_tags_[unique_name] = struct_tag;
        }
        auto node = create_node("decl", line, col);
        node->name = unique_name;
        std::string local_key = current_func_name_ + "$" + unique_name;
        IR::local_var_is_pointer[local_key] = is_pointer;
        IR::local_var_elem_scales[local_key] = elem_scale;
        unsigned_vars_[unique_name] = type_unsigned && !is_pointer;
        bool_vars_[unique_name] = type_bool && !is_pointer;
        value_sizes_[unique_name] = is_pointer ? target_scale_ : base_size;
        if (peek() == "=") {
          take("=");
          node->lhs = expr();
          if (type_bool && !is_pointer) node->lhs = bool_normalize(std::move(node->lhs));
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
            std::string next_name;
            bool next_is_func_ptr = false;
            if (peek() == "(") {
              take("(");
              while (peek() == "*") { take("*"); }
              next_name = take();
              take(")");
              take("(");
              int depth = 1;
              while (depth > 0 && pos_ < tokens_.size()) {
                if (peek() == "(") depth++;
                else if (peek() == ")") depth--;
                take();
              }
              next_is_func_ptr = true;
            } else {
              next_name = take();
            }
            skip_attribute();
            std::string next_unique_name = next_name + "$" + std::to_string(local_var_counter_++);
            if (scopes_.back().count(next_name)) {
              error("redefinition of local " + next_name);
            }
            scopes_.back()[next_name] = next_unique_name;
            auto next = create_node("decl", line, col);
            next->name = next_unique_name;
            bool next_is_pointer = (next_stars > 0 || next_is_func_ptr);
            int next_elem_scale = next_is_func_ptr ? target_scale_ : (next_stars == 1 ? base_size : (next_stars > 1 ? target_scale_ : 1));
            std::string next_local_key = current_func_name_ + "$" + next_unique_name;
            IR::local_var_is_pointer[next_local_key] = next_is_pointer;
            IR::local_var_elem_scales[next_local_key] = next_elem_scale;
            unsigned_vars_[next_unique_name] = type_unsigned && !next_is_pointer;
            bool_vars_[next_unique_name] = type_bool && !next_is_pointer;
            value_sizes_[next_unique_name] = next_is_pointer ? target_scale_ : base_size;
            if (peek() == "=") {
              take("=");
              next->lhs = expr();
              if (type_bool && !next_is_pointer) next->lhs = bool_normalize(std::move(next->lhs));
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
      
      if (peek() == "int" || peek() == "char" || peek() == "short" || peek() == "long" ||
          peek() == "void" || peek() == "unsigned" || peek() == "signed" || peek() == "_Bool" ||
          peek() == "const" || peek() == "volatile" || peek() == "restrict" || peek() == "__restrict" || peek() == "__restrict__" || peek() == "register" ||
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
      if (bool_vars_.count(name) && bool_vars_[name]) {
        node->lhs = bool_normalize(std::move(node->lhs));
      }
    } else {
      std::string bin_op = op.substr(0, 1);
      auto bin_node = create_node(bin_op, line, col);
      auto var_node = create_node("var", line, col);
      var_node->name = name;
      var_node->is_unsigned = unsigned_vars_.count(name) && unsigned_vars_[name];
      var_node->type_size = value_sizes_.count(name) ? (int)value_sizes_[name] : target_scale_;
      var_node->is_bool = bool_vars_.count(name) && bool_vars_[name];
      bin_node->lhs = std::move(var_node);
      bin_node->rhs = expr();
      apply_integer_conversion(*bin_node);
      node->lhs = std::move(bin_node);
      if (bool_vars_.count(name) && bool_vars_[name]) {
        node->lhs = bool_normalize(std::move(node->lhs));
      }
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
      int lhs_size = node->body[0]->type_size < 4 ? 4 : node->body[0]->type_size;
      int rhs_size = node->body[1]->type_size < 4 ? 4 : node->body[1]->type_size;
      bool lhs_unsigned = node->body[0]->type_size >= 4 && node->body[0]->is_unsigned;
      bool rhs_unsigned = node->body[1]->type_size >= 4 && node->body[1]->is_unsigned;
      node->type_size = lhs_size > rhs_size ? lhs_size : rhs_size;
      node->is_unsigned = (lhs_size == rhs_size) ? (lhs_unsigned || rhs_unsigned) :
                          (lhs_size > rhs_size ? lhs_unsigned : rhs_unsigned);
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
      parent->type_size = 4;
      parent->is_unsigned = false;
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
      parent->type_size = 4;
      parent->is_unsigned = false;
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
      apply_integer_conversion(*parent);
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
      apply_integer_conversion(*parent);
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
      apply_integer_conversion(*parent);
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
      parent->type_size = 4;
      parent->is_unsigned = false;
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
      apply_integer_conversion(*parent);
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
      parent->type_size = parent->lhs->type_size < 4 ? 4 : parent->lhs->type_size;
      parent->is_unsigned = parent->lhs->type_size >= 4 && parent->lhs->is_unsigned;
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
      apply_integer_conversion(*parent);
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
    while (peek() == "[" || peek() == "." || peek() == "->" || peek() == "++" || peek() == "--" || peek() == "(") {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      std::string op = take();
      if (op == "++" || op == "--") {
        if (node->op != "var")
          Diagnostics::error(line, col, "lvalue required as increment operand");
        auto parent = create_node("postfix_" + op, line, col);
        parent->name = node->name;
        node = std::move(parent);
      } else if (op == "(") {
        auto call_node = create_node("call", line, col);
        // Strip leading unary_* from the callee (in C, dereferencing function pointers is a no-op)
        while (node->op == "unary_*") {
          node = std::move(node->lhs);
        }
        if (node->op == "var") {
          call_node->name = node->name;
        } else {
          call_node->lhs = std::move(node);
        }
        if (peek() != ")") {
          while (true) {
            call_node->body.push_back(expr());
            if (peek() != ",")
              break;
            take(",");
          }
        }
        take(")");
        node = std::move(call_node);
      } else if (op == "[") {
        auto parent = create_node("index", line, col);
        parent->lhs = std::move(node);
        parent->rhs = expr();
        take("]");
        node = std::move(parent);
      } else {
        bool is_arrow = (op == "->");
        std::string field_name = take();
        auto parent = create_node("index", line, col);
        std::string lhs_tag = infer_struct_tag(*node);
        if (is_arrow) {
          auto deref = create_node("unary_*", line, col);
          deref->lhs = std::move(node);
          deref->type_tag = lhs_tag;
          parent->lhs = std::move(deref);
        } else {
          parent->lhs = std::move(node);
        }
        int byte_offset = 0;
        int field_sz = target_scale_;
        int field_total_sz = field_sz;
        std::vector<long> field_dims;
        std::string field_tag = "";
        std::string field_key = lhs_tag.empty() ? "" : lhs_tag + "." + field_name;
        if (!field_key.empty() && global_struct_field_offsets_by_tag.count(field_key)) {
          byte_offset = global_struct_field_offsets_by_tag[field_key];
          field_sz = global_struct_field_sizes_by_tag[field_key];
          field_total_sz = global_struct_field_total_sizes_by_tag[field_key];
          field_dims = global_struct_field_dims_by_tag[field_key];
          if (global_struct_field_tags.count(field_key)) {
            field_tag = global_struct_field_tags[field_key];
          }
        } else if (global_field_offsets.count(field_name)) {
          byte_offset = global_field_offsets[field_name];
          if (global_field_sizes.count(field_name)) {
            field_sz = global_field_sizes[field_name];
            field_total_sz = field_sz;
          }
        }
        // Encode byte offset as index with scale=1 (raw byte addressing)
        // We use a special marker: store the byte_offset directly and set scale=1
        parent->name = "byte_offset";
        auto index_val = create_node("num", line, col);
        index_val->value = byte_offset;
        parent->rhs = std::move(index_val);
        // Store field size in the parent node so IR can emit correct load
        parent->value = field_sz;
        parent->elem_size = field_sz;
        parent->aggregate_size = field_total_sz;
        parent->array_dims = field_dims;
        parent->type_tag = field_tag;
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
      if (node->op == "unary_!" ) {
        node->type_size = 4;
        node->is_unsigned = false;
      } else {
        node->type_size = node->lhs->type_size < 4 ? 4 : node->lhs->type_size;
        node->is_unsigned = node->lhs->type_size >= 4 && node->lhs->is_unsigned;
      }
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
      if (next_tok == "int" || next_tok == "char" || next_tok == "short" || next_tok == "long" || next_tok == "void" || next_tok == "_Bool" ||
          next_tok == "unsigned" || next_tok == "signed" ||
          next_tok == "const" || next_tok == "volatile" || next_tok == "restrict" || next_tok == "__restrict" || next_tok == "__restrict__" ||
          next_tok == "float" || next_tok == "double" ||
          global_typedefs.count(next_tok)) {
        is_cast = true;
      } else if (next_tok == "struct" || next_tok == "enum") {
        is_cast = true;
      }
      
      if (is_cast) {
        take("(");
        int cast_size = parse_base_type();
        bool cast_unsigned = last_type_unsigned_;
        bool cast_bool = last_type_bool_;
        int cast_stars = 0;
        while (peek() == "*") { take("*"); cast_stars++; }
        if (cast_stars > 0) cast_size = target_scale_; // pointer cast
        take(")");
        auto node = create_node("cast", line, col);
        node->value = cast_size;  // store target byte size for IR truncation
        node->is_unsigned = cast_unsigned && cast_stars == 0;
        node->type_size = cast_size;
        node->is_bool = cast_bool && cast_stars == 0;
        node->lhs = primary();
        if (node->is_bool) {
          return bool_normalize(std::move(node));
        }
        return node;
      }
      
      take("(");
      auto node = expr();
      take(")");
      return node;
    }
    if (peek() == "sizeof") {
      take("sizeof");
      take("(");
      // Peek: if next token is a type keyword or typedef, parse as type
      std::string nt = peek();
      bool is_type = (nt == "int" || nt == "char" || nt == "short" || nt == "long" ||
                      nt == "void" || nt == "unsigned" || nt == "signed" || nt == "_Bool" ||
                      nt == "float" || nt == "double" || nt == "struct" || nt == "union" ||
                      nt == "enum" || nt == "const" || nt == "volatile" || nt == "restrict" || nt == "__restrict" || nt == "__restrict__" ||
                      global_typedefs.count(nt));
      long sz = target_scale_;
      if (is_type) {
        sz = parse_base_type();
        while (peek() == "*") {
          take("*");
          sz = target_scale_;
        }
      } else {
        auto dummy = expr();
        sz = sizeof_expr(*dummy);
      }
      take(")");
      auto node = create_node("num", line, col);
      node->value = sz;
      node->type_size = target_scale_;
      return node;
    }
    if (peek() == "__builtin_constant_p") {
      take("__builtin_constant_p");
      take("(");
      expr();
      take(")");
      auto node = create_node("num", line, col);
      node->value = 0;
      node->type_size = 4;
      return node;
    }
    if (std::isdigit(static_cast<unsigned char>(peek()[0]))) {
      auto node = create_node("num", line, col);
      node->value = std::strtoul(take().c_str(), nullptr, 0);
      node->type_size = (node->value >= -2147483648LL && node->value <= 2147483647LL) ? 4 : 8;
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
      node->type_size = 4;
      return node;
    }
    if (peek()[0] == '"') {
      auto node = create_node("str", line, col);
      node->name = take();
      return node;
    }
    if (std::isalpha(static_cast<unsigned char>(peek()[0])) || peek()[0] == '_') {
      std::string ident = take();
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
      node->is_unsigned = unsigned_vars_.count(ident) && unsigned_vars_[ident];
      node->type_size = value_sizes_.count(ident) ? (int)value_sizes_[ident] : target_scale_;
      node->is_bool = bool_vars_.count(ident) && bool_vars_[ident];
      return node;
    }
    error("expected expression, got '" + peek() + "'");
  }
}

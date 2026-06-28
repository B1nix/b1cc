#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include <vector>
#include <string>
#include <memory>
#include <set>
#include <map>

namespace Parser {
  class Parser {
  public:
    explicit Parser(std::vector<Lexer::Token> tokens, int target_scale);
    std::vector<std::unique_ptr<AST::Node>> parse();

    std::set<std::string> global_typedefs;
    std::map<std::string, int> global_enums;
    std::map<std::string, std::map<std::string, int>> global_structs;
    std::map<std::string, int> global_field_offsets;
    std::map<std::string, int> global_field_sizes;
    std::map<std::string, int> global_struct_sizes;
    std::map<std::string, std::string> global_struct_field_tags;
    std::map<std::string, int> global_struct_field_offsets_by_tag;
    std::map<std::string, int> global_struct_field_sizes_by_tag;
    std::map<std::string, int> global_struct_field_total_sizes_by_tag;
    std::map<std::string, std::vector<long>> global_struct_field_dims_by_tag;

  private:
    std::vector<Lexer::Token> tokens_;
    size_t pos_ = 0;
    int target_scale_;
    std::string current_func_name_;
    std::map<std::string, std::string> current_static_locals_;
    std::vector<std::map<std::string, std::string>> scopes_;
    std::map<std::string, bool> unsigned_vars_;
    std::map<std::string, bool> bool_vars_;
    std::map<std::string, long> value_sizes_;
    std::map<std::string, std::string> var_struct_tags_;
    int local_var_counter_ = 0;
    bool last_type_unsigned_ = false;
    bool last_type_bool_ = false;

    std::string resolve_name(const std::string &name) const;

    [[noreturn]] void error(const std::string &msg);
    const std::string &peek() const;
    std::string take(const std::string &want = "");
    void type();
    std::unique_ptr<AST::Node> create_node(const std::string &op, int line, int col);
    void apply_integer_conversion(AST::Node &node);
    std::unique_ptr<AST::Node> bool_normalize(std::unique_ptr<AST::Node> node);
    std::unique_ptr<AST::Node> function(bool is_static = false);
    void global_decl(bool is_static = false, bool is_extern = false);
    std::unique_ptr<AST::Node> block_stmt();
    std::unique_ptr<AST::Node> stmt();
    std::unique_ptr<AST::Node> assign_stmt(bool semicolon);
    std::unique_ptr<AST::Node> expr();
    std::unique_ptr<AST::Node> logical_or();
    std::unique_ptr<AST::Node> logical_and();
    std::unique_ptr<AST::Node> bitwise_or();
    std::unique_ptr<AST::Node> bitwise_xor();
    std::unique_ptr<AST::Node> bitwise_and();
    std::unique_ptr<AST::Node> equality();
    std::unique_ptr<AST::Node> relational();
    std::unique_ptr<AST::Node> shift();
    std::unique_ptr<AST::Node> add();
    std::unique_ptr<AST::Node> term();
    std::unique_ptr<AST::Node> factor();
    std::unique_ptr<AST::Node> unary();
    std::unique_ptr<AST::Node> primary();
    struct InitElement {
      long offset;
      std::unique_ptr<AST::Node> val;
      int size;
    };
    void parse_aggregate_init(long base_offset, const std::string &struct_tag, const std::vector<long> &array_dims, size_t dim_idx, int base_type_size, std::vector<InitElement> &inits);
    void parse_aggregate_init_internal(long base_offset, const std::string &struct_tag, const std::vector<long> &array_dims, size_t dim_idx, int base_type_size, std::vector<InitElement> &inits, bool has_braces);
    void skip_attribute();
    int parse_base_type();
    bool is_function_decl();
    long eval_const(const AST::Node &node);
    long sizeof_expr(const AST::Node &node) const;
    std::string infer_struct_tag(const AST::Node &node) const;
  };
}

#endif // PARSER_H

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

  private:
    std::vector<Lexer::Token> tokens_;
    size_t pos_ = 0;
    int target_scale_;
    std::string current_func_name_;
    std::map<std::string, std::string> current_static_locals_;
    std::vector<std::map<std::string, std::string>> scopes_;
    int local_var_counter_ = 0;

    std::string resolve_name(const std::string &name) const;

    [[noreturn]] void error(const std::string &msg);
    const std::string &peek() const;
    std::string take(const std::string &want = "");
    void type();
    std::unique_ptr<AST::Node> create_node(const std::string &op, int line, int col);
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
    void skip_attribute();
    int parse_base_type();
    bool is_function_decl();
    long eval_const(const AST::Node &node);
  };
}

#endif // PARSER_H

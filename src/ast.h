#ifndef AST_H
#define AST_H

#include <string>
#include <memory>
#include <vector>

namespace AST {
  struct Node {
    std::string op;
    std::string name;
    long value = 0;
    bool is_static = false;
    std::unique_ptr<Node> lhs;
    std::unique_ptr<Node> rhs;
    std::vector<std::unique_ptr<Node>> body;
    std::vector<std::string> params;
    std::vector<int> param_aggregate_sizes;
    int aggregate_size = 0;
    std::string type_tag;
    std::vector<long> array_dims;
    int elem_size = 0;
    bool is_unsigned = false;
    int type_size = 8;
    bool is_bool = false;
    int line = 1;
    int col = 1;
  };
}

namespace Lexer {
  struct Token {
    std::string text;
    int line = 1;
    int col = 1;
  };
}

#endif // AST_H

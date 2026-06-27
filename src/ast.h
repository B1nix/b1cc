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

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace Diagnostics {
  inline std::string filepath = "input.c";

  [[noreturn]] inline void error(int line, int col, const std::string &msg) {
    std::cerr << filepath << ":" << line << ":" << col << ": error: " << msg << "\n";
    std::exit(1);
  }

  [[noreturn]] inline void fatal(const std::string &msg) {
    std::cerr << "b1cc: error: " << msg << "\n";
    std::exit(1);
  }
}

namespace AST {
  struct Node {
    std::string op;
    std::string name;
    long value = 0;
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

  static std::vector<Token> lex(const std::string &src) {
    std::vector<Token> out;
    int current_line = 1;
    int current_col = 1;
    size_t i = 0;

    auto consume = [&](size_t count) {
      for (size_t k = 0; k < count && i < src.size(); ++k) {
        if (src[i] == '\n') {
          current_line++;
          current_col = 1;
        } else {
          current_col++;
        }
        i++;
      }
    };

    while (i < src.size()) {
      unsigned char c = static_cast<unsigned char>(src[i]);
      int tok_line = current_line;
      int tok_col = current_col;

      if (std::isspace(c)) {
        consume(1);
      } else if (src[i] == '#') {
        while (i < src.size() && src[i] != '\n') {
          consume(1);
        }
      } else if (i + 1 < src.size() && src.substr(i, 2) == "//") {
        consume(2);
        while (i < src.size() && src[i] != '\n') {
          consume(1);
        }
      } else if (i + 1 < src.size() && src.substr(i, 2) == "/*") {
        consume(2);
        while (i + 1 < src.size() && src.substr(i, 2) != "*/") {
          consume(1);
        }
        if (i + 1 == src.size()) {
          Diagnostics::error(tok_line, tok_col, "unterminated block comment");
        }
        consume(2);
      } else if (std::isdigit(c)) {
        size_t start = i;
        consume(1);
        while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
          consume(1);
        }
        out.push_back({src.substr(start, i - start), tok_line, tok_col});
      } else if (std::isalpha(c) || src[i] == '_') {
        size_t start = i;
        consume(1);
        while (i < src.size()) {
          unsigned char d = static_cast<unsigned char>(src[i]);
          if (!std::isalnum(d) && src[i] != '_')
            break;
          consume(1);
        }
        out.push_back({src.substr(start, i - start), tok_line, tok_col});
      } else if (src[i] == '"') {
        size_t start = i;
        consume(1);
        while (i < src.size() && src[i] != '"') {
          if (src[i] == '\\' && i + 1 < src.size()) {
            consume(2);
          } else {
            consume(1);
          }
        }
        if (i == src.size()) {
          Diagnostics::error(tok_line, tok_col, "unterminated string literal");
        }
        consume(1);
        out.push_back({src.substr(start, i - start), tok_line, tok_col});
      } else if (i + 1 < src.size() &&
                 (src.substr(i, 2) == "==" || src.substr(i, 2) == "!=" ||
                  src.substr(i, 2) == "<=" || src.substr(i, 2) == ">=")) {
        std::string text = src.substr(i, 2);
        consume(2);
        out.push_back({text, tok_line, tok_col});
      } else if (std::string("{}[](),;=+-*/<>").find(src[i]) != std::string::npos) {
        std::string text = src.substr(i, 1);
        consume(1);
        out.push_back({text, tok_line, tok_col});
      } else {
        Diagnostics::error(tok_line, tok_col, std::string("unexpected character '") + src[i] + "'");
      }
    }
    out.push_back({"EOF", current_line, current_col});
    return out;
  }
}

namespace Parser {
  using namespace AST;
  using namespace Lexer;

  class Parser {
  public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<std::unique_ptr<Node>> parse() {
      std::vector<std::unique_ptr<Node>> funcs;
      while (peek() != "EOF")
        funcs.push_back(function());
      take("EOF");
      return funcs;
    }

  private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    [[noreturn]] void error(const std::string &msg) {
      const Token &tok = tokens_[pos_];
      Diagnostics::error(tok.line, tok.col, msg);
    }

    const std::string &peek() const { return tokens_[pos_].text; }

    std::string take(const std::string &want = "") {
      const std::string got = peek();
      if (!want.empty() && got != want)
        error("expected '" + want + "', got '" + got + "'");
      ++pos_;
      return got;
    }

    void type() {
      if (peek() != "int" && peek() != "char" && peek() != "long" &&
          peek() != "void")
        error("expected type, got '" + peek() + "'");
      take();
      while (peek() == "*")
        take("*");
    }

    std::unique_ptr<Node> create_node(const std::string &op, int line, int col) {
      auto node = std::make_unique<Node>();
      node->op = op;
      node->line = line;
      node->col = col;
      return node;
    }

    std::unique_ptr<Node> function() {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      type();
      auto node = create_node("func", line, col);
      node->name = take();
      take("(");
      if (peek() == "void" && tokens_[pos_ + 1].text == ")") {
        take("void");
      } else if (peek() != ")") {
        while (true) {
          type();
          node->params.push_back(take());
          if (peek() != ",")
            break;
          take(",");
        }
      }
      take(")");
      auto block = block_stmt();
      node->body = std::move(block->body);
      return node;
    }

    std::unique_ptr<Node> block_stmt() {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      take("{");
      auto node = create_node("block", line, col);
      while (peek() != "}")
        node->body.push_back(stmt());
      take("}");
      return node;
    }

    std::unique_ptr<Node> stmt() {
      if (peek() == "{") {
        return block_stmt();
      }
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;

      if (peek() == "int" || peek() == "char" || peek() == "long" ||
          peek() == "void") {
        type();
        auto node = create_node("decl", line, col);
        node->name = take();
        if (peek() == "=") {
          take("=");
          node->lhs = expr();
        }
        take(";");
        return node;
      }
      if (peek() == "return") {
        take("return");
        auto node = create_node("return", line, col);
        node->lhs = expr();
        take(";");
        return node;
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
        node->body.push_back(assign_stmt(false));
        take(";");
        node->lhs = expr();
        take(";");
        node->body.push_back(assign_stmt(false));
        take(")");
        node->body.push_back(stmt());
        return node;
      }
      if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text == "=") {
        return assign_stmt(true);
      }
      auto node = create_node("expr", line, col);
      node->lhs = expr();
      take(";");
      return node;
    }

    std::unique_ptr<Node> assign_stmt(bool semicolon) {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      auto node = create_node("assign", line, col);
      node->name = take();
      take("=");
      node->lhs = expr();
      if (semicolon)
        take(";");
      return node;
    }

    std::unique_ptr<Node> expr() {
      return equality();
    }

    std::unique_ptr<Node> equality() {
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

    std::unique_ptr<Node> relational() {
      auto node = add();
      while (peek() == "<" || peek() == ">" || peek() == "<=" || peek() == ">=") {
        int line = tokens_[pos_].line;
        int col = tokens_[pos_].col;
        auto parent = create_node(take(), line, col);
        parent->lhs = std::move(node);
        parent->rhs = add();
        node = std::move(parent);
      }
      return node;
    }

    std::unique_ptr<Node> add() {
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

    std::unique_ptr<Node> term() {
      auto node = factor();
      while (peek() == "*" || peek() == "/") {
        int line = tokens_[pos_].line;
        int col = tokens_[pos_].col;
        auto parent = create_node(take(), line, col);
        parent->lhs = std::move(node);
        parent->rhs = factor();
        node = std::move(parent);
      }
      return node;
    }

    std::unique_ptr<Node> factor() {
      auto node = primary();
      while (peek() == "[") {
        int line = tokens_[pos_].line;
        int col = tokens_[pos_].col;
        take("[");
        auto parent = create_node("index", line, col);
        parent->lhs = std::move(node);
        parent->rhs = expr();
        take("]");
        node = std::move(parent);
      }
      return node;
    }

    std::unique_ptr<Node> primary() {
      int line = tokens_[pos_].line;
      int col = tokens_[pos_].col;
      if (peek() == "(") {
        take("(");
        auto node = expr();
        take(")");
        return node;
      }
      if (std::isdigit(static_cast<unsigned char>(peek()[0]))) {
        auto node = create_node("num", line, col);
        for (char c : take())
          node->value = node->value * 10 + (c - '0');
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
          node->name = ident;
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
        auto node = create_node("var", line, col);
        node->name = ident;
        return node;
      }
      error("expected expression, got '" + peek() + "'");
    }
  };
}

namespace IR {
  using namespace AST;

  struct IrInst {
    std::string op;
    std::string arg;
    long value = 0;
  };

  struct IrFunction {
    std::string name;
    std::vector<std::string> params;
    std::vector<IrInst> code;
    std::map<std::string, int> locals;
    std::vector<std::pair<std::string, std::string>> strings;
    bool has_call = false;
  };

  static void lower_expr(const Node &node, IrFunction &fn) {
    if (node.op == "num") {
      fn.code.push_back({"const", "", node.value});
      return;
    }
    if (node.op == "var") {
      if (!fn.locals.count(node.name))
        Diagnostics::error(node.line, node.col, "unknown local " + node.name);
      fn.code.push_back({"load", "", fn.locals[node.name]});
      return;
    }
    if (node.op == "str") {
      std::string label = ".Lstr" + std::to_string(fn.strings.size());
      fn.strings.push_back({label, node.name});
      fn.code.push_back({"str", label, 0});
      return;
    }
    if (node.op == "call") {
      if (node.body.size() > 8)
        Diagnostics::error(node.line, node.col, "calls with more than 8 arguments are not supported");
      for (const auto &arg : node.body)
        lower_expr(*arg, fn);
      fn.has_call = true;
      fn.code.push_back({"call", node.name, static_cast<long>(node.body.size())});
      return;
    }
    if (node.op == "index") {
      lower_expr(*node.lhs, fn);
      lower_expr(*node.rhs, fn);
      fn.code.push_back({"index", "", 0});
      return;
    }
    lower_expr(*node.lhs, fn);
    lower_expr(*node.rhs, fn);
    fn.code.push_back({node.op, "", 0});
  }

  static void lower_stmt(const Node &stmt, IrFunction &fn, int &label_id);

  static void lower_block(const Node &block, IrFunction &fn, int &label_id) {
    for (const auto &stmt : block.body)
      lower_stmt(*stmt, fn, label_id);
  }

  static void lower_stmt(const Node &stmt, IrFunction &fn, int &label_id) {
    if (stmt.op == "block") {
      lower_block(stmt, fn, label_id);
    } else if (stmt.op == "decl") {
      if (fn.locals.count(stmt.name))
        Diagnostics::error(stmt.line, stmt.col, "duplicate local " + stmt.name);
      fn.locals[stmt.name] = static_cast<int>(fn.locals.size());
      if (stmt.lhs) {
        lower_expr(*stmt.lhs, fn);
        fn.code.push_back({"store", "", fn.locals[stmt.name]});
      }
    } else if (stmt.op == "assign") {
      if (!fn.locals.count(stmt.name))
        Diagnostics::error(stmt.line, stmt.col, "unknown local " + stmt.name);
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"store", "", fn.locals[stmt.name]});
    } else if (stmt.op == "return") {
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"ret", "", 0});
    } else if (stmt.op == "expr") {
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"pop", "", 0});
    } else if (stmt.op == "if") {
      std::string else_label = ".Lelse" + std::to_string(label_id++);
      std::string end_label = ".Lendif" + std::to_string(label_id++);
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", else_label, 0});
      lower_stmt(*stmt.body[0], fn, label_id);
      fn.code.push_back({"jmp", end_label, 0});
      fn.code.push_back({"label", else_label, 0});
      if (stmt.body.size() > 1)
        lower_stmt(*stmt.body[1], fn, label_id);
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "while") {
      std::string start_label = ".Lwhile" + std::to_string(label_id++);
      std::string end_label = ".Lendwhile" + std::to_string(label_id++);
      fn.code.push_back({"label", start_label, 0});
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", end_label, 0});
      lower_stmt(*stmt.body[0], fn, label_id);
      fn.code.push_back({"jmp", start_label, 0});
      fn.code.push_back({"label", end_label, 0});
    } else if (stmt.op == "for") {
      std::string start_label = ".Lfor" + std::to_string(label_id++);
      std::string end_label = ".Lendfor" + std::to_string(label_id++);
      lower_stmt(*stmt.body[0], fn, label_id);
      fn.code.push_back({"label", start_label, 0});
      lower_expr(*stmt.lhs, fn);
      fn.code.push_back({"jz", end_label, 0});
      lower_stmt(*stmt.body[2], fn, label_id);
      lower_stmt(*stmt.body[1], fn, label_id);
      fn.code.push_back({"jmp", start_label, 0});
      fn.code.push_back({"label", end_label, 0});
    } else {
      Diagnostics::error(stmt.line, stmt.col, "unknown AST statement " + stmt.op);
    }
  }

  static IrFunction lower_func(const Node &ast) {
    IrFunction fn;
    fn.name = ast.name;
    fn.params = ast.params;
    for (const std::string &param : fn.params) {
      if (fn.locals.count(param))
        Diagnostics::error(ast.line, ast.col, "duplicate parameter " + param);
      fn.locals[param] = static_cast<int>(fn.locals.size());
    }
    int label_id = 0;
    lower_block(ast, fn, label_id);
    return fn;
  }

  static std::vector<IrFunction> lower_program(std::vector<std::unique_ptr<Node>> ast) {
    std::vector<IrFunction> out;
    for (const auto &func : ast)
      out.push_back(lower_func(*func));
    return out;
  }
}

namespace Backend {
  using namespace IR;

  static std::string emit_arm64_darwin(const IrFunction &fn) {
    std::ostringstream out;
    if (!fn.strings.empty()) {
      out << ".cstring\n";
      for (const auto &s : fn.strings)
        out << s.first << ":\n    .asciz " << s.second << "\n";
      out << ".text\n";
    }
    out << ".globl _" << fn.name << "\n.p2align 2\n_" << fn.name << ":\n";
    const bool frame = fn.has_call || !fn.locals.empty();
    if (frame) {
      out << "    stp x29, x30, [sp, #-16]!\n";
      out << "    mov x29, sp\n";
      if (!fn.locals.empty())
        out << "    sub sp, sp, #" << (fn.locals.size() * 16) << "\n";
      for (size_t i = 0; i < fn.params.size(); ++i)
        out << "    str x" << i << ", [x29, #-" << ((i + 1) * 16) << "]\n";
    }
    for (const IrInst &inst : fn.code) {
      if (inst.op == "const") {
        out << "    mov x0, #" << inst.value << "\n";
        out << "    str x0, [sp, #-16]!\n";
      } else if (inst.op == "load") {
        out << "    ldr x0, [x29, #-" << ((inst.value + 1) * 16) << "]\n";
        out << "    str x0, [sp, #-16]!\n";
      } else if (inst.op == "str") {
        out << "    adrp x0, " << inst.arg << "@PAGE\n";
        out << "    add x0, x0, " << inst.arg << "@PAGEOFF\n";
        out << "    str x0, [sp, #-16]!\n";
      } else if (inst.op == "store") {
        out << "    ldr x0, [sp], #16\n";
        out << "    str x0, [x29, #-" << ((inst.value + 1) * 16) << "]\n";
      } else if (inst.op == "pop") {
        out << "    add sp, sp, #16\n";
      } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                 inst.op == "/" || inst.op == "==" || inst.op == "!=" ||
                 inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                 inst.op == ">=" || inst.op == "index") {
        out << "    ldr x0, [sp], #16\n";
        out << "    ldr x1, [sp], #16\n";
        if (inst.op == "index") {
          out << "    ldr x0, [x1, x0, lsl #3]\n";
        } else if (inst.op == "+")
          out << "    add x0, x1, x0\n";
        else if (inst.op == "-")
          out << "    sub x0, x1, x0\n";
        else if (inst.op == "*")
          out << "    mul x0, x1, x0\n";
        else if (inst.op == "/")
          out << "    sdiv x0, x1, x0\n";
        else {
          out << "    cmp x1, x0\n";
          if (inst.op == "==")
            out << "    cset x0, eq\n";
          else if (inst.op == "!=")
            out << "    cset x0, ne\n";
          else if (inst.op == "<")
            out << "    cset x0, lt\n";
          else if (inst.op == ">")
            out << "    cset x0, gt\n";
          else if (inst.op == "<=")
            out << "    cset x0, le\n";
          else
            out << "    cset x0, ge\n";
        }
        out << "    str x0, [sp, #-16]!\n";
      } else if (inst.op == "jz") {
        out << "    ldr x0, [sp], #16\n";
        out << "    cmp x0, #0\n";
        out << "    b.eq " << inst.arg << "\n";
      } else if (inst.op == "jmp") {
        out << "    b " << inst.arg << "\n";
      } else if (inst.op == "label") {
        out << inst.arg << ":\n";
      } else if (inst.op == "call") {
        for (long i = inst.value - 1; i >= 0; --i)
          out << "    ldr x" << i << ", [sp], #16\n";
        out << "    bl _" << inst.arg << "\n";
        out << "    str x0, [sp, #-16]!\n";
      } else if (inst.op == "ret") {
        out << "    ldr x0, [sp], #16\n";
        if (frame) {
          out << "    mov sp, x29\n";
          out << "    ldp x29, x30, [sp], #16\n";
        }
        out << "    ret\n";
      } else {
        Diagnostics::fatal("unknown IR op " + inst.op);
      }
    }
    return out.str();
  }

  static std::string emit_x86_64_b1nix(const IrFunction &fn) {
    static const char *regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    std::ostringstream out;
    if (!fn.strings.empty()) {
      out << ".section .rodata\n";
      for (const auto &s : fn.strings)
        out << s.first << ":\n    .asciz " << s.second << "\n";
      out << ".text\n";
    }
    out << ".globl " << fn.name << "\n" << fn.name << ":\n";
    const bool frame = fn.has_call || !fn.locals.empty();
    if (frame) {
      out << "    pushq %rbp\n";
      out << "    movq %rsp, %rbp\n";
      if (!fn.locals.empty())
        out << "    subq $" << (fn.locals.size() * 16) << ", %rsp\n";
      static const char *arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
      for (size_t i = 0; i < fn.params.size(); ++i)
        out << "    movq " << arg_regs[i] << ", -" << ((i + 1) * 16)
            << "(%rbp)\n";
    }
    for (const IrInst &inst : fn.code) {
      if (inst.op == "const") {
        out << "    movq $" << inst.value << ", %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "load") {
        out << "    movq -" << ((inst.value + 1) * 16) << "(%rbp), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "str") {
        out << "    leaq " << inst.arg << "(%rip), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "store") {
        out << "    popq %rax\n";
        out << "    movq %rax, -" << ((inst.value + 1) * 16) << "(%rbp)\n";
      } else if (inst.op == "pop") {
        out << "    addq $8, %rsp\n";
      } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                 inst.op == "/" || inst.op == "==" || inst.op == "!=" ||
                 inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                 inst.op == ">=" || inst.op == "index") {
        out << "    popq %rcx\n";
        out << "    popq %rax\n";
        if (inst.op == "index") {
          out << "    movq (%rax,%rcx,8), %rax\n";
        } else if (inst.op == "+")
          out << "    addq %rcx, %rax\n";
        else if (inst.op == "-")
          out << "    subq %rcx, %rax\n";
        else if (inst.op == "*")
          out << "    imulq %rcx, %rax\n";
        else if (inst.op == "/") {
          out << "    cqto\n";
          out << "    idivq %rcx\n";
        } else {
          out << "    cmpq %rcx, %rax\n";
          if (inst.op == "==")
            out << "    sete %al\n";
          else if (inst.op == "!=")
            out << "    setne %al\n";
          else if (inst.op == "<")
            out << "    setl %al\n";
          else if (inst.op == ">")
            out << "    setg %al\n";
          else if (inst.op == "<=")
            out << "    setle %al\n";
          else
            out << "    setge %al\n";
          out << "    movzbq %al, %rax\n";
        }
        out << "    pushq %rax\n";
      } else if (inst.op == "jz") {
        out << "    popq %rax\n";
        out << "    cmpq $0, %rax\n";
        out << "    je " << inst.arg << "\n";
      } else if (inst.op == "jmp") {
        out << "    jmp " << inst.arg << "\n";
      } else if (inst.op == "label") {
        out << inst.arg << ":\n";
      } else if (inst.op == "call") {
        if (inst.value > 6)
          Diagnostics::fatal("x86_64 calls with more than 6 arguments are not supported");
        for (long i = inst.value - 1; i >= 0; --i)
          out << "    popq " << regs[i] << "\n";
        out << "    call " << inst.arg << "\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "ret") {
        out << "    popq %rax\n";
        if (frame)
          out << "    leave\n";
        out << "    ret\n";
      } else {
        Diagnostics::fatal("unknown IR op " + inst.op);
      }
    }
    return out.str();
  }

  static std::string emit_i386_b1nix(const IrFunction &fn) {
    static const char *regs[] = {"%eax", "%ecx", "%edx", "%ebx", "%esi", "%edi"};
    std::ostringstream out;
    if (!fn.strings.empty()) {
      out << ".section .rodata\n";
      for (const auto &s : fn.strings)
        out << s.first << ":\n    .asciz " << s.second << "\n";
      out << ".text\n";
    }
    out << ".globl " << fn.name << "\n" << fn.name << ":\n";
    const bool frame = fn.has_call || !fn.locals.empty();
    if (frame) {
      out << "    pushl %ebp\n";
      out << "    movl %esp, %ebp\n";
      if (!fn.locals.empty())
        out << "    subl $" << (fn.locals.size() * 16) << ", %esp\n";
      for (size_t i = 0; i < fn.params.size(); ++i) {
        out << "    movl " << (8 + i * 4) << "(%ebp), %eax\n";
        out << "    movl %eax, -" << ((i + 1) * 16) << "(%ebp)\n";
      }
    }
    for (const IrInst &inst : fn.code) {
      if (inst.op == "const") {
        out << "    movl $" << inst.value << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "load") {
        out << "    movl -" << ((inst.value + 1) * 16) << "(%ebp), %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "str") {
        out << "    movl $" << inst.arg << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "store") {
        out << "    popl %eax\n";
        out << "    movl %eax, -" << ((inst.value + 1) * 16) << "(%ebp)\n";
      } else if (inst.op == "pop") {
        out << "    addl $4, %esp\n";
      } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                 inst.op == "/" || inst.op == "==" || inst.op == "!=" ||
                 inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                 inst.op == ">=" || inst.op == "index") {
        out << "    popl %ecx\n";
        out << "    popl %eax\n";
        if (inst.op == "index") {
          out << "    movl (%eax,%ecx,4), %eax\n";
        } else if (inst.op == "+")
          out << "    addl %ecx, %eax\n";
        else if (inst.op == "-")
          out << "    subl %ecx, %eax\n";
        else if (inst.op == "*")
          out << "    imull %ecx, %eax\n";
        else if (inst.op == "/") {
          out << "    cltd\n";
          out << "    idivl %ecx\n";
        } else {
          out << "    cmpl %ecx, %eax\n";
          if (inst.op == "==")
            out << "    sete %al\n";
          else if (inst.op == "!=")
            out << "    setne %al\n";
          else if (inst.op == "<")
            out << "    setl %al\n";
          else if (inst.op == ">")
            out << "    setg %al\n";
          else if (inst.op == "<=")
            out << "    setle %al\n";
          else
            out << "    setge %al\n";
          out << "    movzbl %al, %eax\n";
        }
        out << "    pushl %eax\n";
      } else if (inst.op == "jz") {
        out << "    popl %eax\n";
        out << "    cmpl $0, %eax\n";
        out << "    je " << inst.arg << "\n";
      } else if (inst.op == "jmp") {
        out << "    jmp " << inst.arg << "\n";
      } else if (inst.op == "label") {
        out << inst.arg << ":\n";
      } else if (inst.op == "call") {
        if (inst.value > 6)
          Diagnostics::fatal("i386 calls with more than 6 arguments are not supported");
        for (long i = 0; i < inst.value; ++i)
          out << "    popl " << regs[i] << "\n";
        for (long i = 0; i < inst.value; ++i)
          out << "    pushl " << regs[i] << "\n";
        out << "    call " << inst.arg << "\n";
        out << "    addl $" << (inst.value * 4) << ", %esp\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "ret") {
        out << "    popl %eax\n";
        if (frame)
          out << "    leave\n";
        out << "    ret\n";
      } else {
        Diagnostics::fatal("unknown IR op " + inst.op);
      }
    }
    return out.str();
  }

  static std::string compile_asm(const std::string &src, const std::string &target) {
    std::ostringstream out;
    auto tokens = Lexer::lex(src);
    Parser::Parser parser(std::move(tokens));
    auto ast = parser.parse();
    auto ir_functions = lower_program(std::move(ast));

    for (const IrFunction &fn : ir_functions) {
      if (target == "arm64-darwin")
        out << emit_arm64_darwin(fn);
      else if (target == "x86_64-b1nix")
        out << emit_x86_64_b1nix(fn);
      else if (target == "i386-b1nix" || target == "x86-b1nix")
        out << emit_i386_b1nix(fn);
      else
        Diagnostics::fatal("unknown target " + target);
    }
    return out.str();
  }
}

namespace Driver {
  static std::string read_file(const std::string &path) {
    std::ifstream in(path);
    if (!in)
      Diagnostics::fatal("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

  static void write_file(const std::string &path, const std::string &data) {
    std::ofstream out(path);
    if (!out)
      Diagnostics::fatal("cannot write " + path);
    out << data;
  }

  static bool exists(const std::string &path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
  }

  static std::string shell_quote(const std::string &s) {
    std::string out = "'";
    for (char c : s)
      out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
  }

  static int run(int argc, char **argv) {
    bool emit_asm = false;
    std::string input;
    std::string output = "a.out";
    std::string target = "arm64-darwin";

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-S") {
        emit_asm = true;
      } else if (arg.rfind("--target=", 0) == 0) {
        target = arg.substr(9);
      } else if (arg == "-o") {
        if (++i == argc)
          Diagnostics::fatal("-o needs a path");
        output = argv[i];
      } else if (arg[0] == '-') {
        Diagnostics::fatal("unknown option " + arg);
      } else if (input.empty()) {
        input = arg;
      } else {
        Diagnostics::fatal("multiple input files are not supported");
      }
    }

    if (input.empty())
      Diagnostics::fatal("usage: b1cc [-S] input.c [-o output]");

    Diagnostics::filepath = input;

    const std::string asm_text = Backend::compile_asm(read_file(input), target);
    if (emit_asm) {
      write_file(output, asm_text);
      return 0;
    }

    char tmp[] = "/tmp/b1cc-XXXXXX.s";
    int fd = mkstemps(tmp, 2);
    if (fd < 0)
      Diagnostics::fatal("cannot create temporary assembly file");
    close(fd);
    write_file(tmp, asm_text);

    std::string cc = "cc";
    std::string prefix;
    if (target == "x86_64-b1nix" || target == "i386-b1nix" || target == "x86-b1nix") {
      const char *env_cc = std::getenv("B1NIX_CC");
      cc = env_cc ? env_cc : "../b1nix/tools/toolchain/bin/b1nix-cc";
      if (!exists(cc))
        Diagnostics::fatal("set B1NIX_CC or run from next to ../b1nix");
      prefix = "B1NIX_ARCH=" + (target == "x86_64-b1nix" ? std::string("x86_64") : std::string("x86")) + " ";
    } else if (target != "arm64-darwin") {
      Diagnostics::fatal("linking is not supported for target " + target);
    }

    const std::string cmd =
        prefix + shell_quote(cc) + " " + shell_quote(tmp) + " -o " + shell_quote(output);
    int rc = std::system(cmd.c_str());
    unlink(tmp);
    return rc == 0 ? 0 : 1;
  }
}

int main(int argc, char **argv) {
  return Driver::run(argc, argv);
}

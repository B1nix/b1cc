#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

struct Token {
  std::string text;
};

struct Node {
  std::string op;
  std::string name;
  long value = 0;
  std::unique_ptr<Node> lhs;
  std::unique_ptr<Node> rhs;
  std::vector<std::unique_ptr<Node>> body;
};

struct IrInst {
  std::string op;
  long value = 0;
};

struct IrFunction {
  std::string name;
  std::vector<IrInst> code;
};

[[noreturn]] static void die(const std::string &msg) {
  std::cerr << "b1cc: " << msg << "\n";
  std::exit(1);
}

static std::vector<Token> lex(const std::string &src) {
  std::vector<Token> out;
  for (size_t i = 0; i < src.size();) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    if (std::isspace(c)) {
      ++i;
    } else if (std::isdigit(c)) {
      size_t start = i++;
      while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
        ++i;
      }
      out.push_back({src.substr(start, i - start)});
    } else if (std::isalpha(c) || src[i] == '_') {
      size_t start = i++;
      while (i < src.size()) {
        unsigned char d = static_cast<unsigned char>(src[i]);
        if (!std::isalnum(d) && src[i] != '_')
          break;
        ++i;
      }
      out.push_back({src.substr(start, i - start)});
    } else if (std::string("{}();+-*/").find(src[i]) != std::string::npos) {
      out.push_back({src.substr(i++, 1)});
    } else {
      die("unexpected character");
    }
  }
  out.push_back({"EOF"});
  return out;
}

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

  std::unique_ptr<Node> function() {
    take("int");
    auto node = std::make_unique<Node>();
    node->op = "func";
    node->name = take();
    take("(");
    if (peek() == "void")
      take("void");
    take(")");
    take("{");
    take("return");
    auto expr_node = expr();
    take(";");
    take("}");
    node->body.push_back(std::move(expr_node));
    return node;
  }

  const std::string &peek() const { return tokens_[pos_].text; }

  std::string take(const std::string &want = "") {
    const std::string got = peek();
    if (!want.empty() && got != want)
      die("expected '" + want + "', got '" + got + "'");
    ++pos_;
    return got;
  }

  std::unique_ptr<Node> expr() {
    auto node = term();
    while (peek() == "+" || peek() == "-") {
      auto parent = std::make_unique<Node>();
      parent->op = take();
      parent->lhs = std::move(node);
      parent->rhs = term();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> term() {
    auto node = factor();
    while (peek() == "*" || peek() == "/") {
      auto parent = std::make_unique<Node>();
      parent->op = take();
      parent->lhs = std::move(node);
      parent->rhs = factor();
      node = std::move(parent);
    }
    return node;
  }

  std::unique_ptr<Node> factor() {
    if (peek() == "(") {
      take("(");
      auto node = expr();
      take(")");
      return node;
    }
    if (std::isdigit(static_cast<unsigned char>(peek()[0]))) {
      auto node = std::make_unique<Node>();
      node->op = "num";
      for (char c : take())
        node->value = node->value * 10 + (c - '0');
      return node;
    }
    die("expected expression, got '" + peek() + "'");
  }
};

static void lower_expr(const Node &node, IrFunction &fn) {
  if (node.op == "num") {
    fn.code.push_back({"const", node.value});
    return;
  }
  lower_expr(*node.lhs, fn);
  lower_expr(*node.rhs, fn);
  fn.code.push_back({node.op, 0});
}

static IrFunction lower_func(const Node &ast) {
  IrFunction fn;
  fn.name = ast.name;
  lower_expr(*ast.body[0], fn);
  fn.code.push_back({"ret", 0});
  return fn;
}

static std::vector<IrFunction> lower_program(std::vector<std::unique_ptr<Node>> ast) {
  std::vector<IrFunction> out;
  for (const auto &func : ast)
    out.push_back(lower_func(*func));
  return out;
}

static std::string emit_arm64_darwin(const IrFunction &fn) {
  std::ostringstream out;
  out << ".globl _" << fn.name << "\n.p2align 2\n_" << fn.name << ":\n";
  for (const IrInst &inst : fn.code) {
    if (inst.op == "const") {
      out << "    mov x0, #" << inst.value << "\n";
      out << "    str x0, [sp, #-16]!\n";
    } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" || inst.op == "/") {
      out << "    ldr x0, [sp], #16\n";
      out << "    ldr x1, [sp], #16\n";
      if (inst.op == "+")
        out << "    add x0, x1, x0\n";
      else if (inst.op == "-")
        out << "    sub x0, x1, x0\n";
      else if (inst.op == "*")
        out << "    mul x0, x1, x0\n";
      else if (inst.op == "/")
        out << "    sdiv x0, x1, x0\n";
      out << "    str x0, [sp, #-16]!\n";
    } else if (inst.op == "ret") {
      out << "    ldr x0, [sp], #16\n";
      out << "    ret\n";
    } else {
      die("unknown IR op " + inst.op);
    }
  }
  return out.str();
}

static std::string compile_asm(const std::string &src) {
  std::ostringstream out;
  for (const IrFunction &fn : lower_program(Parser(lex(src)).parse())) {
    out << emit_arm64_darwin(fn);
  }
  return out.str();
}

static std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    die("cannot open " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void write_file(const std::string &path, const std::string &data) {
  std::ofstream out(path);
  if (!out)
    die("cannot write " + path);
  out << data;
}

static std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s)
    out += c == '\'' ? "'\\''" : std::string(1, c);
  return out + "'";
}

int main(int argc, char **argv) {
  bool emit_asm = false;
  std::string input;
  std::string output = "a.out";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-S") {
      emit_asm = true;
    } else if (arg == "-o") {
      if (++i == argc)
        die("-o needs a path");
      output = argv[i];
    } else if (arg[0] == '-') {
      die("unknown option " + arg);
    } else if (input.empty()) {
      input = arg;
    } else {
      die("multiple input files are not supported");
    }
  }

  if (input.empty())
    die("usage: b1cc [-S] input.c [-o output]");

  const std::string asm_text = compile_asm(read_file(input));
  if (emit_asm) {
    write_file(output, asm_text);
    return 0;
  }

  char tmp[] = "/tmp/b1cc-XXXXXX.s";
  int fd = mkstemps(tmp, 2);
  if (fd < 0)
    die("cannot create temporary assembly file");
  close(fd);
  write_file(tmp, asm_text);

  std::string cc = "cc";
  const std::string cmd =
      shell_quote(cc) + " " + shell_quote(tmp) + " -o " + shell_quote(output);
  int rc = std::system(cmd.c_str());
  unlink(tmp);
  return rc == 0 ? 0 : 1;
}

#include "lexer.h"
#include "diagnostics.h"
#include <cctype>
#include <algorithm>

namespace Lexer {
  std::vector<Token> lex(const std::string &src, const std::map<std::string, Preprocessor::Macro> &macros, std::set<std::string> active_macros) {
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
        if (c == '0' && i < src.size() && (src[i] == 'x' || src[i] == 'X')) {
          consume(1);
          while (i < src.size() && std::isxdigit(static_cast<unsigned char>(src[i]))) {
            consume(1);
          }
        } else {
          while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
            consume(1);
          }
        }
        std::string num_str = src.substr(start, i - start);
        while (i < src.size() && (src[i] == 'u' || src[i] == 'U' || src[i] == 'l' || src[i] == 'L')) {
          consume(1);
        }
        out.push_back({num_str, tok_line, tok_col});
      } else if (std::isalpha(c) || src[i] == '_') {
        size_t start = i;
        consume(1);
        while (i < src.size()) {
          unsigned char d = static_cast<unsigned char>(src[i]);
          if (!std::isalnum(d) && src[i] != '_')
            break;
          consume(1);
        }
        std::string ident = src.substr(start, i - start);
        if (macros.count(ident) && !active_macros.count(ident)) {
          std::set<std::string> next_active = active_macros;
          next_active.insert(ident);
          const auto &m = macros.at(ident);
          if (!m.is_function_like) {
            auto macro_tokens = lex(m.body, macros, next_active);
            for (auto &tok : macro_tokens) {
              if (tok.text != "EOF") {
                tok.line = tok_line;
                tok.col = tok_col;
                out.push_back(tok);
              }
            }
          } else {
            size_t next_i = i;
            while (next_i < src.size() && std::isspace(static_cast<unsigned char>(src[next_i]))) next_i++;
            if (next_i < src.size() && src[next_i] == '(') {
              consume(next_i - i);
              consume(1);
              
              std::vector<std::string> args;
              std::string current_arg;
              int paren_depth = 0;
              while (i < src.size()) {
                char next_c = src[i];
                if (next_c == '(') {
                  paren_depth++;
                  current_arg += next_c;
                  consume(1);
                } else if (next_c == ')') {
                  if (paren_depth == 0) {
                    args.push_back(current_arg);
                    consume(1);
                    break;
                  }
                  paren_depth--;
                  current_arg += next_c;
                  consume(1);
                } else if (next_c == ',' && paren_depth == 0) {
                  args.push_back(current_arg);
                  current_arg.clear();
                  consume(1);
                } else {
                  current_arg += next_c;
                  consume(1);
                }
              }
              
              for (auto &arg : args) {
                size_t s_idx = 0;
                while (s_idx < arg.size() && std::isspace(static_cast<unsigned char>(arg[s_idx]))) s_idx++;
                size_t e_idx = arg.size();
                while (e_idx > s_idx && std::isspace(static_cast<unsigned char>(arg[e_idx - 1]))) e_idx--;
                arg = arg.substr(s_idx, e_idx - s_idx);
              }
              
              std::string result_body;
              size_t b_idx = 0;
              while (b_idx < m.body.size()) {
                if (std::isalpha(static_cast<unsigned char>(m.body[b_idx])) || m.body[b_idx] == '_') {
                  size_t s_start = b_idx;
                  while (b_idx < m.body.size() && (std::isalnum(static_cast<unsigned char>(m.body[b_idx])) || m.body[b_idx] == '_')) b_idx++;
                  std::string token = m.body.substr(s_start, b_idx - s_start);
                  auto it = std::find(m.params.begin(), m.params.end(), token);
                  if (it != m.params.end()) {
                    size_t p_idx = std::distance(m.params.begin(), it);
                    if (p_idx < args.size()) {
                      result_body += args[p_idx];
                    }
                  } else {
                    result_body += token;
                  }
                } else {
                  result_body += m.body[b_idx];
                  b_idx++;
                }
              }
              
              auto macro_tokens = lex(result_body, macros, next_active);
              for (auto &tok : macro_tokens) {
                if (tok.text != "EOF") {
                  tok.line = tok_line;
                  tok.col = tok_col;
                  out.push_back(tok);
                }
              }
            } else {
              out.push_back({ident, tok_line, tok_col});
            }
          }
        } else {
          out.push_back({ident, tok_line, tok_col});
        }
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
      } else if (src[i] == '\'') {
        size_t start = i;
        consume(1);
        while (i < src.size() && src[i] != '\'') {
          if (src[i] == '\\' && i + 1 < src.size()) {
            consume(2);
          } else {
            consume(1);
          }
        }
        if (i == src.size()) {
          Diagnostics::error(tok_line, tok_col, "unterminated character literal");
        }
        consume(1);
        out.push_back({src.substr(start, i - start), tok_line, tok_col});
      } else if (i + 2 < src.size() && src.substr(i, 3) == "...") {
        consume(3);
        out.push_back({"...", tok_line, tok_col});
      } else if (i + 1 < src.size() &&
                 (src.substr(i, 2) == "==" || src.substr(i, 2) == "!=" ||
                  src.substr(i, 2) == "<=" || src.substr(i, 2) == ">=" ||
                  src.substr(i, 2) == "->" || src.substr(i, 2) == "&&" ||
                  src.substr(i, 2) == "||" || src.substr(i, 2) == "<<" ||
                  src.substr(i, 2) == ">>" || src.substr(i, 2) == "++" ||
                  src.substr(i, 2) == "--" || src.substr(i, 2) == "+=" ||
                  src.substr(i, 2) == "-=" || src.substr(i, 2) == "*=" ||
                  src.substr(i, 2) == "/=" || src.substr(i, 2) == "%=")) {
        std::string text = src.substr(i, 2);
        consume(2);
        out.push_back({text, tok_line, tok_col});
      } else if (std::string("{}[](),;=+-*/%<>.&!|^~:?").find(src[i]) != std::string::npos) {
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

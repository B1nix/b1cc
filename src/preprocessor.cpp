#include "preprocessor.h"
#include "diagnostics.h"
#include <sstream>
#include <fstream>
#include <cctype>
#include <algorithm>

namespace Preprocessor {
  std::vector<std::string> driver_include_dirs;
  std::map<std::string, Macro> driver_macros;

  static bool exists(const std::string &path) {
    std::ifstream in(path);
    return in.good();
  }

  static std::vector<std::string> get_expr_tokens(const std::string &s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
      if (std::isspace(static_cast<unsigned char>(s[i]))) {
        i++;
      } else if (std::isdigit(static_cast<unsigned char>(s[i]))) {
        size_t start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        tokens.push_back(s.substr(start, i - start));
      } else if (i + 1 < s.size() && (s.substr(i, 2) == "==" || s.substr(i, 2) == "!=" ||
                                      s.substr(i, 2) == "<=" || s.substr(i, 2) == ">=" ||
                                      s.substr(i, 2) == "&&" || s.substr(i, 2) == "||")) {
        tokens.push_back(s.substr(i, 2));
        i += 2;
      } else if (std::string("+-*/<>()!~").find(s[i]) != std::string::npos) {
        tokens.push_back(std::string(1, s[i]));
        i++;
      } else {
        tokens.push_back(std::string(1, s[i]));
        i++;
      }
    }
    return tokens;
  }

  struct ExprParser {
    std::vector<std::string> tokens;
    size_t pos = 0;

    std::string peek() {
      if (pos >= tokens.size()) return "";
      return tokens[pos];
    }

    std::string take() {
      std::string t = peek();
      pos++;
      return t;
    }

    long primary() {
      std::string t = peek();
      if (t == "(") {
        take();
        long val = eval_or();
        take(); // ")"
        return val;
      }
      if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) {
        return std::stol(take());
      }
      if (t == "!") {
        take();
        return !primary();
      }
      if (t == "~") {
        take();
        return ~primary();
      }
      if (t == "-") {
        take();
        return -primary();
      }
      return 0;
    }

    long mul() {
      long val = primary();
      while (peek() == "*" || peek() == "/") {
        std::string op = take();
        long rhs = primary();
        if (op == "*") val *= rhs;
        else if (rhs != 0) val /= rhs;
        else val = 0;
      }
      return val;
    }

    long add() {
      long val = mul();
      while (peek() == "+" || peek() == "-") {
        std::string op = take();
        long rhs = mul();
        if (op == "+") val += rhs;
        else val -= rhs;
      }
      return val;
    }

    long relational() {
      long val = add();
      while (peek() == "<" || peek() == ">" || peek() == "<=" || peek() == ">=") {
        std::string op = take();
        long rhs = add();
        if (op == "<") val = val < rhs;
        else if (op == ">") val = val > rhs;
        else if (op == "<=") val = val <= rhs;
        else val = val >= rhs;
      }
      return val;
    }

    long equality() {
      long val = relational();
      while (peek() == "==" || peek() == "!=") {
        std::string op = take();
        long rhs = relational();
        if (op == "==") val = val == rhs;
        else val = val != rhs;
      }
      return val;
    }

    long eval_and() {
      long val = equality();
      while (peek() == "&&") {
        take();
        long rhs = equality();
        val = val && rhs;
      }
      return val;
    }

    long eval_or() {
      long val = eval_and();
      while (peek() == "||") {
        take();
        long rhs = eval_and();
        val = val || rhs;
      }
      return val;
    }
  };

  static long eval_preproc_expr(std::string s, const std::map<std::string, Macro> &macros) {
    size_t pos;
    while ((pos = s.find("defined")) != std::string::npos) {
      size_t i = pos + 7;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
      bool has_paren = false;
      if (i < s.size() && s[i] == '(') {
        has_paren = true;
        i++;
      }
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
      size_t start = i;
      while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) i++;
      std::string name = s.substr(start, i - start);
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
      if (has_paren && i < s.size() && s[i] == ')') {
        i++;
      }
      std::string replacement = macros.count(name) ? "1" : "0";
      s.replace(pos, i - pos, replacement);
    }

    std::string res;
    size_t i = 0;
    while (i < s.size()) {
      if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
        size_t start = i;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) i++;
        std::string ident = s.substr(start, i - start);
        if (macros.count(ident) && !macros.at(ident).is_function_like) {
          res += macros.at(ident).body;
        } else {
          res += "0";
        }
      } else {
        res += s[i];
        i++;
      }
    }

    ExprParser parser{get_expr_tokens(res), 0};
    return parser.eval_or();
  }

  static std::string find_include_file(const std::string &name, bool is_angled, const std::string &current_file_dir, const std::vector<std::string> &include_dirs) {
    if (!is_angled) {
      std::string path = current_file_dir + "/" + name;
      if (exists(path)) return path;
    }
    for (const auto &dir : include_dirs) {
      std::string path = dir + "/" + name;
      if (exists(path)) return path;
    }
    return "";
  }

  static std::string join_continuation_lines(const std::string &src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
      if (src[i] == '\\' && i + 1 < src.size() && src[i + 1] == '\n') {
        i += 2;
      } else if (src[i] == '\\' && i + 2 < src.size() && src[i + 1] == '\r' && src[i + 2] == '\n') {
        i += 3;
      } else {
        out += src[i];
        i++;
      }
    }
    return out;
  }

  std::string preprocess(const std::string &raw_src, const std::string &filepath, const std::vector<std::string> &include_dirs, std::map<std::string, Macro> &macros, std::set<std::string> &included_files) {
    std::string src = join_continuation_lines(raw_src);
    std::string out;
    std::vector<CondState> cond_stack;

    auto is_active = [&]() {
      for (const auto &state : cond_stack) {
        if (!state.active) return false;
      }
      return true;
    };

    std::string current_file_dir = ".";
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != std::string::npos) {
      current_file_dir = filepath.substr(0, last_slash);
    }

    std::istringstream iss(src);
    std::string line;
    while (std::getline(iss, line)) {
      size_t first_non_ws = 0;
      while (first_non_ws < line.size() && (line[first_non_ws] == ' ' || line[first_non_ws] == '\t')) {
        first_non_ws++;
      }

      if (first_non_ws < line.size() && line[first_non_ws] == '#') {
        size_t p = first_non_ws + 1;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
        size_t dir_start = p;
        while (p < line.size() && std::isalpha(static_cast<unsigned char>(line[p]))) p++;
        std::string directive = line.substr(dir_start, p - dir_start);

        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;

        if (directive == "ifdef") {
          size_t name_start = p;
          while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) p++;
          std::string name = line.substr(name_start, p - name_start);
          bool cond = macros.count(name) > 0;
          bool parent_active = is_active();
          cond_stack.push_back({cond, cond && parent_active});
          out += "\n";
        } else if (directive == "ifndef") {
          size_t name_start = p;
          while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) p++;
          std::string name = line.substr(name_start, p - name_start);
          bool cond = macros.count(name) == 0;
          bool parent_active = is_active();
          cond_stack.push_back({cond, cond && parent_active});
          out += "\n";
        } else if (directive == "if") {
          bool cond = eval_preproc_expr(line.substr(p), macros) != 0;
          bool parent_active = is_active();
          cond_stack.push_back({cond, cond && parent_active});
          out += "\n";
        } else if (directive == "else") {
          if (cond_stack.empty()) {
            Diagnostics::fatal("unmatched #else");
          }
          bool parent_active = true;
          for (size_t idx = 0; idx + 1 < cond_stack.size(); ++idx) {
            if (!cond_stack[idx].active) parent_active = false;
          }
          cond_stack.back().active = !cond_stack.back().condition_met && parent_active;
          cond_stack.back().condition_met = true;
          out += "\n";
        } else if (directive == "endif") {
          if (cond_stack.empty()) {
            Diagnostics::fatal("unmatched #endif");
          }
          cond_stack.pop_back();
          out += "\n";
        } else if (directive == "define" && is_active()) {
          size_t name_start = p;
          while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) p++;
          std::string name = line.substr(name_start, p - name_start);
          
          Macro m;
          if (p < line.size() && line[p] == '(') {
            m.is_function_like = true;
            p++;
            while (p < line.size() && line[p] != ')') {
              while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
              if (p + 2 < line.size() && line.substr(p, 3) == "...") {
                m.params.push_back("...");
                p += 3;
              } else {
                size_t param_start = p;
                while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) p++;
                if (p == param_start) {
                  p++;
                } else {
                  m.params.push_back(line.substr(param_start, p - param_start));
                }
              }
              while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
              if (p < line.size() && line[p] == ',') p++;
            }
            if (p < line.size() && line[p] == ')') p++;
          }
          
          while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
          std::string body = line.substr(p);
          size_t comment_pos = body.find("//");
          if (comment_pos != std::string::npos) {
            body = body.substr(0, comment_pos);
          }
          while (!body.empty() && (body.back() == ' ' || body.back() == '\t' || body.back() == '\r')) {
            body.pop_back();
          }
          m.body = body;
          macros[name] = m;
          out += "\n";
        } else if (directive == "undef" && is_active()) {
          size_t name_start = p;
          while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) p++;
          std::string name = line.substr(name_start, p - name_start);
          macros.erase(name);
          out += "\n";
        } else if (directive == "include" && is_active()) {
          bool is_angled = false;
          std::string filename;
          if (p < line.size() && line[p] == '<') {
            is_angled = true;
            p++;
            size_t fn_start = p;
            while (p < line.size() && line[p] != '>') p++;
            filename = line.substr(fn_start, p - fn_start);
          } else if (p < line.size() && line[p] == '"') {
            p++;
            size_t fn_start = p;
            while (p < line.size() && line[p] != '"') p++;
            filename = line.substr(fn_start, p - fn_start);
          }
          
          std::string inc_path = find_include_file(filename, is_angled, current_file_dir, include_dirs);
          if (inc_path.empty()) {
            Diagnostics::fatal("cannot find include file " + filename);
          }
          

          bool is_host_system = (inc_path.rfind("/usr/include", 0) == 0 ||
                                 inc_path.rfind("/Library", 0) == 0 ||
                                 inc_path.rfind("/System", 0) == 0 ||
                                 inc_path.rfind("/Applications", 0) == 0);

          if (!is_host_system && !included_files.count(inc_path)) {
            included_files.insert(inc_path);
            std::ifstream in(inc_path);
            if (!in) {
              Diagnostics::fatal("cannot open include file " + inc_path);
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string inc_content = ss.str();
            out += preprocess(inc_content, inc_path, include_dirs, macros, included_files);
          }
          out += "\n";
        } else {
          out += "\n";
        }
      } else {
        if (is_active()) {
          out += line + "\n";
        } else {
          out += "\n";
        }
      }
    }
    return out;
  }
}

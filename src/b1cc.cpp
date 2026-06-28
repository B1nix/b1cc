#include "diagnostics.h"
#include "preprocessor.h"
#include "lexer.h"
#include "parser.h"
#include "ir.h"
#include "backend.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

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

  static void dump_command(const std::string &title, const std::string &cmd) {
    std::cout << "=== " << title << " ===\n";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
      Diagnostics::fatal("cannot run dump command");
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
      std::cout << buf;
    pclose(pipe);
  }

  static void dump_object(const std::string &path, bool dump_symbols, bool dump_sections, bool dump_relocs) {
    std::string q = shell_quote(path);
    if (dump_symbols)
      dump_command("symbols " + path, "nm " + q + " 2>&1");
    if (dump_sections)
      dump_command("sections " + path, "(objdump -h " + q + " 2>/dev/null || otool -l " + q + " 2>&1)");
    if (dump_relocs)
      dump_command("relocations " + path, "(objdump -r " + q + " 2>/dev/null || otool -r " + q + " 2>&1)");
  }

  static int run(int argc, char **argv) {
    bool emit_asm = false;
    bool compile_only = false;
    bool preprocess_only = false;
    bool dump_ast = false;
    bool dump_ir = false;
    bool dump_symbols = false;
    bool dump_sections = false;
    bool dump_relocs = false;
    std::vector<std::string> inputs;
    std::vector<std::string> link_flags;
    std::string output = "";
    std::string target = "arm64-darwin";

    Preprocessor::driver_include_dirs.clear();
    Preprocessor::driver_macros.clear();
    Preprocessor::driver_include_dirs.push_back(".");
    Preprocessor::driver_include_dirs.push_back("../b1nix/userspace/include");
    Preprocessor::driver_include_dirs.push_back("/usr/include");
    Preprocessor::driver_include_dirs.push_back("/usr/local/include");

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-S") {
        emit_asm = true;
      } else if (arg == "-c") {
        compile_only = true;
      } else if (arg == "-E") {
        preprocess_only = true;
      } else if (arg == "-fdump-ast") {
        dump_ast = true;
      } else if (arg == "-fdump-ir") {
        dump_ir = true;
      } else if (arg == "-fdump-symbols") {
        dump_symbols = true;
      } else if (arg == "-fdump-sections") {
        dump_sections = true;
      } else if (arg == "-fdump-relocs") {
        dump_relocs = true;
      } else if (arg.rfind("--target=", 0) == 0) {
        target = arg.substr(9);
      } else if (arg == "-o") {
        if (++i == argc)
          Diagnostics::fatal("-o needs a path");
        output = argv[i];
      } else if (arg.rfind("-I", 0) == 0) {
        std::string dir;
        if (arg == "-I") {
          if (++i == argc)
            Diagnostics::fatal("-I needs a directory");
          dir = argv[i];
        } else {
          dir = arg.substr(2);
        }
        Preprocessor::driver_include_dirs.push_back(dir);
      } else if (arg.rfind("-D", 0) == 0) {
        std::string def;
        if (arg == "-D") {
          if (++i == argc)
            Diagnostics::fatal("-D needs a definition");
          def = argv[i];
        } else {
          def = arg.substr(2);
        }
        size_t eq = def.find('=');
        std::string name = def;
        std::string val = "1";
        if (eq != std::string::npos) {
          name = def.substr(0, eq);
          val = def.substr(eq + 1);
        }
        Preprocessor::Macro m;
        m.is_function_like = false;
        m.body = val;
        Preprocessor::driver_macros[name] = m;
      } else if (arg[0] == '-' && arg != "-") {
        link_flags.push_back(arg);
      } else {
        inputs.push_back(arg);
      }
    }

    if (inputs.empty())
      Diagnostics::fatal("usage: b1cc [-S] [-c] [-E] [-fdump-ast] [-fdump-ir] input.c ... [-o output]");

    if (target == "arm64-darwin") {
      Preprocessor::driver_macros["stdin"] = {false, {}, "__stdinp"};
      Preprocessor::driver_macros["stdout"] = {false, {}, "__stdoutp"};
      Preprocessor::driver_macros["stderr"] = {false, {}, "__stderrp"};
    }

    std::string cc = "cc";
    std::string prefix;
    if (target == "x86_64-b1nix" || target == "i386-b1nix" || target == "x86-b1nix") {
      const char *env_cc = std::getenv("B1NIX_CC");
      cc = env_cc ? env_cc : "../b1nix/tools/toolchain/bin/b1nix-cc";
      if (!preprocess_only && !emit_asm && !exists(cc))
        Diagnostics::fatal("set B1NIX_CC or run from next to ../b1nix");
      prefix = "B1NIX_ARCH=" + (target == "x86_64-b1nix" ? std::string("x86_64") : std::string("x86")) + " ";
    } else if (target != "arm64-darwin") {
      Diagnostics::fatal("linking/assembling is not supported for target " + target);
    }

    if (preprocess_only) {
      std::ostringstream prep_out;
      for (const auto &inp : inputs) {
        std::map<std::string, Preprocessor::Macro> macros = Preprocessor::driver_macros;
        std::set<std::string> included_files;
        included_files.insert(inp);
        std::string prep = Preprocessor::preprocess(read_file(inp), inp, Preprocessor::driver_include_dirs, macros, included_files);
        auto toks = Lexer::lex(prep, macros);
        for (const auto &tok : toks) {
          if (tok.text == "EOF") continue;
          prep_out << tok.text;
          if (tok.text == ";" || tok.text == "}" || tok.text == "{")
            prep_out << "\n";
          else
            prep_out << " ";
        }
        prep_out << "\n";
      }
      if (output.empty()) {
        std::cout << prep_out.str();
      } else {
        write_file(output, prep_out.str());
      }
      return 0;
    }

    if (emit_asm) {
      for (const auto &inp : inputs) {
        Diagnostics::filepath = inp;
        std::string asm_text = Backend::compile_asm(read_file(inp), target, dump_ast, dump_ir);
        std::string dest = output;
        if (dest.empty() || inputs.size() > 1) {
          size_t dot = inp.find_last_of('.');
          dest = (dot != std::string::npos ? inp.substr(0, dot) : inp) + ".s";
        }
        write_file(dest, asm_text);
      }
      return 0;
    }

    if (compile_only) {
      for (const auto &inp : inputs) {
        if (inp.rfind(".o") == inp.size() - 2 || inp.rfind(".a") == inp.size() - 2) {
          continue;
        }
        Diagnostics::filepath = inp;
        std::string asm_text = Backend::compile_asm(read_file(inp), target, dump_ast, dump_ir);
        
        char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
        int fd = mkstemps(tmp_asm, 2);
        if (fd < 0) Diagnostics::fatal("cannot create temporary file");
        close(fd);
        write_file(tmp_asm, asm_text);

        std::string dest_obj = output;
        if (dest_obj.empty() || inputs.size() > 1) {
          size_t dot = inp.find_last_of('.');
          dest_obj = (dot != std::string::npos ? inp.substr(0, dot) : inp) + ".o";
        }

        std::string cmd = prefix + shell_quote(cc) + " -c " + shell_quote(tmp_asm) + " -o " + shell_quote(dest_obj);
        int rc = std::system(cmd.c_str());
        unlink(tmp_asm);
        if (rc != 0) return 1;
        dump_object(dest_obj, dump_symbols, dump_sections, dump_relocs);
      }
      return 0;
    }

    if (inputs.size() == 1 && !compile_only && !emit_asm && !preprocess_only) {
      std::string inp = inputs[0];
      if (inp.rfind(".o") == inp.size() - 2 || inp.rfind(".a") == inp.size() - 2) {
        std::string out_name = output.empty() ? "a.out" : output;
        std::string cmd = prefix + shell_quote(cc) + " " + shell_quote(inp);
        for (const auto &flag : link_flags) cmd += " " + shell_quote(flag);
        cmd += " -o " + shell_quote(out_name);
        return std::system(cmd.c_str()) == 0 ? 0 : 1;
      }
      Diagnostics::filepath = inp;
      std::string asm_text = Backend::compile_asm(read_file(inp), target, dump_ast, dump_ir);
      
      char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
      int fd = mkstemps(tmp_asm, 2);
      if (fd < 0) Diagnostics::fatal("cannot create temporary file");
      close(fd);
      write_file(tmp_asm, asm_text);

      std::string out_name = output.empty() ? "a.out" : output;
      std::string cmd = prefix + shell_quote(cc) + " " + shell_quote(tmp_asm);
      for (const auto &flag : link_flags) cmd += " " + shell_quote(flag);
      cmd += " -o " + shell_quote(out_name);
      int rc = std::system(cmd.c_str());
      unlink(tmp_asm);
      return rc == 0 ? 0 : 1;
    }

    std::vector<std::string> temp_objects;
    std::vector<std::string> link_cmd_args;
    for (const auto &inp : inputs) {
      if (inp.rfind(".o") == inp.size() - 2 || inp.rfind(".a") == inp.size() - 2) {
        link_cmd_args.push_back(inp);
        continue;
      }
      Diagnostics::filepath = inp;
      std::string asm_text = Backend::compile_asm(read_file(inp), target, dump_ast, dump_ir);
      
      char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
      int fd_asm = mkstemps(tmp_asm, 2);
      if (fd_asm < 0) Diagnostics::fatal("cannot create temporary file");
      close(fd_asm);
      write_file(tmp_asm, asm_text);

      char tmp_obj[] = "/tmp/b1cc-XXXXXX.o";
      int fd_obj = mkstemps(tmp_obj, 2);
      if (fd_obj < 0) Diagnostics::fatal("cannot create temporary file");
      close(fd_obj);
      temp_objects.push_back(tmp_obj);
      link_cmd_args.push_back(tmp_obj);

      std::string cmd = prefix + shell_quote(cc) + " -c " + shell_quote(tmp_asm) + " -o " + shell_quote(tmp_obj);
      int rc = std::system(cmd.c_str());
      unlink(tmp_asm);
      if (rc != 0) {
        for (const auto &to : temp_objects) unlink(to.c_str());
        return 1;
      }
    }

    std::string out_name = output.empty() ? "a.out" : output;
    std::string link_cmd = prefix + shell_quote(cc);
    for (const auto &obj : link_cmd_args) {
      link_cmd += " " + shell_quote(obj);
    }
    for (const auto &flag : link_flags) {
      link_cmd += " " + shell_quote(flag);
    }
    link_cmd += " -o " + shell_quote(out_name);

    int rc = std::system(link_cmd.c_str());
    for (const auto &to : temp_objects) {
      unlink(to.c_str());
    }
    return rc == 0 ? 0 : 1;
  }
}

int main(int argc, char **argv) {
  return Driver::run(argc, argv);
}

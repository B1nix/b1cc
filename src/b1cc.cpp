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

  static int run(int argc, char **argv) {
    bool emit_asm = false;
    std::string input;
    std::string output = "a.out";
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
        Diagnostics::fatal("unknown option " + arg);
      } else if (input.empty()) {
        input = arg;
      } else {
        Diagnostics::fatal("multiple input files are not supported");
      }
    }

    if (input.empty())
      Diagnostics::fatal("usage: b1cc [-S] input.c [-o output]");

    if (target == "arm64-darwin") {
      Preprocessor::driver_macros["stdin"] = {false, {}, "__stdinp"};
      Preprocessor::driver_macros["stdout"] = {false, {}, "__stdoutp"};
      Preprocessor::driver_macros["stderr"] = {false, {}, "__stderrp"};
    }

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

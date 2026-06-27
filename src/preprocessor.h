#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <string>
#include <vector>
#include <map>
#include <set>

namespace Preprocessor {
  struct Macro {
    bool is_function_like = false;
    std::vector<std::string> params;
    std::string body;
  };

  struct CondState {
    bool condition_met;
    bool active;
  };

  extern std::vector<std::string> driver_include_dirs;
  extern std::map<std::string, Macro> driver_macros;

  std::string preprocess(const std::string &src, const std::string &filepath, const std::vector<std::string> &include_dirs, std::map<std::string, Macro> &macros, std::set<std::string> &included_files);
}

#endif // PREPROCESSOR_H
